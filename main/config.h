// nubjuk-mcu hardware + audio config
// 보드/마이크 변경 시 이 파일 한 곳만 수정.

#pragma once

#include "driver/gpio.h"
#include "driver/i2s_types.h"

// I2S microphone: ICS43434 (TDK MEMS, I2S, 24-bit MSB-aligned)
// Wiring (ESP32-S3-DevKitC-1):
//   ICS43434 BCLK / SCK   -> GPIO4
//   ICS43434 WS   / LRCL  -> GPIO5
//   ICS43434 SD   / DOUT  -> GPIO6
//   ICS43434 L/R  / SEL   -> GND  (left channel)
//   ICS43434 VDD          -> 3V3
//   ICS43434 GND          -> GND
#define MIC_I2S_PORT       I2S_NUM_0
#define MIC_BCLK_GPIO      GPIO_NUM_4
#define MIC_WS_GPIO        GPIO_NUM_5
#define MIC_DIN_GPIO       GPIO_NUM_6

// Audio capture parameters
//   16 kHz mono int16. microWakeWord 입력 frame size 와 동일 (512 samples = 32 ms).
//   brain WS streaming 도 같은 frame size 로 보내면 자연스러움.
//   voice_queue depth 8 = 256 ms 안전 마진 (timer drift 누적 대비).
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_FRAME_SAMPLES 512
#define VOICE_QUEUE_DEPTH   8
