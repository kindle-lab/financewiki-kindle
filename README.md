# FinanceWiki Kindle

탈옥한 Kindle Basic 11세대(2024, KT6)에서 FinanceWiki 1.x 문서를 읽기 전용으로 여는 KPM 패키지입니다. 편집, GitHub 쓰기, 그래프 기능은 없습니다.

- KPM 패키지 이름: `fw`
- 현재 버전: `1.1.0` (`kindlehf`)
- 원격 정본: `financewiki-park/financewiki-private`의 `main` 브랜치와 루트 `viewer-index.json` (모바일 뷰어와 같은 형식)

## 설치

KTerm에서 한 줄입니다. kindle-lab 허브가 이미 등록된 Kindle이라면 이것만 입력하면 됩니다.

```sh
/var/local/kmc/bin/kpm -y install fw
```

허브가 아직 등록되지 않은 Kindle은 처음 한 번만 아래 줄을 먼저 입력합니다.

```sh
/var/local/kmc/bin/kpm add-repo "https://raw.githubusercontent.com/kindle-lab/kpm-repo/main/manifest.json"
```

설치하면 서재에 두 항목이 생깁니다.

- **FinanceWiki**: 뷰어
- **FinanceWiki 업데이트**: 탭하면 새 버전이 있을 때만 설치합니다. 이후로는 KTerm을 열 필요가 없습니다.

## 토큰 설정

FinanceWiki 저장소가 private이라 **Contents: Read-only** 권한만 준 fine-grained PAT가 한 번 필요합니다. 토큰은 명령행이나 로그에 남기지 않고, 데이터 폴더의 `source.conf`(mode `0600`)에만 저장합니다. 셋 중 편한 방법을 쓰면 됩니다.

1. **USB로 넣기**: 토큰 한 줄만 적은 `FinanceWikiGitHubToken` 파일을 Kindle의 `documents` 폴더에 복사한 뒤 설치(또는 다시 설치)합니다. 설치가 끝나면 그 파일은 지워집니다.
2. **0.7.x를 쓰던 Kindle**: 기존 토큰(`/mnt/us/financewiki-kindle/config/github-token`)을 자동으로 옮깁니다.
3. **KTerm**: `/mnt/us/kmc/kpm/packages/fw/bin/financewiki-sync --configure`를 실행하고 토큰을 입력합니다.

데이터(문서 캐시, 설정, 토큰)는 `/var/local/financewiki-kindle`에 둡니다. 그곳에 쓸 수 없으면 `/mnt/us/financewiki-kindle`을 씁니다. 업데이트해도 데이터는 그대로 남습니다.

## 동작 방식

모바일 뷰어와 같은 원리로 움직이되, Kindle의 약한 네트워크에 맞췄습니다.

1. **처음 실행**: `viewer-index.json`에 있는 문서 전체를 받아 Kindle 안의 SQLite 캐시에 저장합니다. 받은 문서는 바로바로 저장하므로, 중간에 끊겨도 받은 문서는 남고 다음 실행에서 나머지만 이어 받습니다.
2. **이후 실행**: 브랜치 최신 커밋 값(40바이트)부터 확인합니다. 지난번과 같으면 아무것도 받지 않습니다. 다르면 그 커밋 기준으로 목록을 받아, 해시가 바뀐 문서와 새 문서만 받고 삭제된 문서는 캐시에서 지웁니다.
3. **오프라인**: Wi-Fi, DNS, GitHub, TLS 중 하나라도 안 되면 저장된 캐시로 엽니다.

받는 동안에는 진행 화면이 뜹니다. 단계, "120 / 645" 같은 개수와 막대, 지금 받는 문서 제목을 보여 줍니다.

## 화면

