// nubjuk-mcu audio capture — I2S mic 입력, audio_task, voice_queue producer.

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// voice_queue payload: int16_t* (length AUDIO_FRAME_SAMPLES). Consumer 가 free.
// audio_task 가 malloc + post. Send 실패 시 audio_task 가 직접 free + dropped++.
extern QueueHandle_t voice_queue;

// I2S init + audio_task 생성. 부팅 직후 1회 호출.
esp_err_t audio_capture_init(void);

// 모니터링용. 마지막 boot 이후 누적.
uint32_t audio_capture_get_dropped(void);

#ifdef __cplusplus
}
#endif
