# MCU — 인터페이스 정의 (Source of Truth)

> 🔒 이 문서의 코드 블록은 **잠금**입니다. 시그니처·struct 멤버·enum 값을 사용자 명시 승인 없이 변경 불가. 새 구현체를 작성할 때 이 시그니처를 그대로 만족해야 합니다. 자세한 잠금 정책은 `CLAUDE.md` 참고.

## `mcu/main/wake/wake_engine.h`

```c
typedef enum { WAKE_EV_DETECTED, WAKE_EV_TIMEOUT } wake_event_t;

typedef struct {
    void (*on_event)(void *ctx, wake_event_t ev);
    void *ctx;
} wake_callbacks_t;

typedef struct wake_engine {
    esp_err_t (*init)(struct wake_engine *self, const void *config);
    esp_err_t (*process_audio)(struct wake_engine *self, const int16_t *pcm, size_t samples);
    esp_err_t (*set_callbacks)(struct wake_engine *self, const wake_callbacks_t *cb);  // immutable after init
    void (*free)(struct wake_engine *self);
    void *impl_ctx;
} wake_engine_t;

wake_engine_t *wake_porcupine_create(const char *ppn_path, const char *access_key);  // P1~
wake_engine_t *wake_espsr_create(const char *model_id);                              // P6 옵션
```

## `mcu/main/sti/sti_engine.h`

```c
typedef struct {
    char intent[32];
    float confidence;
    char slots_json[256];   // 슬롯은 JSON 문자열로 평탄화
} sti_result_t;

typedef struct {
    uint32_t max_utterance_ms;
    const char *correlation_id;
} sti_session_opts_t;

typedef struct {
    void (*on_session_started)(void *ctx);
    void (*on_intent)(void *ctx, const sti_result_t *result);
    void (*on_error)(void *ctx, const char *reason);
    void *ctx;
} sti_callbacks_t;

typedef struct sti_engine {
    esp_err_t (*init)(struct sti_engine *self, const void *config);
    esp_err_t (*start_session)(struct sti_engine *self, const sti_session_opts_t *opts);
    esp_err_t (*process_audio)(struct sti_engine *self, const int16_t *pcm, size_t samples);
    esp_err_t (*finish_session)(struct sti_engine *self);                              // utterance 종료 명시
    esp_err_t (*cancel_session)(struct sti_engine *self);                              // abort
    esp_err_t (*set_callbacks)(struct sti_engine *self, const sti_callbacks_t *cb);   // immutable after init
    void (*free)(struct sti_engine *self);
    void *impl_ctx;
} sti_engine_t;

sti_engine_t *sti_rhino_create(const char *rhn_path, const char *access_key);                  // P1~
sti_engine_t *sti_brain_create(const char *brain_url);                                         // P4
sti_engine_t *sti_dual_create(sti_engine_t *primary, sti_engine_t *fallback);                  // P4
```

**Lifecycle 규칙**:
- `set_callbacks` → `init` → `start_session` → `process_audio*` → `finish_session` 또는 `cancel_session` → 다시 `start_session` 가능
- 콜백 발화 순서: `on_session_started` (start_session 후 즉시) → `on_intent` 또는 `on_error` (finish_session 후, 1회만)
- `cancel_session`은 콜백 발화하지 않음
- 동시 세션 X — 한 세션 종료 후에야 다음 `start_session` 가능

## `mcu/main/coord/voice_coordinator.h`

```c
// 내부 모듈 — wake.on_event(WAKE_EV_DETECTED)를 받아 sti.start_session 호출.
// FSM에는 직접 통지하지 않음 (sti의 콜백이 FSM에 통지).
typedef struct voice_coordinator voice_coordinator_t;

voice_coordinator_t *voice_coordinator_create(wake_engine_t *wake, sti_engine_t *sti);
void voice_coordinator_free(voice_coordinator_t *self);

// 내부적으로 wake->set_callbacks를 호출하여 본인을 등록.
// correlation_id는 coordinator가 생성 (random_u64 권장).
```

## `mcu/main/motion/motion_controller.h`

