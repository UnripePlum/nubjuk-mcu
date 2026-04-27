# MCU — Phase별 구현 계획

> Phase 0 → 1 → 2 → 3 → 4 → 5 → 6 순서대로 진행. 임의로 phase 순서 바꾸지 말 것.
> 인터페이스 시그니처는 `INTERFACES.md`에 잠금 — 그대로 만족해야 함.

## Phase 0 — 부트스트랩 (mcu 작업 없음)

mcu 디렉토리는 비어있음. 다음만 준비:
- [ ] Picovoice Console 계정 생성, AccessKey 발급 (개발 무료 티어)
- [ ] Wake word `.ppn` 빌드 의뢰: "넙죽 훈련병" (한국어, ESP32-S3 타깃)
- [ ] Rhino context `.rhn` 빌드 (`rhino/context.yml` 입력)
- [ ] ESP-IDF v5.x 설치 + ESP32-S3 보드 셋업

---

## Phase 1 — ESP standalone (오디오 + intent, motion 모의)

**목표**: 오프라인에서 음성 → intent JSON을 시리얼로 출력. WS·viewer·motor 전부 없음.

### 디렉토리 스캐폴딩
```
mcu/
├── CMakeLists.txt
├── sdkconfig (PSRAM 8MB, Wi-Fi 활성)
├── partitions.csv
├── main/
│   ├── main.c
│   ├── audio_capture.c|h
│   ├── state_machine.c|h
│   ├── config.h
│   ├── wake/
│   │   ├── wake_engine.h            # 인터페이스 (잠금)
│   │   └── wake_engine_porcupine.c  # 구현
│   ├── sti/
│   │   ├── sti_engine.h             # 인터페이스 (잠금)
│   │   └── sti_engine_rhino.c       # 구현
│   ├── coord/
│   │   ├── voice_coordinator.h      # 인터페이스 (잠금)
│   │   └── voice_coordinator.c      # 구현
│   └── motion/
│       ├── motion_controller.h      # 인터페이스 (잠금)
│       ├── motion_registry.c|h
│       ├── log_motion_controller.c
│       └── motion_controller_mock.c
└── components/
    ├── pv_porcupine/                # Picovoice 라이브러리
    └── pv_rhino/
```

### 구현 task

#### 1.1 Audio capture (single dispatcher + 명시적 fan-out)
- [ ] I2S 드라이버 init (INMP441 마이크, 16kHz mono PCM)
- [ ] **DMA buffer**: count=4, length=512 samples (32ms), **internal RAM 배치** (PSRAM은 latency 위험)
- [ ] **Audio frame 슬라이싱**: DMA 32ms → Porcupine/Rhino 요구 frame size로 split (Porcupine ≈ 32ms = 512 samples, Rhino ≈ 32ms 동일)
- [ ] **Single dispatcher pattern** (race 방지):
  - `audio_task` (Core 1, prio 22) — DMA에서 읽고 frame slice 후 `voice_queue`에 enqueue. 그 외 작업 X
  - `voice_task` (Core 1, prio 20) — `voice_queue`에서 frame 하나씩 꺼내, **현재 FSM 상태에 따라 라우팅**:
    - state=idle → wake.process_audio만 호출
    - state=listening → sti.process_audio만 호출 (wake는 무시)
  - 두 엔진 동시 feed 안 함 — race/double-consume 차단
- [ ] **Backpressure**: voice_queue 가득 차면 drop (count + log), motion 안전성 우선
- [ ] **Diagnostic counters**: dropped_frames, ring_overflow, processing_time_max — heartbeat에 포함
- [ ] **Frame format 검증**: I2S init 시 sample rate, bit depth, channel count 실측 후 단언

#### 1.2 wake_engine_porcupine
- [ ] `wake_engine_t` 인터페이스 구현 (`init/process_audio/set_callbacks/free`)
- [ ] Porcupine 핸들 생성 (`.ppn` + AccessKey)
- [ ] `process_audio`에서 keyword 감지 시 `WAKE_EV_DETECTED` event 발화
- [ ] callback 호출은 **voice_task 컨텍스트** (ISR 금지)

