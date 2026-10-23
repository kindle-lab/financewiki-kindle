# Phase 3 빌더 안내 — R1 문서로 대체

2026년 9월20일 R1에서 포함 범위·파서·링크·데이터 계약을 교정했다.
현재 구현과 명령은 [R1 Snapshot v2 안내](R1_SNAPSHOT_V2_KO.md)를 따른다.

이전 문서의 “인덱스와 inbox 제외”, “articles의 ideas/published만 포함”은 폐기한 정책이다.
현재는 데스크톱과 같은 `wiki/articles/sources/agenda/inbox` 아래 모든 안전한 `.md`를 읽는다.
기존 326개 v1 snapshot과 기기 배포 파일은 보존하며, 새 v2 332개 snapshot은 기기에 전송하지 않는다.

## 같은 설루션을 어떤 AI로도 다시 만드는 재현 프롬프트

R1_SNAPSHOT_V2_KO.md의 최신 계약과 재현 프롬프트를 읽어 FinanceWiki read-only exporter를 구현하라.
기존 원본·배포본을 바꾸지 말고 안전한 5개 문서 루트와 인덱스를 포함하라.
원문 경로/hash·metadata·링크를 desktop 실제 파서와 대조하고 schema 호환·출처·자동검증 결과를 기록하라.
이전 Phase 3의 제외 규칙을 되살리거나 v2를 현재 v1 reader에 전송하지 마라.
