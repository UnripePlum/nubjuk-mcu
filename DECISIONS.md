# nubjuk-mcu — Decision Log

이 파일은 mcu 모듈의 비-자명한 architecture/구현 결정을 캡처한다.

## 왜 이 파일이 있는가

`ARCHITECTURE.md` / `INTERFACES.md` / `PHASES.md`는 *what* (구조, 시그니처, 순서)만 말한다. 이 파일은 *why* (왜 그렇게 했는지, 무엇을 거부했는지, 무엇이 깨졌었는지)를 캡처한다.

이 프로젝트의 외부 audience는 다른 하드웨어 (e.g. 라즈베리파이) 로 비슷한 voice intent system을 만드는 peer 엔지니어다. 그 사람에게 product는 코드도 BOM도 동작하는 로봇도 아니다. **결정의 reasoning이 product다.** 그 reasoning이 repo에 글로 존재하지 않으면 product도 존재하지 않는 것과 같다.

## 작성 규칙

- 비-자명한 결정 만날 때마다 entry 추가 (예: "왜 voice_queue cap을 8로?", "왜 motion_task가 Core 0?")
- entry당 3~10문장. 길어지면 `ARCHITECTURE.md`로 승격
- 각 entry: **결정 / Trigger / Rationale / 거부한 대안 / (선택) 깨졌었던 시나리오**
- post-hoc 미화 금지. 처음에 뭘 시도했다가 어떻게 깨졌는지 사실 그대로
- 자명한 건 안 적음 (예: i2s_set_pin GPIO 번호). "한 달 후 내가 봐도 왜 그렇게 했는지 다시 추론해야 할 것"만 적는다
- 시간순 append. 기존 entry는 수정 X (틀렸으면 새 entry로 supersede)

## 좋은 entry 예시

```
### 2026-MM-DD — voice_queue cap을 8로 정함

- 결정: voice_queue capacity = 8 frames (32ms × 8 = 256ms 버퍼)
- Trigger: P1.1 audio_capture 짜다가 ESP-IDF timer drift로 frame burst 발생
- Rationale: cap=2일 때 timer jitter 누적 → frame drop으로 wake 감지 놓침. 256ms 버퍼면 voice_task가 잠시 stall해도 안전
- 거부한 대안:
  - cap=16 (메모리 낭비, latency budget 의미 없어짐)
  - dynamic resize (복잡도 ↑, 디버깅 어려움)
- 깨졌던 시나리오: idle에서 wake_engine이 1프레임 miss → 사용자가 두 번 말해야 동작
```

---

## Entries

### 2026-04-27 — Decision Log 운영 시작

- **결정**: 비-자명한 모든 architecture/구현 결정을 inline으로 이 파일에 기록한다.
- **Trigger**: Office-hours 브레인스토밍 세션. fork-target persona를 "라즈베리파이 엔지니어, 코드 안 가져감, 아키텍처만 가져감"으로 정의 → 이 프로젝트의 product는 코드도 로봇도 아니고 architecture reasoning이라는 결론.
- **Rationale**: 현재 `ARCHITECTURE.md` / `INTERFACES.md` / `PHASES.md`엔 *what*만 있고 *why*가 없다. P1 구현 중 만나는 실제 결정의 reasoning을 inline으로 캡처해야 사실 기반이 유지된다. 후에 reverse-engineer로 글을 쓰면 합리화/미화 위험.
- **거부한 대안**:
  - **B (Architecture-first rewrite)**: P1 정지하고 `ARCHITECTURE.md`를 5000~8000단어 prose로 expand. 거부 이유: 아직 짜보지 않은 결정에 대한 prose는 합리화/가짜가 됨. 글 without 실험 = 신뢰성 낮음.
  - **C (Repo split)**: `nubjuk` (robot) + `esp32-voice-intent-arch` (generic docs) 분리. 거부 이유: single repo에서 product 작동 검증부터 해야 함. split은 P3 이후에 재검토.
- **후속 작업**:
  - P1.1 audio_capture 진입 시 첫 실제 결정 entry 작성
  - 같은 세션에서 `PHASES.md` P4를 "선택적"에서 "필수, 핵심 contribution"으로 reframe
  - 언어: 한국어 유지 (프로젝트 일관성, code/주석은 영어)
- **언제 이 결정을 재검토할까**: P3 끝나는 시점에 entry 수가 30개 미만이면 (a) 결정이 적었거나 (b) 게을렀거나. 둘 중 어느 쪽인지 솔직히 평가.

### 2026-04-27 — Mic = ICS43434, I2S 32-bit slot mono 16 kHz, GPIO 4/5/6, int16 변환 = `>> 16`

