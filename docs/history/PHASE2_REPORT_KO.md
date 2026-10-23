# Phase 2 — 최소 실행 앱 결과

2026년 9월20일. 승인 범위: 독립 최소 앱 구현·PC 검증·기기 시험용 패키지. 모델 추천은 앞선 승인안의 `gpt-6-astra / high`를 유지했으며, 모델 변경 도구를 실행하지 않았습니다.

## 결과

후속 실기 시험에서 0.0.1의 잘못된 `rotation=0` guard로 즉시 종료됨을 확인했다. 0.0.2 수정 내용과 재시험 조건은 [회전 검사 수정 기록](PHASE2_ROTATION_FIX_KO.md)을 우선 참고한다. 0.0.2는 사용자 실기 확인으로 표시·한글·갱신·종료 복귀·120초 종료를 통과했고, Phase 2는 완료다. 아래 최초 결과는 초기 제작 시점의 기록이다.

C + FBInk 기반 ARM 실행 파일과 USB 복사형 패키지를 제작했습니다. **개발/호스트 검증 완료, 기기 시험 대기**입니다. Kindle가 마운트되어 있지 않고 연결 주소도 제공되지 않아 실기 실행은 하지 않았습니다.

동작 화면은 제목·버전·한글 regular/bold·한영 혼합·숫자·기호·긴 문단·고정 표, 부분 갱신·전체 갱신·종료 버튼입니다. 실제 FinanceWiki 문서나 DB는 아직 포함하지 않습니다.

## 확인된 내용

- ARM32 little-endian EABI5/hard-float, 정적 실행 파일. 동적 interpreter 없음.
- Linux ARM input_event 크기 16바이트를 빌드 시 확인.
- 두 글꼴에 한글 완성형 11172자의 glyph와 UI 문자열 glyph 존재. ‘한’의 실제 bitmap 생성 성공.
- 터치 판정: 좌표 범위, 축 변환, 버튼 경계, 움직인 손가락·다중 접촉·유실 입력 처리 테스트 통과.
- 프로세스: 정상/실패, crash, timeout, SIGTERM 무시 시 SIGKILL 승격, 종료 신호, screensaver 통지 테스트 통과.
- 런처: mock 서비스 환경에서 정상/비정상/신호 종료와 probe/Pillow 실패 시 원상복구 테스트 통과.
- 앱 자체 C 코드의 ARM `-Wall -Wextra -Werror` 컴파일 검사 통과.

## 초기 권장안과 달라진 점

Linux용 koxtoolchain 환경이 없어서 Mac용 Zig 0.16.0 C 컴파일러를 사용했습니다. 앱 언어는 C이며 Zig 언어 런타임을 쓰지 않습니다. musl 정적 링크로 Kindle 공유 라이브러리 의존성을 줄였습니다. 공식 archive 체크섬을 확인했고 compiler/cache는 작업 영역에만 있습니다. Linux kindlehf toolchain 비교 빌드는 미실행입니다.

FBInk에서 확인한 대상 식별은 KT6 / Basic 5 / Rossini / Bellatrix(MTK)입니다. 이번 버전은 해당 기기와 portrait만 허용합니다. FBInk의 실제 소스에서 non-Kobo 진단 출력의 printf 인자 1개 누락을 발견해 vendored copy에 수정하고 기록했습니다.

## 수명주기와 한계

런처는 기존 Awesome PID가 실행 중인지 확인한 뒤 자신이 변경한 UI 상태만 복구합니다. 프로세스 이름 전체에 killall을 사용하지 않습니다. 프로토타입은 물리 터치 하나만 점유하고 Bluetooth/가상 장치를 제외합니다.

별도 감독 프로세스가 실제 viewer를 실행하고 정상/비정상 종료 뒤 진입 전 화면을 복구합니다. 120초가 지나거나 잠금 이벤트가 오면 viewer를 끝냅니다. 잠금 시에는 이전 화면을 덮어 그리지 않습니다. viewer를 강제 종료해도 남은 프로세스가 처리할 수 있도록 했고, 런처가 사라지면 감독 프로세스가 기록된 소유권으로 UI 복구를 시도합니다.

이 동작은 실제 Kindle에서 아직 확인되지 않았습니다. Linux PR_SET_PDEATHSIG 기반 런처 상실 복구, e-ink ioctl, 전체 화면 줄바꿈, 터치 회전값, powerd 이벤트·Pillow/statusbar 호환성은 실기 시험 항목입니다. 커널 정지나 전원 상실을 소프트웨어 복구 성공으로 간주하지 않습니다.

부팅 훅·system 파일·KPM 등록·Wi-Fi/인증서·HID daemon 설정 변경은 없습니다. 앱을 삭제하면 사용자 파일 영역의 해당 폴더와 scriptlet이 제거되는 구성입니다.

## 다음 작업

`docs/DEVICE_TEST_KO.md`의 실기 체크를 진행하고 화면 사진과 로그로 한글·터치·갱신·기본 UI 복귀를 확인합니다. 실기 통과 전까지 기술 스택 최종 채택과 Phase 2 완료를 선언하지 않습니다.

Phase 3 data builder는 아직 시작하지 않았습니다. 실기 결과를 반영한 뒤 별도 단계 계획·모델 추천·승인을 거칩니다.
