// nubjuk-mcu main entry.
// P1.1 단계: audio_capture self-check 모드. voice_queue 를 drain 하면서 frame count
// + drop + window peak amplitude 를 주기적으로 시리얼에 출력. wake / sti / fsm 미진입.

#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "audio_capture.h"
#include "config.h"

static const char *TAG = "nubjuk";

static inline int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

// 임시 self-check task. P1.4 voice_coordinator 들어오면 voice_task 로 교체.
// peak: 100 frame window 안의 |sample| 최댓값 (int16 범위 0~32767).
//   조용한 환경 < ~500, 일반 대화 ~수천, 박수/큰 소리 > 1만. 0 고정 = 와이어링/SEL 불량 의심.
static void self_check_task(void *arg)
{
    uint32_t count = 0;
    int32_t window_peak = 0;
    TickType_t start = xTaskGetTickCount();

    while (1) {
        int16_t *frame = NULL;
        if (xQueueReceive(voice_queue, &frame, portMAX_DELAY) == pdTRUE) {
            count++;

            int32_t fp = 0;
            for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
                int32_t v = abs_i32((int32_t)frame[i]);
                if (v > fp) fp = v;
            }
            if (fp > window_peak) window_peak = fp;

            free(frame);

            if (count % 100 == 0) {
                uint32_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start);
                float rate = (elapsed_ms > 0) ? (count * 1000.0f / elapsed_ms) : 0.0f;
                ESP_LOGI("self-check",
                         "frames=%lu elapsed=%lums rate=%.1f/s dropped=%lu peak=%ld",
                         (unsigned long)count, (unsigned long)elapsed_ms, rate,
                         (unsigned long)audio_capture_get_dropped(),
                         (long)window_peak);
                window_peak = 0;
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot");

    ESP_ERROR_CHECK(audio_capture_init());

    BaseType_t ok = xTaskCreate(
        self_check_task, "self-check", 4096, NULL, 5, NULL
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "self-check task create fail");
    }
}
