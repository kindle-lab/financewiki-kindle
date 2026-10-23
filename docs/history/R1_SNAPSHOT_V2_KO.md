# R1 — 실제 FinanceWiki 데이터 정합성 복구

2026년 9월20일. 추천 모델 GPT-6 Astra · high. PC 구현·검증 완료, 기기 전송 없음.

## 결과

- 기존 데이터 326개에서 빠졌던 인덱스 6개를 복구해 **332개**를 포함했다.
- 실제 source와 상대경로·원문 SHA-256 불일치 0개.
- 데스크톱 v0.2.59의 **실제 Rust 모듈을 컴파일한 oracle**과 제목·원본 ID·별칭·태그·링크 전체 대조: 불일치 0개.
- 별칭 189개, 태그 162개. 링크 발생 1702건: resolved 1698, missing 4, ambiguous 0.
- `교보생명*` FTS 18개, `aliases:교보생명*` 3개, `tags:금융*` 5개. 검색 UI/한국어 키보드의 구현 증거는 아니다.
- Python 테스트 18개 통과(빌더 15개 + 실제 Rust oracle 3개), C reader v1 허용/v2 거부 검증 통과.
- source 기준 커밋 `e2e29c3e1dcb59d93cd40c9c93625aaff8ff9f4c`; `main`, clean. 원격 fetch를 하지 않았으므로 GitHub 최신 여부는 미확인.
- desktop 소스 기준 `78c7a00bd837c80b8d134f2a4c628018ae547eef`. 설치 앱 바이너리의 정확한 빌드 커밋을 증명한 것은 아니다.

기존 기기용 v1 DB와 실행 파일은 보존했다. 이 단계에서는 reader·화면·버튼·폰트·패키징·전송 코드를 바꾸지 않았다.

## 포함 정책

`wiki/`, `articles/`, `sources/`, `agenda/`, `inbox/` 아래의 일반 `.md` 파일 전부가 대상이다.
인덱스도 포함한다. `articles/drafts`·`working` 등에 문서가 생겨도 포함된다. 현재 agenda/inbox는 0개다.
다른 최상위 루트와 원자료 바이너리는 대상이 아니다. 허용 루트 내의 제외 항목은 이유를 manifest에 남긴다.
심볼릭 링크·reparse 항목을 따라가지 않는다. 경로 NFC 충돌, 잘못된 UTF-8, 빌드 중 문서 집합/내용/Git 상태 변경은 실패 처리한다.

## v2 계약

| 저장 대상 | 계약 |
|---|---|
| `documents` | NFC 상대경로 SHA-256 앞 32자에서 파생한 snapshot ID, 원래 경로와 NFC 키, root/parent_path, 원본 ID(nullable), 제목/type/status, 본문, 전체 원문, metadata, 원문 크기/hash |
| `aliases`, `tags` | 순서·값·NFC 소문자 검색 키. tags의 앞 `#`는 desktop과 같이 제거. 원래 표기는 전체 원문/metadata에 보존 |
| `links` | source/발생 순서, raw target, target name, 표시명, anchor, 원문 Unicode 문자 위치 `[start,end)`, target(nullable), resolution, match kind, 사유, 후보 snapshot ID 배열 |
| `documents_fts` | FTS5 unicode61, document_id UNINDEXED + title/body/aliases/tags |
| `snapshot` | manifest의 파일 목록 제외 주요 값. `value`는 JSON 값이며 숫자 schema_version은 `2` |
| `manifest.json` | schema, 시간대 포함 생성 시각, source/builder commit·dirty·상태 digest, builder 파일 hash, 문서 집합 digest, 호환 정보, 포함 정책, 제외 항목, 파일 크기/hash |
| `audit.json` | 전체 문서 경로·hash·제목·ID·별칭·태그, 분류별 개수, 검증 결과, 미해결 링크와 후보·사유 |

원문 `markdown`은 UTF-8 BOM·CRLF까지 보존해 재인코딩한 바이트 hash가 실제 파일과 같아야 한다.
`body`는 읽기/검색용으로 frontmatter를 분리한 값이다. `metadata_json`은 desktop 호환의 작은 frontmatter 문법이며 **완전한 YAML 파서가 아니다**. 복잡한 metadata도 전체 원문에는 남는다.