#### 1.3 sti_engine_rhino (lifecycle + confidence semantics)
- [ ] `sti_engine_t` 인터페이스 구현 (session lifecycle)
- [ ] Rhino 핸들 생성 (`.rhn` + AccessKey), 부팅 시 1회 init
- [ ] **Session lifecycle** (Picovoice Rhino API 매핑):
  - `start_session(opts)` → Rhino 핸들 reset, `voice_task` 컨텍스트에서 `on_session_started` 즉시 발화
  - `process_audio(pcm, n)` → `pv_rhino_process(handle, frames, &is_finalized)` 호출
  - `is_finalized == true` 시 즉시 `pv_rhino_get_intent` → `on_intent` 또는 `on_error` 콜백 1회 발화 (자동 finalize)
  - `finish_session()` → Rhino 강제 finalize 호출 (utterance 종료 명시 시)
  - `cancel_session()` → Rhino reset, 콜백 미발화
- [ ] **Confidence 결정** (Rhino C API는 confidence를 직접 노출하지 않음):
  - `pv_rhino_is_understood == false` → `intent="unknown"`, confidence=0
  - `pv_rhino_get_intent` 성공 → confidence=1.0 (Rhino는 이산 결정)
  - 즉 **사실상 confidence는 0 or 1.0**. "0.6 threshold" 표현은 Phase 4 brain 도입 후에만 의미 있음
- [ ] `on_error("not_understood")` — Rhino가 utterance를 매칭 못 했을 때
- [ ] `on_error("session_timeout")` — `max_utterance_ms` 초과해도 finalize 안 됨
- [ ] **Threading**: 모든 process_audio / 콜백 발화는 `voice_task` 컨텍스트에서. ISR에서 호출 시 ESP_ERR_INVALID_STATE
- [ ] `set_callbacks` immutable: 이미 등록됐으면 `ESP_ERR_INVALID_STATE`
- [ ] **Pre-roll 정책**: wake 감지 직후 N ms (예: 200ms) 오디오는 폐기. Rhino는 wake 후 첫 utterance만 처리 (wake word 자체가 utterance 일부로 들어가는 것 방지)

#### 1.4 voice_coordinator
- [ ] `wake.set_callbacks`로 `WAKE_EV_DETECTED` 구독
- [ ] 이벤트 수신 시 `sti.start_session(opts{cid, max_utterance_ms:5000})` 호출
- [ ] correlation_id 생성 (random_u64 권장 — UUID는 MCU에서 over-engineering)
- [ ] coordinator 자체는 FSM에 직접 통지 X (sti의 콜백이 FSM에 통지)

#### 1.5 state_machine (busy/single-flight 명시)
- [ ] FSM 상태 enum: `idle | listening | intent_recognized | validating | executing | completed | rejected | motion_failed`
- [ ] `state_machine_init(sti, motion)` — wake는 받지 않음
- [ ] sti 콜백 등록: `on_session_started → idle→listening`, `on_intent → 검증 후 validating/rejected`
- [ ] motion 콜백 등록: `on_started → state push`, `on_completed → completed`, `on_failed → motion_failed`
- [ ] precondition 검증 (`motion_registry`에서 lookup)
- [ ] cycle_deadline_ms 검사 (5000ms)
- [ ] Phase 1엔 viewer 없으니 state push 대신 시리얼 로그
- [ ] **Busy/single-flight contract** (idle이 아닐 때 새 wake 처리):
  - `idle` → wake 정상 처리
  - `listening | intent_recognized | validating | executing | completed | rejected | motion_failed` → **새 wake 무시**. audio fan-out에서 voice_task가 sti.process_audio만 호출하므로 wake 자체가 자동으로 무시됨. voice_coordinator도 신호 받으면 무시 + busy 카운터 증가 + 시리얼 로그
  - busy로 거부된 wake는 viewer push 시 `error{code:"busy"}` (Phase 2부터)
  - audio_task/voice_task가 dispatcher로 라우팅하므로 자동 enforce — 별도 큐 적재 X