- **결정**:
  - 마이크: TDK ICS43434 (I2S 디지털 MEMS, 24-bit MSB-aligned).
  - I2S0 master, 16 kHz Fs, **32-bit slot**, **mono** mode (L/R 핀 = GND, slot_mask = left).
  - GPIO 매핑 (ESP32-S3-DevKitC-1): BCLK=4, WS=5, DIN=6.
  - voice_queue payload = `int16_t *`, audio_task 가 malloc + 변환 (`int32 >> 16`) + send. consumer 가 free.
  - voice_queue depth = 8 (32 ms × 8 = 256 ms 버퍼).
- **Trigger**: 사용자가 plan 의 INMP441 가정 대신 ICS43434 + ESP32-S3-DevKitC-1 가 이미 있음을 알림. Step 3 (audio_capture) 진입 시점.
- **Rationale**:
  - **ICS43434 ≈ INMP441 호환**: 둘 다 -26 dBFS @ 94 dB SPL, MSB-first I2S, BCLK 1.024 MHz @ 16 kHz Fs. ICS43434 가 SNR 살짝 좋음 (65 vs 61 dBA) + 내장 HPF. Porcupine 인식엔 영향 없음. plan §"외부 의존" 의 INMP441 표기는 audience 입장에선 같은 카테고리.
  - **32-bit slot 채택**: ICS43434 가 24-bit data 를 32-bit slot 의 MSB align 으로 보냄. 16-bit slot 으로 받으면 데이터 잘림. ESP-IDF i2s_std driver 의 표준 패턴.
  - **Mono mode (slot_mask = left)**: 단일 마이크라 stereo 의 한쪽은 항상 0. mono 가 DMA 대역폭 절반 + de-interleave 불필요. P3+ 에서 듀얼 마이크 (배경 noise 제거) 들어오면 stereo 로 재검토.
  - **`>> 16` 변환**: 24-bit 중 상위 16-bit 만 사용 = 하위 8-bit 정보 손실. -26 dBFS 마이크라 +/- 32k 영역에 amplitude 충분. clipping 없음.
  - **GPIO 4/5/6**: DevKitC-1 의 free pins, 다른 보드 변경 시 config.h 한 곳만 수정.
  - **malloc per frame**: prototype default. fragmentation 측정은 P6 24h soak 까지 미룸. 깨지면 static pool 로 전환.
- **거부한 대안**:
  - **24-bit 그대로 voice_queue 전달**: Porcupine API 가 int16 강제. 변환 어쨌든 필요. 변환 위치는 audio_task 가 가장 자연스러움 (frame 단위 + 1회).
  - **`>> 8` (24-bit → upper 16 of 24-bit)**: dynamic range 더 보존. 하지만 amplitude 충분하니 단순한 `>> 16` 채택. wake 인식률 < 80% 면 재검토.
  - **Stereo mode + de-interleave**: mono 가 단순. P3+ 에서 검토.
  - **16-bit slot**: ICS43434 가 24-bit MSB align 이라 데이터 잘림. NG.
  - **DMA buffer 를 PSRAM 에**: latency hot path 라 internal RAM 가야 함 (ARCHITECTURE.md 메모리 배치 정책).
- **깨졌던 시나리오**: 없음 (구현 직후, on-device 검증 전).
- **재검토 시점**:
  - on-device self-check 에서 30s 측정 시 frame count != 938 ± 10 또는 dropped > 0 → I2S config 재점검.
  - P1 Gate 한국어 코퍼스 측정 시 wake/intent accuracy < 80% → 변환 정책 (`>> 16` vs `>> 8` vs normalize) 재검토.

### 2026-04-27 — Picovoice ESP32-S3 차단 발견 → wake = microWakeWord 한국어, sti = brain WS

이 프로젝트 가장 큰 architecture pivot.

- **결정**:
  - **wake_engine_t P1 구현체**: Picovoice Porcupine 폐기 → microWakeWord 한국어 자체 학습 모델 (TFLite Micro on ESP32-S3).
  - **sti_engine_t P1 구현체**: Picovoice Rhino 폐기 → brain (RPi + Whisper + SLM) WS client. `sti_engine_brain` 을 P4 옵션 → **P1 필수**로 끌어올림.
  - 한국어 wake "넙죽 훈련병" 정체성 유지 (Option B 선택, bootstrap 방식 거부).
