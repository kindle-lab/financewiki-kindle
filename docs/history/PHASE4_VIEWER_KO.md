# Phase 4.2 — 실제 FinanceWiki 문서 viewer와 읽기 설정

상태: 구현·호스트 검증 완료, Kindle 실기 재시험 대기.

## 포함 기능

Phase 3 snapshot의 `financewiki.db`를 앱 패키지 `data/`에 넣고 SQLite를 읽기 전용으로 연다. 앱은 정렬된 첫 문서부터 실제 제목·category·본문을 표시한다.

- `‹`: 이전 본문 페이지 또는 이전 문서
- `›`: 다음 본문 페이지 또는 다음 문서
- `A−` / `A+`: 본문 크기를 22px 이상 42px 이하에서 조절
- `가`: 제공된 Noto Sans CJK KR 보통체/굵은체 전환
- `×`: Kindle 기본 UI로 복귀

본문은 Markdown 원문을 읽기용 평문으로 변환해 표시한다. 제목·목록·인용·강조·코드 표식·일반 링크·`[[위키 링크]]`를 정리하며, 링크 목적지가 아닌 화면용 이름을 남긴다. 현재 표는 읽기 쉬운 행 텍스트로 보이며 표 격자는 별도 확장 과제다. 글자 크기에 맞춰 UTF-8 경계를 보존한 페이지 길이를 다시 계산한다. 설정은 앱의 `state/settings.conf`에만 저장하고 snapshot은 수정하지 않는다.

## 안전 경계

- `SQLITE_OPEN_READONLY`로만 snapshot을 연다.
- 앱은 `data/financewiki.db`, FinanceWiki master, Kindle snapshot에 쓰지 않는다.
- snapshot DB는 manifest와 함께 패키지에 포함된다. atomic update/rollback은 Phase 8에서 별도로 구현한다.
- SQLite 3.50.2 amalgamation은 앱 바이너리에 정적 링크된다. Kindle 시스템 라이브러리·설정은 바꾸지 않는다.

## 검증

- C snapshot test: schema version, document load, UTF-8 페이지 경계
- Phase 3 Python test: builder source 불변, link/FTS/integrity/output 보호
- 실제 snapshot: 326개 문서, 1394개 link, SQLite integrity check `ok`
- ARM32 hard-float static ELF 검사

## Kindle 실기 시험

1. USB에서 기존 `financewiki-phase2/bin/financewiki`, `BUILD.json`, `data/financewiki.db`, `data/manifest.json`만 수정본으로 교체한다. 기존 snapshot이나 로그는 먼저 보관한다.
2. USB를 분리한 뒤 기본 홈의 `FinanceWiki Phase 2`를 실행한다.
3. 상단에 실제 category·문서 제목과 `1/326`이 보이는지 확인한다.
4. `‹`·`›`로 본문 페이지와 다음 문서가 바뀌는지 확인한다. `A−`·`A+`와 `가`를 눌러 표시가 바뀌고, 문서를 넘긴 뒤에도 설정이 유지되는지 확인한다.
5. `종료 / Exit`, 120초 종료, 기본 Kindle 홈 복귀를 확인한다.

## 같은 설루션을 어떤 AI로도 다시 만드는 재현 프롬프트

Phase 3의 FinanceWiki SQLite snapshot을 Kindle 네이티브 C/FBInk viewer에 읽기 전용으로 연결하라. 앱은 package 내부의 `data/financewiki.db`를 SQLite read-only 모드로 열고 schema version을 확인한 뒤 documents 테이블에서 실제 category·title·body를 표시해야 한다. Markdown 원문은 제목·목록·인용·강조·코드·일반 링크·위키 링크를 읽기 좋은 본문으로 변환하고, UTF-8 문자를 깨지지 않게 글자 크기에 맞춰 페이지로 나눠야 한다. 이전·다음, 글자 작게·크게, 보통체·굵은체, 종료의 터치 영역을 제공하고 설정은 runtime state에만 저장하라. 문서/페이지 전환은 e-ink 전체 갱신으로 처리하고, 기존 120초 종료·잠금·UI 복구와 물리 터치만 사용하는 규칙을 보존하라.

SQLite를 앱에 정적으로 링크하되 loadable extension과 쓰기 경로를 사용하지 마라. FinanceWiki master와 Kindle DB의 문서 내용을 수정하지 말고, package build 전에 manifest DB 해시를 확인하라. C unit test로 schema/document/UTF-8 page 경계를, Python test로 builder 불변성·FTS·SQLite integrity를 확인하고 ARM static ELF 검사를 수행하라. 실제 기기에서 표시와 터치가 확인되기 전에는 실기 통과라고 주장하지 마라. Markdown 서식, 내부 링크, 검색 UI, atomic update/rollback, HID 입력은 별도 단계로 남겨라.
