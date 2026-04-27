// nubjuk-mcu wake_engine interface (LOCKED)
// 시그니처 변경은 사용자 명시 승인 필요. INTERFACES.md 와 동기화 유지.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// Factory functions. 구현 phase:
//   wake_porcupine_create — P1.2
//   wake_espsr_create     — P6 옵션
wake_engine_t *wake_porcupine_create(const char *ppn_path, const char *access_key);
wake_engine_t *wake_espsr_create(const char *model_id);

#ifdef __cplusplus
}
#endif