제목은 frontmatter → 첫 H1 → 파일명. 별칭·태그는 scalar/따옴표 포함 inline list/block list를 지원하고 순서를 유지하며 중복 제거한다.
링크는 정확한 제목 → 별칭 → 파일명 순이며 첫 일치 단계의 후보가 둘 이상이면 ambiguous다.
NFC 후 문자별 소문자화를 사용한다. `casefold`로 `ß`와 `ss`를 합치지 않는다.
desktop과 마찬가지로 전체 원문에서 위키링크를 찾으므로 frontmatter/코드 안의 링크도 현재 색인에 포함된다.
빈 대상/문서명 없는 anchor는 desktop이 무시하므로 `ignored_wikilinks`에 기록한다.
`.md` 제거, 경로/ID 해석, anchor-only 자기문서 이동을 임의로 추가하지 않았다.

`modified_value`는 `updated_at` 또는 `updated`의 원문 값이고 `modified_provenance`는 해당 frontmatter 키다.
날짜로 해석·검증한 값이나 Git 수정일이라고 주장하지 않는다. 해당 값이 없으면 NULL이며 export 시각을 대입하지 않는다.

문서 digest는 NFC 경로와 파일 hash의 정렬된 JSON tuple 배열 SHA-256이다. 탭/개행이 있는 경로에도 모호하지 않다.
v1의 digest/ID와 동일하다고 가정하면 안 된다. ID는 파일명 변경에 독립적인 영구 ID가 아니다.

## 호환성과 남은 범위

schema v2와 현재 v1 reader는 호환되지 않는다. `PRAGMA user_version=2`, snapshot schema=2로 표시하고,
manifest의 `requires_snapshot_schema=2`, `deployment_ready=false`, `verified_reader_versions=[]`를 사용한다.
아직 지원 reader가 없으므로 `minimum_reader_version=null`이다. 가짜 최소 버전을 정하지 않았다.
현재 reader가 v2를 거부하는지 호스트에서 검증했으며, 기기에 전송하지 않았다.

render blocks·assets·state DB는 R2 이후 구현 대상이며 manifest의 미구현 기능 목록에 명시했다.
원문과 검색용 본문을 함께 보관해 약 10MB가 되는 것은 무손실 감사와 재파싱을 위한 현재 선택이다.
기기 RAM·읽기 성능은 이번 단계에서 실측하지 않았다.

미해결 4건은 원문에 존재하는 두 대상 이름이다. 임의로 비슷한 문서에 연결하거나 원본을 고치지 않았다.

- `외국인 순매수 전환···거래일엔 12조 순매도`: 1건.
- `토큰증권 조각투자 넘어 본류로`: 3건.

## 사용

Python 3.10 이상 + SQLite FTS5 + Git, macOS/Linux가 필요하다. Python 추가 패키지는 없다.
descriptor-relative `O_NOFOLLOW` 읽기 때문에 Windows 실행은 아직 지원/검증하지 않았다.

```sh
python3 tools/build_snapshot.py \
  --source /path/to/FinanceWiki \
  --output /path/outside/FinanceWiki/new-snapshot
```

기존 출력은 덮어쓰지 않는다. output은 source 밖의 **새 경로**여야 한다.
임시 형제 디렉터리에서 검사 후 rename한다. 동시 exporter/외부 프로세스가 같은 output 경로를 만들지 않게 한다.
source를 잠그지 않으므로 최종 재검사 직후의 외부 수정까지 막는 트랜잭션은 아니다. 정지된 checkout에서 export할 것을 권장한다.
이 PC 산출물 publish를 Kindle/MTP의 원자적 업데이트·rollback 구현으로 해석하지 않는다.

동일 source/builder 상태와 `--generated-at`을 지정하면 DB 재현성 테스트를 할 수 있다.
builder는 이 Git 소스 저장소에서 실행한다. commit뿐 아니라 dirty 상태와 script hash도 확인한다.

## 실제 desktop 코드 대조

`tests/desktop_oracle.rs`는 컴파일 시 지정한 desktop checkout의 `document_model.rs`와 `link_resolver.rs`를 직접 include한다.
desktop 소스를 수정하거나 앱을 다시 빌드하지 않는다. 이미 빌드된 호환 Rust dependency를 사용했다.
다음 경로 변수는 실행 환경에 맞게 지정한다. `RUSTC`와 `.rlib`는 같은 toolchain/target이어야 한다.