- [ ] **State 전이 atomicity**: 모든 전이는 `voice_task` 단일 thread에서만 (mutex 불필요)
- [ ] **잘못된 전이 정책**: 정의되지 않은 전이 시 `ESP_LOGE` + 무시 (panic 안 함)

#### 1.6 LogMotionController
- [ ] `motion_controller_t` 구현 — UART에 `[motion] sit start (1500ms)` 식으로 로그
- [ ] **자체 motion_task 소유** (Core 0, prio 18) — `motion_request` 큐를 받아 처리. 콜백은 이 task 컨텍스트에서 발화
- [ ] play 호출 → 큐에 적재 → motion_task가 `vTaskDelay(pdMS_TO_TICKS(max_duration_ms))` 후 `on_completed` 발화
- [ ] `stop()`: 진행 중이면 abort + `on_failed(MOTION_FAILED_ESTOP/USER/PRECONDITION)`
- [ ] `set_callbacks` immutable

#### 1.7 motion_registry
- [ ] intent enum 7종 (sit/stand/roll_left/roll_right/surprise/idle/unknown)
- [ ] 각 intent의 `max_duration_ms`, `preconditions[]` 정의
- [ ] `motion_registry_lookup(intent_name) → recipe_t*` API

#### 1.8 main.c 조립 (task topology 표 명시)
- [ ] Wi-Fi connect (sdkconfig SSID/PWD)
- [ ] Wi-Fi 연결 직후 시리얼에 명확히 IP 출력: `>>> ws://192.168.x.x/viewer ready` — Phase 2 viewer 연결 대비 (사용자가 viewer 쪽에 직접 입력)
- [ ] factory 호출:
  ```c
  wake_engine_t *wake = wake_porcupine_create(PPN, KEY);
  sti_engine_t  *sti  = sti_rhino_create(RHN, KEY);
  motion_controller_t *mc = log_motion_controller_create();
  voice_coordinator_t *coord = voice_coordinator_create(wake, sti);
  state_machine_init(sti, mc);
  ```
- [ ] **FreeRTOS task topology** (잠금):

| Task | Core | Priority | Stack | 책임 | 호출 가능 API |
|------|------|---------|-------|------|-------------|
| `audio_task` | 1 | 22 | 4 KB | I2S DMA → frame slice → `voice_queue` enqueue | I2S read, queue post (non-blocking) |
| `voice_task` | 1 | 20 | 16 KB | `voice_queue` consume + dispatch (wake 또는 sti) + 콜백 발화 | wake/sti API, FSM 전이, ws broadcast post |
| `motion_task` | 0 | 18 | 8 KB | `motion_request` 큐 처리 + watchdog enforce + 모터 제어 | motion API, GPIO, FSM 콜백 발화 |
| `ws_task` (P2~) | 0 | 12 | 8 KB | viewer broadcast 큐 송신 (esp_http_server worker와 별도) | WS send (blocking 허용) |
| `wifi/lwip` (시스템) | 0 | 23 | (esp default) | Wi-Fi stack | — |

- [ ] **Priority 원칙**: 시스템 task(wifi/timer) > audio > voice > motion > ws. **"highest" 사용 금지** — 시스템 task를 굶기면 안 됨
- [ ] **Queue topology**:
  - `voice_queue`: capacity 8 frames (32ms × 8 = 256ms 버퍼), full 시 drop + count
  - `motion_queue`: capacity 1 (single-flight), full 시 reject (`ESP_ERR_INVALID_STATE`)
  - `ws_broadcast_queue` (P2~): capacity 16, full 시 oldest drop
- [ ] **Stack 검증**: Phase 1 Gate에 `uxTaskGetStackHighWaterMark` 측정값 추가 — 최소 1 KB headroom 확보
- [ ] **Heap 측정**: 부팅 직후 / executing 중 free_heap 시리얼 로그 — 100KB 이상 유지

### Phase 1 Gate (verification)

