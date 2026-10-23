# FinanceWiki Kindle 1.0.0 KPM 배포 기록

기준일: 2026년 10월24일

## 배포 후보

- Package ID: `com.financewiki.kindle`
- KPM 표시 버전: `1.0.0`
- 실행 바이너리 버전: `1.0.0-kpm`
- 대상: `kindlehf` (Basic 5 / KT6 실기 관측값 기반)
- 산출물: `dist/financewiki-kindle_1.0.0-kpm_kindlehf.kpkg`
- 원격 데이터: `financewiki-park/financewiki-private@main`

## 확정된 동작

- GitHub의 `viewer-index.json` 1.x 계약을 읽는다.
- 최초 성공 시 인덱스의 모든 Markdown 문서를 캐시한다.
- 이후에는 문서 hash 비교로 새 문서와 변경 문서만 내려받고, 삭제된 경로는 제거한다.
- 네트워크 실패 또는 응답 오류 때는 마지막 완료 SQLite 트랜잭션을 유지한 채 캐시를 연다.
- 그래프, 편집, commit/push는 포함하지 않는다.

## KPM 허브 등록 게이트

`kindle-lab/kpm-repo`의 `manifest.json`에는 검증되지 않은 artifact 경로를 먼저 추가하지 않는다. 다음 실기기 검증 후 동일 바이트와 SHA-256을 등록한다.

1. KPM `add-repo`, `install`, `uninstall`이 `kindlehf`에서 정상 동작한다.
2. `curl --version`이 HTTPS를 지원하고, private repo read-only PAT로 최초 전체 수집을 완료한다.
3. 문서를 GitHub에서 한 건 수정한 뒤, 다음 실행이 해당 문서만 갱신한다.
4. Wi-Fi를 끈 뒤 캐시로 다시 열고, 기존 문서가 보인다.
5. launcher 종료 뒤 Kindle 홈 화면이 정상 복구된다.

이 게이트를 통과하지 않은 artifact는 KPM hub의 immutable version/path로 게시하지 않는다.