```sh
FINANCEWIKI_DESKTOP_SOURCE=/path/to/financewiki-app \
  "$RUSTC" --edition=2021 tests/desktop_oracle.rs \
  -L "dependency=$DESKTOP_DEPS" \
  --extern "unicode_normalization=$NORMALIZATION_RLIB" \
  --extern "serde_json=$SERDE_JSON_RLIB" -o build/desktop-oracle

FINANCEWIKI_DESKTOP_ORACLE=build/desktop-oracle \
  python3 -m unittest tests/test_snapshot.py tests/test_desktop_parity.py -v

python3 tools/audit_snapshot.py \
  --source /path/to/FinanceWiki --snapshot /path/to/new-snapshot \
  --oracle build/desktop-oracle --report /path/outside/FinanceWiki/new-parity.json
```

oracle가 없으면 빌더 15개는 실행할 수 있지만 desktop 대조 테스트 3개는 **skip**된다. 이를 통과로 보고하지 않는다.
독립 auditor는 builder의 파서·scanner를 import하지 않고 원본 파일 집합/바이트를 다시 읽어 비교한다.
실제 GUI 검색 순위나 사용 중인 cache DB와의 byte-level 일치 검증은 아니다.

기존 reader 호환성 시험:

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc -Ivendor/sqlite \
  -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_FTS5 \
  tests/snapshot_v2_guard_test.c src/snapshot.c vendor/sqlite/sqlite3.c \
  -lm -o build/snapshot-v2-guard
build/snapshot-v2-guard /path/to/v1/financewiki.db /path/to/v2/financewiki.db
```

## 다음 승인 대상 — R2

추천 **GPT-6 Astra · xhigh**. 실제 표/Markdown 구조, 폰트 측정 기반 줄바꿈·페이지, 하단 4×2 동일 크기 버튼을 구현한다.
권장 C/FBInk 기반 유지 + PC 구조화 전처리. 대안 외부 C renderer 도입은 의존성·한글·표 지원 실측이 필요하다.
`src/markdown.*`, 신규 `src/render.*`, `src/core.*`, `src/main.c`, builder의 render blocks 및 fixture 변경이 예상된다.
검은 fill 인자 순서 오류, 표 잘림, 페이지 누락/중복, 글꼴 변경 후 위치 보존이 주요 위험이다.
같은 renderer의 실제 자료 미리보기와 자동 테스트를 먼저 제시하고, 기기 전송은 미리보기 확인 후 진행한다.
규모는 큼. R3 탐색·R5 검색 UI를 이번 승인에 묶지 않는다. R2 실행 승인 대기.

## 같은 설루션을 어떤 AI로도 다시 만드는 재현 프롬프트

GitHub main을 지식 정본으로 유지하는 FinanceWiki Kindle의 R1 read-only exporter를 구현하라.
사용자 승인 단계 하나만 수행하고, source/desktop/Kindle 배포 파일을 수정하지 마라.
별도 앱 저장소의 Python 표준 라이브러리로 5개 허용 루트의 모든 .md와 인덱스를 읽어 schema-v2 SQLite/FTS5·manifest·audit을 새 외부 경로에 생성하라.
NFC 경로, raw UTF-8 bytes, 원본 ID, 제목, 전체 부모 경로, frontmatter, aliases/tags, 원문 위키링크와 해석 사유·후보를 보존하라.
desktop Rust 파서/해석 모듈을 직접 컴파일한 oracle로 의미를 대조하고 source path/hash set과 FTS 무결성을 독립 검사하라.
symlink·invalid UTF-8·경로 충돌·변경된 source는 실패 처리하고, 실패 시 자기 임시 출력만 정리하라. 기존 snapshot을 덮어쓰지 마라.
source/builder commit·dirty·script hash·문서 digest·호환 범위를 기록하되 비밀값·기기별 절대경로·credentials는 배포물에 넣지 마라.
원본 Git 쓰기·fetch·force push·hard reset·기기 전송·원자료/Inbox 변경·외부 업로드를 하지 마라.
자동 테스트·실데이터 대조·현재 reader의 v2 거부 검증을 따로 보고하고, UI·render blocks·assets·state·원자적 기기 갱신·실기 성능은 미구현/미검증으로 명시하라.
문서·코드·테스트·검증 보고서를 남기고 다음 R2 렌더러/UI의 추천 모델·추론·선택지·파일·위험·검증을 먼저 제시한 뒤 승인을 기다려라.