```c
typedef struct {
    const char *intent;
    uint32_t    max_duration_ms;   // 내부에서 enforce. FSM은 결과만 받음.
    const char *correlation_id;
} motion_request_t;

typedef enum { MOTION_STOP_USER, MOTION_STOP_ESTOP, MOTION_STOP_PRECONDITION } motion_stop_reason_t;

typedef enum {
    MOTION_IDLE, MOTION_PLAYING, MOTION_DONE,
    MOTION_FAILED_TIMEOUT, MOTION_FAILED_HARDWARE, MOTION_FAILED_ESTOP,
} motion_state_t;

typedef struct {
    void (*on_started)(void *ctx, const char *correlation_id, uint32_t expected_ms);
    void (*on_completed)(void *ctx, const char *correlation_id, uint32_t actual_ms);
    void (*on_failed)(void *ctx, const char *correlation_id, motion_state_t reason);
    void *ctx;
} motion_callbacks_t;

typedef struct motion_controller {
    esp_err_t (*play)(struct motion_controller *self, const motion_request_t *req);
    esp_err_t (*stop)(struct motion_controller *self, motion_stop_reason_t reason);
    motion_state_t (*get_state)(struct motion_controller *self);                        // 디버그/introspection
    esp_err_t (*set_callbacks)(struct motion_controller *self, const motion_callbacks_t *cb);
    void (*free)(struct motion_controller *self);
    void *impl_ctx;
} motion_controller_t;

motion_controller_t *log_motion_controller_create(void);                                // P1~2 (UART 로그)
motion_controller_t *hardware_motion_controller_create(const motion_hw_config_t *cfg);  // P3~ (실제 모터)
motion_controller_t *motion_controller_mock_create(void);                               // 단위 테스트
```

**Lifecycle 규칙**:
- `set_callbacks` → `play(req)` → 내부에서 `on_started` 발화 → `on_completed` 또는 `on_failed` (단 1회)
- `play` 중 `stop()` 호출 시 `on_failed(MOTION_FAILED_ESTOP/USER/PRECONDITION)`
- `max_duration_ms` 위반은 motion_controller 내부에서 enforce, `on_failed(MOTION_FAILED_TIMEOUT)`
- 동시 play X — 진행 중일 때 다시 play 호출하면 `ESP_ERR_INVALID_STATE`

## state_machine 조립 시그니처

```c
// state_machine.c — sti와 motion만 보유. wake는 coordinator가 흡수.
void state_machine_init(sti_engine_t *sti, motion_controller_t *motion);

// 콜백 등록은 main.c에서 set_callbacks로 외부 주입.
// state_machine 내부 함수 (콜백 핸들러):
void fsm_on_session_started(void *ctx);                    // → idle to listening
void fsm_on_intent(void *ctx, const sti_result_t *r);      // → validating → executing or rejected
void fsm_on_sti_error(void *ctx, const char *reason);      // → rejected
void fsm_on_motion_started(void *ctx, const char *cid, uint32_t expected_ms);
void fsm_on_motion_completed(void *ctx, const char *cid, uint32_t actual_ms);
void fsm_on_motion_failed(void *ctx, const char *cid, motion_state_t reason);
```

## 조립 패턴 (`main.c`)

```c
// 1. 인터페이스 구현 생성
wake_engine_t       *wake = wake_porcupine_create(PPN_PATH, PV_KEY);
sti_engine_t        *sti  = sti_rhino_create(RHN_PATH, PV_KEY);
motion_controller_t *mc   = log_motion_controller_create();

// 2. coordinator가 wake와 sti 결합 (wake는 sti를 모름)
voice_coordinator_t *coord = voice_coordinator_create(wake, sti);

// 3. FSM 초기화 (wake 미주입)
state_machine_init(sti, mc);

// 4. 콜백 일괄 등록 (immutable, init 직후 1회만)
sti->set_callbacks(sti, &(sti_callbacks_t){
    .on_session_started = fsm_on_session_started,
    .on_intent          = fsm_on_intent,
    .on_error           = fsm_on_sti_error,
    .ctx                = fsm,
});
mc->set_callbacks(mc, &(motion_callbacks_t){
    .on_started   = fsm_on_motion_started,
    .on_completed = fsm_on_motion_completed,
    .on_failed    = fsm_on_motion_failed,
    .ctx          = fsm,
});
```

## Phase 변경 시 교체 위치

Phase 3 (motion 실제):
```diff
- motion_controller_t *mc = log_motion_controller_create();
+ motion_controller_t *mc = hardware_motion_controller_create(&motor_cfg);
```

Phase 4 (brain 추가):
```diff
- sti_engine_t *sti = sti_rhino_create(RHN_PATH, PV_KEY);
+ sti_engine_t *brain = sti_brain_create(BRAIN_URL);
+ sti_engine_t *rhino = sti_rhino_create(RHN_PATH, PV_KEY);
+ sti_engine_t *sti   = sti_dual_create(brain, rhino);
```

→ FSM·voice_coordinator·콜백 함수 코드는 **변경 없음**.