- **Trigger**: Picovoice 통합 방법 조사. `github.com/Picovoice/porcupine/lib/` 점검 후 발견.
- **Discovery 디테일**:
  - Picovoice 의 사전 빌드 lib 플랫폼: Android, iOS, Linux x86_64, macOS, Pi (Cortex-A), MCU (Cortex-M only, STM32F411 등). **Xtensa LX7 빌드 없음**.
  - GitHub issue #321 (ESP32 지원 요청, 2020) 5+년 미해결. 가까운 미래 가능성 낮음.
  - Picovoice library 는 closed-source binary blob → 자체 컴파일 불가.
  - Arduino SDK (`porcupine-arduino-en` 등) 도 Cortex-M binary 만 → ESP32 Arduino core 와 호환 X.
- **Rationale (Option B 한국어 wake 유지)**:
  - "넙죽 훈련병" 이 프로젝트 정체성 핵심. 영어로 가면 캐릭터 손실.
  - microWakeWord + Korean Piper TTS (커뮤니티 모델 `neurlang/piper-onnx-kss-korean`) + 자체 녹음 = 한국어 wake 학습 가능. 1~4주 R&D.
  - brain 도입은 Rhino 대체 + office-hours 의 "P4 가 핵심 contribution" framing 을 day 1 부터 시현 (오히려 강화).
  - INTERFACES.md 의 wake_engine_t / sti_engine_t 추상화가 정확히 이런 시나리오 대비. 잠금 가치 발휘 = 구현체만 swap, 다른 코드 영향 없음.
- **거부한 대안**:
  - **Option A (Bootstrap, 영어 wake 먼저)**: 시스템 검증 빠름. 거부 이유: 정체성 우선, 사용자 명시 결정.
  - **Option C (영어 wake 영구)**: R&D 부담 0. 거부 이유: 정체성 손실.
  - **Option D (Espressif WakeNet 한국어 custom 유료 의뢰)**: 비용 미상 + 20K 음성 샘플 (500+ 화자 + 어린이 100+) 제공 필요. 거부 이유: 비용 + 데이터 수집 부담.
  - **Option E (Picovoice 기다림)**: 5년 안 옴. 비추.
  - **Picovoice Foundation/Enterprise 라이선스 + Xtensa 빌드 의뢰**: 가격 + 일정 미상.
- **Architecture 영향**:
  - **PHASES.md P1.2** (wake_engine): porcupine → microwakeword.
  - **PHASES.md P1.3** (sti_engine): rhino → brain.
  - **PHASES.md P4** (brain 추가): brain 도입 자체는 P1 으로 이동. P4 는 polish/multi-turn/context 같은 advanced feature 슬롯으로 재정의 또는 폐지 후보.
  - **sti_dual fallback 의 의미 변화**: 원래 brain (cloud) + Rhino (local) 듀얼. Rhino 가 ESP32 에서 X 라 local fallback 정체 모호. 옵션:
    - (i) **local fallback = 없음**: brain 끊김 → reject. 단순.
    - (ii) **local fallback = 다수 microWakeWord 모델 (intent-as-wake-word)**: 앉아/서/굴러왼/... 각각 wake 학습. 정확도 낮지만 degraded mode 가능.
    - (iii) **local fallback = PCM 버퍼링 + 재연결 시 재생**: brain 재연결 후에 처리.
    - 결정 미정. P1 구현 중 진입 시점에 재방문.
- **Open questions / 후속 (mcu 세션 밖)**:
  - microWakeWord 한국어 학습 R&D (host Mac 또는 Colab GPU, root 세션 작업).
  - brain 모듈 (RPi 4 + Whisper 한국어 + intent classifier). root 세션 작업. brain/* 디렉토리 신규 생성.
  - real "넙죽 훈련병" 녹음 수집 (5~10 명 × 약 20 회). 사람 일.
  - sti_dual local fallback 의 정체 결정 (위 (i)/(ii)/(iii) 중 하나).
- **재검토 시점**:
  - microWakeWord 한국어 학습이 4주 안에 FAR < 1/시간 + FRR < 10% 미달 → Option A bootstrap (영어 wake 임시) 으로 fallback 검토.
  - Picovoice ESP32 SDK 출시 시 (5년 뒤라도) wake_engine_t 구현체 옵션 추가.
- **깨졌던 시나리오**: 발견 자체. plan 의 P1 (Porcupine + Rhino on ESP32) 가정이 6시간 짜리 코드 작성 + 1일 환경 셋업 후 깨짐. 사전 외부 SDK 호환성 검증의 가치 입증. **앞으로 외부 SDK 통합 plan 시 lib/ 또는 빌드 타겟 매트릭스 먼저 확인** 을 학습.
- **연관 문서**:
  - 이 결정으로 `docs/plans/mcu-p0-p1.md` SUPERSEDED.
  - 신규 plan: `docs/plans/mcu-p0-p1-v2.md`.
  - PHASES.md 상단 pivot 노트 추가.
