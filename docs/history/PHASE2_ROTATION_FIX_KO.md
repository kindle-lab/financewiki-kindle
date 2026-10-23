# Phase 2 0.0.2 — 초기 화면 검사 수정

2026년 9월20일. Phase 2 범위의 실기 실패 수정이며 Phase 3는 시작하지 않았다.

## 확인된 원인

0.0.1의 실제 session.log와 previous.log 모두 다음을 기록하고 종료했다.

```text
device=Basic 5 code=Rossini platform=Bellatrix
size=1072x1448 bpp=8 rotation=3
Only Basic 2024 (Basic 5/KT6), native portrait supported in this prototype.
```

코드는 native `rotation=0`만 허용했다. 화면을 그리거나 Kindle UI를 일시 정지하기 전의 probe 단계에서 종료된 것이다.

## 수정

- `Basic 5 / MTK / 1072×1448 / 8bpp / native rotation=3`이라는 관측된 조합만 허용한다. 다른 기종·화면 모드의 거부는 유지한다.
- native rotation은 픽셀을 추가로 회전하라는 뜻이 아니다. vendored FBInk의 `initialize_fbink`는 Kindle의 화면 크기를 framebuffer xres/yres에서 가져오고 기본 좌표 변환을 no-op으로 둔다. `FBInkState.current_rota`도 device rotation과 buffer 방향을 구분한다.
- 터치 축 범위의 대소관계로 추가 축 교환을 추정하던 코드를 제거하고, 정규화 후 명시적인 FBInk touch flags만 적용한다. [KOReader Kindle 구현](https://github.com/koreader/koreader/blob/master/frontend/device/kindle/device.lua)의 Basic 5도 별도의 축 교환/미러링 hook을 추가하지 않는다. 이 교차 확인은 실기 터치 성공을 대신하지 않는다.
- probe 통과·터치 변환·최초 화면 출력 완료/실패 로그를 추가했다.
- 버전은 `0.0.2-phase2`로 올리고 패키지 manifest 버전을 소스에서 읽도록 변경했다.
- 실제 rotation=3 허용, 다른 모드 거부, 좌표 네 모서리 및 세 버튼 판정 회귀 테스트를 추가했다.

## 실기 재시험

호스트 `make test`, 런처 mock 테스트 5종, ARM 빌드와 ELF 검사, main.c ARM 경고 오류 검사(`-Wall -Wextra -Werror`)를 통과했다. 실제 기기의 기존 바이너리·manifest·실행 로그를 Mac에 백업한 뒤 `bin/financewiki`와 `BUILD.json`만 교체했고, 기기에서 재읽어 로컬 수정본과 바이트 단위 일치를 확인했다. 다른 앱 파일과 설정은 변경하지 않았다.

설치 바이너리: 420784바이트, SHA-256 `185becf73f7f16849ab5c46e368bb39ed5c48e9ba500a225d726b5970576dadc`.

호스트 테스트와 배포 파일 검증은 실제 화면·터치 동작 확인과 별개다. USB 분리 후 홈의 같은 `FinanceWiki Phase 2`를 실행한다. 화면의 버전이 `0.0.2-phase2`인지, 부분/전체 갱신과 종료 버튼이 반응하는지 확인한다. 실패하면 새 로그로 다음 실패 지점을 판단한다.

이 수정은 회전 설정 ioctl, 시스템 파일, 부팅 서비스, KPM/KOReader/HID 설정을 변경하지 않는다. 기존 120초 감독 종료 및 UI 복구 경로를 유지한다.