- [ ] **한국어 코퍼스 150 샘플 intent accuracy ≥ 90%** (Picovoice Rhino 한국어). 데이터셋 정의: 7 intent × 5 변형 × 3 노이즈(조용/일반/시끄러움) × 2 화자 + 30 distractor. 발화 거리·디바이스 명시
- [ ] **End-to-end P95 ≤ 800ms** (Porcupine 감지 시각 → motion 모의 시작 시각, ts_ms diff)
- [ ] Porcupine FAR < 1/시간 (1시간 침묵 + TV/일반 노이즈 1시간 측정)
- [ ] cycle_timeout 동작 (5초간 발화 안 하면 시리얼에 reject 출력)
- [ ] `intent="unknown"` 또는 not_understood 시 reject 처리
- [ ] FSM 모든 전이가 시리얼 로그에 기록됨
- [ ] 각 task `uxTaskGetStackHighWaterMark` 측정값 ≥ 1 KB headroom
- [ ] free_heap (executing 중) ≥ 100 KB

---

## Phase 2 — WS server + viewer 연동

**목표**: ESP가 viewer의 WS 서버 호스팅. FSM 전이를 viewer에 push.

### 추가 task

#### 2.1 ws_viewer_server
- [ ] `esp_http_server` 시작, `/viewer` WebSocket handler 등록
- [ ] **Phase 2 default = single viewer** (cap=1). 추가 연결 시도 시 close 후 `error{code:"single_viewer_occupied"}` 응답
- [ ] envelope 빌더 (v/type/ts_ms/device_id/boot_id/seq/correlation_id/payload)
- [ ] `boot_id` 부팅 시 1회 생성 (random_u64 hex 표기)
- [ ] `seq` 단조 증가 카운터 (uint32, wraparound 시 viewer가 boot_id로 식별)
- [ ] FSM 전이마다 `state` 메시지 push
- [ ] `intent`/`motion_started`/`motion_completed`/`motion_failed`/`error` push
- [ ] heartbeat 5초 주기 (`heartbeat` 메시지)
- [ ] viewer 메시지 파싱 + dispatch:
  - `subscribe`: viewer 등록
  - `manual_trigger`: DEV_MODE에서만 처리 — 새 cid 발급 후 FSM 강제 진입
  - `ping`: 무시 (단순 keepalive)
- [ ] schema 위반 메시지 → `error{code:"invalid_message"}`
- [ ] 연결 직후 `hello` 자동 송신

#### 2.2 manual_trigger 경로
- [ ] sdkconfig `CONFIG_NUBJUK_DEV_MODE` 옵션 추가 (default y in dev, n in prod)
- [ ] DEV_MODE=n에서 manual_trigger 메시지 받으면 `error{code:"manual_trigger_disabled"}`
- [ ] DEV_MODE=y에서 manual_trigger → FSM의 `manual_trigger_intent` 진입점 호출
- [ ] FSM은 wake 없이 `intent_recognized` 상태로 직접 진입 (sti 우회)

#### 2.3 FSM ↔ viewer 통합
- [ ] state_machine.c에서 `viewer_broadcast_*` 함수 호출 추가
- [ ] viewer 끊겨도 FSM 동작 영향 없음 (degraded mode)

### Phase 2 Gate

- [ ] viewer 연결 시 `hello` snapshot 정상 수신
- [ ] FSM 전이 시 viewer가 `state` 메시지 모두 받음
- [ ] `intent` / `motion_started` / `motion_completed` 모두 viewer UI 반영
- [ ] DebugButtons → manual_trigger → ESP가 motion 모의 실행
- [ ] DEV_MODE=n 빌드에서 manual_trigger 거부됨
- [ ] heartbeat 5초 주기 유지, 30초 끊기면 viewer 측 reconnect
- [ ] viewer 끊김 후 재연결 시 새 hello 수신
- [ ] `free_heap` (executing + 1 viewer 연결) > 100 KB
- [ ] busy 시 새 wake → `error{code:"busy"}` 송신
- [ ] 두 번째 viewer 연결 시도 → `single_viewer_occupied` 응답 후 close

---

## Phase 3 — HardwareMotionController + safety

**목표**: 실제 모터 구동 + 안전 장치.

### 추가 task

