// nubjuk-mcu sti_engine interface (LOCKED)
// 시그니처 변경은 사용자 명시 승인 필요. INTERFACES.md 와 동기화 유지.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char  intent[32];
    float confidence;
    char  slots_json[256];   // 슬롯은 JSON 문자열로 평탄화
} sti_result_t;

typedef struct {
    uint32_t    max_utterance_ms;
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

// Factory functions. 구현 phase:
//   sti_rhino_create — P1.3
//   sti_brain_create — P4
//   sti_dual_create  — P4
sti_engine_t *sti_rhino_create(const char *rhn_path, const char *access_key);
sti_engine_t *sti_brain_create(const char *brain_url);
sti_engine_t *sti_dual_create(sti_engine_t *primary, sti_engine_t *fallback);

#ifdef __cplusplus
}
#endif
