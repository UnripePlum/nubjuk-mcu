#include "audio_capture.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

static const char *TAG = "audio";

QueueHandle_t voice_queue = NULL;

static i2s_chan_handle_t rx_chan = NULL;
static volatile uint32_t s_dropped = 0;

// I2S DMA 가 ICS43434 의 24-bit data 를 32-bit slot 의 MSB align 으로 전달.
// 상위 16 bits 만 추출 (>> 16) 해서 int16_t voice frame 으로 변환.
static void audio_task(void *arg)
{
    const size_t raw_bytes = AUDIO_FRAME_SAMPLES * sizeof(int32_t);
    int32_t *raw = malloc(raw_bytes);
    if (!raw) {
        ESP_LOGE(TAG, "raw buf malloc fail");
        vTaskDelete(NULL);
    }

    while (1) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(rx_chan, raw, raw_bytes, &bytes_read, portMAX_DELAY);
        if (err != ESP_OK || bytes_read != raw_bytes) {
            ESP_LOGW(TAG, "i2s_read err=%d bytes=%zu", err, bytes_read);
            s_dropped++;
            continue;
        }

        int16_t *frame = malloc(AUDIO_FRAME_SAMPLES * sizeof(int16_t));
        if (!frame) {
            s_dropped++;
            continue;
        }
        for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
            frame[i] = (int16_t)(raw[i] >> 16);
        }

        if (xQueueSend(voice_queue, &frame, 0) != pdTRUE) {
            free(frame);
            s_dropped++;
        }
    }
}

esp_err_t audio_capture_init(void)
{
    voice_queue = xQueueCreate(VOICE_QUEUE_DEPTH, sizeof(int16_t *));
    if (!voice_queue) {
        return ESP_ERR_NO_MEM;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = AUDIO_FRAME_SAMPLES;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK_GPIO,
            .ws   = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_DIN_GPIO,
            .invert_flags = { false, false, false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    // audio_task: Core 1, prio 22 (PHASES.md task topology 표).
    BaseType_t ok = xTaskCreatePinnedToCore(
        audio_task, "audio", 4096, NULL, 22, NULL, 1
    );
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "init ok: %d Hz, %d samples/frame, queue depth %d",
             AUDIO_SAMPLE_RATE, AUDIO_FRAME_SAMPLES, VOICE_QUEUE_DEPTH);
    return ESP_OK;
}

uint32_t audio_capture_get_dropped(void)
{
    return s_dropped;
}