#### 3.1 HardwareMotionController (bounded priority + 모터 hardware-specific)
- [ ] `motion_controller_t` 구현 — 모터 드라이버 (서보/PWM/I2C) 제어
- [ ] `motion_request_t.intent`별 motor 시퀀스 (motion_registry에서 lookup)
- [ ] 별도 `motion_task` (Core 0, **priority 18 — 시스템 task 23보다 낮음**, ws/일반보다 높음). "highest"는 사용 금지
- [ ] **내부 watchdog** — `motion_deadline_ms` 자체 enforce, 초과 시 motor disable + `on_failed(TIMEOUT)`
- [ ] `stop()` 호출 시 motor 즉시 안전 정지 + `on_failed(MOTION_FAILED_USER/ESTOP/PRECONDITION)`
- [ ] **모터 driver 의존**: motor enable/disable 핀 명시. PWM-only servo는 enable 핀이 없으므로 별도 power MOSFET 또는 driver chip의 hardware enable 라인 필요

#### 3.2 Emergency stop
- [ ] GPIO 인터럽트 ISR 등록 (e-stop 핀, debounce 50ms)
- [ ] ISR에서 motor disable GPIO 직접 set (FreeRTOS API 호출 금지, 최소 작업)
- [ ] ISR이 `motion_task`에 abort signal — `xQueueSendFromISR` 또는 `xTaskNotifyFromISR` (ISR-safe API만)
- [ ] motion_task가 `on_failed(ESTOP)` 콜백 발화
- [ ] e-stop 후 복구 정책: **latched** (수동 reset 명시 후에야 다음 motion 가능)

#### 3.3 Boot-time safe posture
- [ ] main.c 부팅 직후 motor driver disable 라인을 active 설정 — **outputs inhibited until calibration**
- [ ] Wi-Fi 연결 전, FSM 시작 전에 처리
- [ ] 필요 시 calibration 시퀀스 후 known-safe 위치로 이동

#### 3.4 esp_task_wdt
- [ ] motion_task에 watchdog 등록 (5초)
- [ ] audio_task / voice_task / ws_task 모두 watchdog 등록
- [ ] 모든 task가 정상 yield 하는지 확인

#### 3.5 main.c 교체
- [ ] `log_motion_controller_create()` → `hardware_motion_controller_create(&cfg)` 로 factory 변경
- [ ] FSM 코드는 **변경 없음**

### Phase 3 Gate

- [ ] preconditions 위반 → `state{to:rejected, reason:"precondition"}`
- [ ] max_duration_ms 초과 → motor 강제 정지 + `motion_failed{reason:"timeout"}`
- [ ] e-stop GPIO trigger → 즉시 motor cut (oscilloscope로 검증, 응답 시간 측정)
- [ ] e-stop latched: 수동 reset 전까지 다음 motion 거부
- [ ] motion_task가 Core 0, priority 18로 실행 중 (esp-idf-monitor 확인)
- [ ] Boot-time에 motor driver disable로 시작 — outputs inhibited
- [ ] WS task를 강제 정지(suspend)해도 motion safety 정상 (fault injection)
- [ ] Brownout 시뮬레이션 — 전원 sag 후 motor가 안전 자세로 복구

---

## Phase 4 — brain 추가 (선택적)

**목표**: 자연어 자유도가 필요해질 때 brain 도입. Rhino를 fallback으로 유지.

### 추가 task

#### 4.1 sti_engine_brain
- [ ] `sti_engine_t` 구현 — `esp_websocket_client`로 brain 연결
- [ ] `start_session` → WS connect + `session_start` JSON 송신
- [ ] `process_audio` → binary frame `[seq_u16 BE][flags_u8][reserved_u8][PCM]` 송신 (자세한 포맷은 `docs/protocol/mcu-brain.md`)
- [ ] `finish_session` → `session_end` 송신, intent 응답 대기
- [ ] `cancel_session` → `session_cancel` 송신 + WS close
- [ ] brain의 `intent` 응답 수신 → `on_intent` 콜백
- [ ] `session_busy` / `error` 수신 → `on_error` 콜백
- [ ] WS connect timeout 500ms (LAN 가정), refused/timeout 모두 → `on_error("brain_unreachable")`
- [ ] **로컬 PCM 버퍼링**: dual fallback이 가능하도록 utterance 시작부터 PCM을 ring buffer (≤ 5초)에 저장. brain 실패 시 같은 PCM을 Rhino에 feed

