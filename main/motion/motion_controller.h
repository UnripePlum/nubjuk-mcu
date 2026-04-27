// nubjuk-mcu motion_controller interface (LOCKED)
// 시그니처 변경은 사용자 명시 승인 필요. INTERFACES.md 와 동기화 유지.

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *intent;
    uint32_t    max_duration_ms;   // 내부에서 enforce. FSM 은 결과만 받음.
    const char *correlation_id;
} motion_request_t;

typedef enum {
    MOTION_STOP_USER,
    MOTION_STOP_ESTOP,
    MOTION_STOP_PRECONDITION,
} motion_stop_reason_t;

typedef enum {
    MOTION_IDLE,
    MOTION_PLAYING,
    MOTION_DONE,
    MOTION_FAILED_TIMEOUT,
    MOTION_FAILED_HARDWARE,
    MOTION_FAILED_ESTOP,
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

// Hardware config 는 P3 에서 정의. 헤더에선 opaque forward 만 둔다.
typedef struct motion_hw_config motion_hw_config_t;

// Factory functions. 구현 phase:
//   log_motion_controller_create      — P1.6 (UART 로그)
//   hardware_motion_controller_create — P3 (실 모터)
//   motion_controller_mock_create     — 단위 테스트 (이 plan NOT in scope)
motion_controller_t *log_motion_controller_create(void);
motion_controller_t *hardware_motion_controller_create(const motion_hw_config_t *cfg);
motion_controller_t *motion_controller_mock_create(void);

#ifdef __cplusplus
}
#endif
