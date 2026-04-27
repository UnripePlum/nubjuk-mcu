// nubjuk-mcu voice_coordinator interface (LOCKED)
// 시그니처 변경은 사용자 명시 승인 필요. INTERFACES.md 와 동기화 유지.
//
// 내부 모듈. wake.on_event(WAKE_EV_DETECTED) 받아 sti.start_session 호출.
// FSM 에는 직접 통지하지 않음 (sti 의 콜백이 FSM 에 통지).
// correlation_id 는 coordinator 가 생성 (random_u64 권장).

#pragma once

#include "wake/wake_engine.h"
#include "sti/sti_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct voice_coordinator voice_coordinator_t;

// Factory. 구현 phase: P1.4 (이 plan 의 NOT in scope).
// 내부적으로 wake->set_callbacks 로 본인을 등록.
voice_coordinator_t *voice_coordinator_create(wake_engine_t *wake, sti_engine_t *sti);
void voice_coordinator_free(voice_coordinator_t *self);

#ifdef __cplusplus
}
#endif