- 위: `FinanceWiki`, 쪽 표시(`3 / 12`), 폴더 위치와 글자 설정
- 아래 한 줄 버튼: **처음**(위키 첫 화면) · **뒤로** · **이전** · **다음** · **A-** · **A+** · **체**(명조/고딕) · **종료**
- 이전/다음은 그 문서 안에서만 움직입니다.
- 밑줄 친 줄은 다른 문서로 이어지는 링크입니다. 탭하면 열리고, **뒤로**로 돌아옵니다. 링크는 모바일 뷰어와 같은 규칙으로 찾습니다(상대 경로, 저장소 경로, 경로, slug, 제목, 별칭 순).
- 마크다운 처리: 문서 머리말(YAML) 숨김, 제목 크기 구분, 하위 목록 들여쓰기, 표는 "열 이름: 값" 줄로, 이미지는 `[그림: 설명]`, 코드 블록은 원문 그대로 보여 줍니다.
- 페이지는 화면에 실제로 들어가는 만큼 재서 나눕니다(FBInk 측정). 글이 넘쳐 앱이 꺼지지 않습니다.
- 글자: 42 / 46 / 50 / 54 / 58 / 62px, 기본 54px 명조. 설정은 데이터 폴더에 저장됩니다.
- 15분 동안 입력이 없거나 전원 버튼을 누르면 닫히고, Kindle 홈 화면을 다시 그립니다.

## 서체

0.7.0 뷰어와 같은 Noto Serif/Sans CJK KR을 씁니다. 0.7.0은 가변 폰트(VF)를 그대로 불러서 기본 굵기(명조 200, 고딕 100)로 그려졌고, 전자잉크에서 흐렸습니다. 1.1.0은 굵기를 고정한 파일을 씁니다.

- `fonts/NotoSerifCJKkr-SemiBold.ttf`: 본문 명조 (wght 600)
- `fonts/NotoSansCJKkr-Medium.ttf`: 본문 고딕과 화면 글자 (wght 500)
- `fonts/NotoSansCJKkr-Bold.otf`: 제목

굵기를 바꾸려면 `fonts/source/`에 VF 원본 두 개를 두고(체크섬은 `dependencies.json`) `python3 tools/make_fonts.py`를 실행합니다. VF 원본은 34MB라 저장소에 넣지 않았습니다.

## 빌드

Apple Silicon macOS 기준입니다. Zig 0.16.0을 체크섬 확인 후 `.cache/`에 받습니다.

```sh
sh scripts/build-macos.sh
sh tools/build-kpkg.sh
```

`build-macos.sh`는 ARMv7 hard-float static 바이너리 두 개(`financewiki`, `financewiki-sync`)를 만들고 호스트 테스트를 돌립니다. `build-kpkg.sh`는 `packages/fw/artifacts/`에 KPM 패키지를 만듭니다. KPM은 압축 최상위의 `manifest.json`만 읽으므로 파일을 폴더 안에 넣지 않습니다. 이미 배포한 버전 파일은 다시 만들지 않습니다.

## 테스트 범위

- 동기화: 처음 전체 받기, 끊긴 뒤 이어 받기, 커밋이 같을 때 생략, 바뀐 문서만 받기, 삭제 반영, 오프라인, 진행 출력
- 목록 해석: 공백 있는 JSON, 별칭, 빈 문서. 실제 `viewer-index.json` 645개 문서
- 마크다운: 머리말, 강조 기호 짝 맞추기, 단어 안 `_`, 부등호(`ROE<r`), 코드, 링크, 이미지, 하위 목록, 표, 인용, 구분선
- 페이지 나누기: 꽉 찬 페이지, 띄어쓰기 기준 분할, 한 페이지보다 긴 단어
- 링크: 실제 위키 링크 줄 2,780개 중 2,775개 해석 (나머지 5개는 대상 문서가 위키에 없음)
- 화면: 버튼 터치 영역, 화면 방향, 탭 판정
- 실행기: 정상 종료, 충돌, 신호, 점검 실패 시 Kindle UI·화면보호기 복구와 홈 다시 그리기

## 기록

- [docs/RELEASE_1_1_0_KO.md](docs/RELEASE_1_1_0_KO.md): 1.1.0에서 고친 문제
- [docs/history/](docs/history/): Phase 2~4 시험판과 1.0.0 후보 기록 (코덱스 작업)

## 라이선스

GPL-3.0-or-later. 포함한 서드파티 구성요소는 [THIRD_PARTY.md](THIRD_PARTY.md)와 `licenses/`를 보세요.