#### 4.2 sti_engine_dual (fallback routing matrix)
- [ ] `sti_engine_t` 구현 — primary + fallback 합성
- [ ] **Routing matrix** (시점별):
  - **세션 시작 전 brain 연결 실패** (timeout/refused) → 즉시 Rhino로
  - **session_busy 응답** → 즉시 Rhino로
  - **session_ack 후 mid-session WS drop** → 버퍼된 PCM을 Rhino에 feed (1회 retry)
  - **session_end 후 응답 없이 5초 경과** → 같은 처리
  - **schema-invalid 응답** → fail (둘 다 안 함, on_error)
- [ ] viewer에 fallback 활성 시 `error{code:"brain_unreachable"}` informational push
- [ ] **PCM 버퍼 크기 제한**: 5초 × 16kHz × 2바이트 = 160 KB (PSRAM에 배치)

#### 4.3 main.c factory 변경
- [ ] `sti_rhino_create()` → `sti_dual_create(brain, rhino)` 로 변경
- [ ] FSM·voice_coordinator 코드는 **변경 없음**

### Phase 4 Gate
- [ ] brain 정상 동작 시 brain 응답 사용
- [ ] brain 끊김 시 500ms 내 Rhino fallback 활성, 같은 utterance에 대해 결과 반환
- [ ] viewer가 fallback 발생 시 informational error 표시
- [ ] 자연어 변형 ("좀 앉아볼래?" 등) 인식률 향상 측정 (Rhino 단독 대비 +20% 이상)

---

## Phase 5 — Unity viewer (mcu 변경 거의 없음)

mcu 코드는 그대로. Unity가 같은 WS 프로토콜을 따름.
- [ ] `subscribe.client_kind = "unity"` 메시지 수용 (기존 코드 이미 일반화됨)
- [ ] (선택) Phase 5에서 다중 viewer 허용으로 cap 상향 검토 — 이때 free_heap 재측정

---

## Phase 6 — 양산화

#### 6.1 wake_espsr (선택, 라이선스 비용 절감)
- [ ] `wake_engine_t` 구현 (`wake_engine_espsr.c`)
- [ ] ESP-SR Skainet 도구로 "넙죽 훈련병" 모델 학습
- [ ] FAR/FRR 측정으로 Porcupine과 비교

#### 6.2 DEV_MODE 비활성
- [ ] sdkconfig `CONFIG_NUBJUK_DEV_MODE=n`
- [ ] manual_trigger 차단 자동화

#### 6.3 보안
- [ ] WS 인증 토큰 (viewer ↔ ESP)
- [ ] WSS (TLS) + 서버 인증서 trust store
- [ ] OTA 펌웨어 업데이트 (서명 검증, anti-rollback)
- [ ] NVS 암호화 (AccessKey 등 secret 보호)
- [ ] Factory reset 절차

#### 6.4 Watchdog 강화
- [ ] esp_task_wdt 모든 task
- [ ] coredump 파티션 + crash reporting (NVS 저장)
- [ ] reset reason / boot loop 감지 → safe mode

#### 6.5 24h+ soak
- [ ] 연속 가동 24시간 (memory leak slope ≤ 100 bytes/h)
- [ ] heartbeat 누락 / WS 재연결 안정성
- [ ] Power-cycle 100회 — boot loop 0회
- [ ] Wi-Fi flap 테스트 — 재연결 100회 정상
- [ ] Brownout 테스트
- [ ] 열 안정성 — 35°C 환경에서 24시간

### Phase 6 Gate
- [ ] DEV_MODE=n 빌드에서 manual_trigger 차단 검증
- [ ] WS 인증 토큰 동작
- [ ] 24h soak 무사 통과 (memory stable, 모션 정확도 유지)
- [ ] OTA 배포 round-trip 1회 성공 + rollback 동작
- [ ] coredump 캡처 후 정상 reboot 동작
