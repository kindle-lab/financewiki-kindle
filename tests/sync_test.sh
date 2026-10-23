#!/bin/sh
# Initial full sync, commit short-circuit, delta, resume after a dropped
# connection, deletion and offline fallback, all against tests/mock_curl.sh.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fx="$tmp/fixture"; data="$tmp/data"; log="$tmp/requests"
mkdir -p "$fx" "$data"
cat >"$data/source.conf" <<'CONF'
owner=financewiki-park
repository=financewiki-private
branch=main
token=github_pat_fixture
CONF
chmod 600 "$data/source.conf"
run() { : >"$log"; FINANCEWIKI_DATA_DIR="$data" FINANCEWIKI_CURL="$root/tests/mock_curl.sh" FW_SYNC_FIXTURE="$fx" FW_SYNC_DEBUG="$log" "$FW_SYNC_BINARY" "$@"; }
docs() { python3 - "$data/financewiki.db" "$@" <<'PY'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1]); q = sys.argv[2]
print(db.execute(q).fetchone()[0])
PY
}
# 25 documents (more than one 20-document batch), pretty-printed like the real index.
python3 - "$fx" <<'PY'
import json, sys, os
fx = sys.argv[1]; docs = []
for i in range(25):
    name = f"d{i:02d}.md"
    open(os.path.join(fx, name), "w").write(f"# 문서 {i}\n본문 {i}\n")
    docs.append({"id": f"wiki/{name}", "path": f"wiki/{name}", "title": f"문서 {i}", "hash": f"h{i}", "size": 20,
                 "category": "wiki", "directory": "wiki", "slug": f"d{i:02d}", **({"aliases": ["별칭A", "Alias B"]} if i == 3 else {})})
open(os.path.join(fx, "empty.md"), "w").write("")
docs.append({"path": "empty.md", "title": "빈 문서", "hash": "e", "category": "root", "directory": "", "slug": "empty"})
json.dump({"version": 1, "documents": docs}, open(os.path.join(fx, "index.json"), "w"), ensure_ascii=False, indent=2)
PY
printf 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' >"$fx/sha"

# 1. A dropped connection at document 23 keeps the 22 already fetched.
if (FW_SYNC_FAIL=d22.md; export FW_SYNC_FAIL; run --sync 2>/dev/null); then echo 'expected failure' >&2; exit 1; fi
[ "$(docs 'select count(*) from documents')" = 22 ]
[ "$(docs "select value from snapshot where key='complete'")" = 0 ]
# 2. Resume: only the missing 4 bodies are fetched, then the cache is complete.
run --sync 2>/dev/null
[ "$(grep -c '/contents/wiki/d\|/contents/empty' "$log")" = 4 ]
[ "$(docs 'select count(*) from documents')" = 26 ]
[ "$(docs "select value from snapshot where key='complete'")" = 1 ]
[ "$(docs "select value from snapshot where key='schema_version'")" = 4 ]
[ "$(docs "select aliases from documents where path='wiki/d03.md'")" = "별칭A
Alias B" ]
[ "$(docs "select length(body) from documents where path='empty.md'")" = 0 ]
grep -q 'ref=aaaaaaaa' "$log"
# 3. Same commit: one request, nothing else downloaded.
run --sync 2>/dev/null
[ "$(wc -l <"$log" | tr -d ' ')" = 1 ]
# 4. New commit with one changed and one deleted document: index + 1 body.
python3 - "$fx" <<'PY'
import json, sys, os
fx = sys.argv[1]; p = os.path.join(fx, "index.json"); d = json.load(open(p))
d["documents"] = [x for x in d["documents"] if x["path"] != "wiki/d24.md"]
for x in d["documents"]:
    if x["path"] == "wiki/d05.md": x["hash"] = "h5b"; x["title"] = "바뀐 문서"
json.dump(d, open(p, "w"), ensure_ascii=False, indent=2)
open(os.path.join(fx, "d05.md"), "w").write("# 바뀐 문서\n새 본문\n")
PY
printf 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' >"$fx/sha"
run --sync 2>/dev/null
[ "$(wc -l <"$log" | tr -d ' ')" = 3 ]
[ "$(docs 'select count(*) from documents')" = 25 ]
[ "$(docs "select title from documents where path='wiki/d05.md'")" = '바뀐 문서' ]
# 5. Progress lines for the reader's sync screen.
printf 'cccccccccccccccccccccccccccccccccccccccc' >"$fx/sha"
printf '# 바뀐 문서 둘\n' >"$fx/d06.md"
python3 - "$fx" <<'PY'
import json, sys, os
p = os.path.join(sys.argv[1], "index.json"); d = json.load(open(p))
for x in d["documents"]:
    if x["path"] == "wiki/d06.md": x["hash"] = "h6b"
json.dump(d, open(p, "w"), ensure_ascii=False, indent=2)
PY
out=$(FW_SYNC_PROGRESS=1 run --sync 2>/dev/null)
[ "$out" = "$(printf '@state checking\n@state index\n@plan 1 25\n@doc 1 1 문서 6\n@state done')" ] || { printf 'progress: %s\n' "$out" >&2; exit 1; }
[ "$(FW_SYNC_PROGRESS=1 run --sync 2>/dev/null)" = "$(printf '@state checking\n@state uptodate')" ]
[ "$(FW_SYNC_PROGRESS=1 FINANCEWIKI_DATA_DIR="$data" FINANCEWIKI_CURL=false "$FW_SYNC_BINARY" --auto 2>/dev/null)" = "$(printf '@state checking\n@state offline')" ]
# 6. Offline: --auto succeeds on the existing cache.
FINANCEWIKI_DATA_DIR="$data" FINANCEWIKI_CURL=false "$FW_SYNC_BINARY" --auto 2>/dev/null
FINANCEWIKI_DATA_DIR="$data" "$FW_SYNC_BINARY" --status | grep -q 'complete=1'
echo 'sync: initial, batch resume, commit short-circuit, delta, deletion and offline fallback passed'
