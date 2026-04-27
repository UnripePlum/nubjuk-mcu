# nubjuk-mcu — Claude Code 작업 규칙 (mcu 세션 전용)

## 모듈 책임 (한 줄)
ESP-IDF v5.x C 프로젝트. ESP32-S3에서 마이크 캡처 + wake word + STI + FSM + motion 실행 + viewer WS 서버.

상세 아키텍처는 `ARCHITECTURE.md` 참고.

---

## 🚧 격리 규칙 (cwd = mcu/)

| 허용 | 금지 |
|------|------|
| `mcu/**` (이 모듈 전체 read/write) | `viewer/**`, `brain/**` (다른 모듈 영역) |
| `docs/**` (read; protocol/*.md는 잠금) | `schemas/**` (잠금) |
| | `README.md`, root `CLAUDE.md` — 통합 결정 문서 |

다른 모듈의 동작이 궁금하면 **`docs/protocol/*.md`** 만 참고. 다른 모듈 코드 읽지 말 것.

---

## 🔒 잠금 정책

다음 파일의 시그니처·struct 멤버·enum 값·페이로드 구조는 사용자 명시 승인 없이 변경 불가.

### 모듈 내부 인터페이스 (잠금)
- `mcu/main/wake/wake_engine.h` — `wake_engine_t` (event-only)
- `mcu/main/sti/sti_engine.h` — `sti_engine_t` (session lifecycle)
- `mcu/main/coord/voice_coordinator.h` — wake → sti 결합 모듈
- `mcu/main/motion/motion_controller.h` — `motion_controller_t` (callbacks + 내부 watchdog)

→ **정확한 시그니처는 `INTERFACES.md`**.

### 통신 계약 (잠금, 외부)
- `docs/protocol/mcu-viewer.md` — viewer와의 WS 메시지 계약
- `docs/protocol/mcu-brain.md` — Phase 4 brain과의 WS STI 서비스 계약

### Threading contract (잠금)
- 콜백은 `voice_task` / `motion_task` 컨텍스트, **ISR 금지**
- 콜백 내부에서 **블로킹 호출 금지**
- `set_callbacks()`는 init 직후 1회만, 이후 immutable
- 콜백 reentrancy 금지
- 한 인터페이스의 모든 메서드는 단일 task에서만 호출 (wake/sti는 voice_task, motion은 motion_task)
- 콜백 payload (intent strings 등) 라이프타임: 콜백 함수 return 전까지만 유효, 필요하면 복사할 것

### Task 우선순위 정책 (잠금)
- 시스템 task(wifi/timer) > audio > voice > motion > ws
- "highest" 사용 금지 — 시스템 task를 굶기면 안 됨
- 정확한 priority 표는 `PHASES.md` §1.8

---

## 작업 원칙

1. **인터페이스 시그니처를 바꿔야 한다고 느끼면 STOP** — 사용자에게 "이 변경을 해도 될까요"를 먼저 물어볼 것
2. **새 기능은 새 구현체로** — 기존 인터페이스를 만족하는 새 클래스/팩토리를 추가
3. **Phase 진행은 `PHASES.md` 따름** — 임의로 phase 순서 바꾸지 말 것
4. **모든 통신 메시지는 schema 검증 통과 필수** (`docs/protocol/`)
5. **테스트 가능성 우선**: motion_controller·sti_engine은 mock/log 구현이 항상 동시에 존재
6. **워킹 디렉토리 밖은 손대지 말 것** (위 격리 규칙)

### 격리가 깨지는 신호 (즉시 STOP, 사용자 확인)

- 인터페이스 시그니처 (`*.h`)를 바꿔야 할 것 같은 감각
- protocol 파일 내용 변경 필요
- `schemas/` 변경 필요
- task priority 표 변경 필요
- ARCHITECTURE.md의 핵심 결정 (audio fan-out 정책, motion 격리 등) 변경 필요

---

## 문서 인덱스

| 파일 | 내용 |
|------|------|
| `CLAUDE.md` (이 파일) | 작업 규칙 + 잠금 정책 |
| `ARCHITECTURE.md` | 컴포넌트 다이어그램, 데이터 흐름, task 토폴로지, 메모리 배치 |
| `INTERFACES.md` | 잠금된 인터페이스 시그니처 (source of truth) |
| `PHASES.md` | Phase 0~6 구현 task + Gate 기준 |
| `docs/protocol/mcu-viewer.md` | ESP↔viewer WS 메시지 계약 (잠금) |
| `docs/protocol/mcu-brain.md` | Phase 4 ESP↔brain WS 계약 (잠금) |
