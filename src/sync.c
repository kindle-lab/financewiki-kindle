/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Kindle-side, cache-first synchronizer for the FinanceWiki viewer contract.
 * It deliberately uses the device's curl binary: KPM installations already
 * provide that HTTPS/TLS boundary, and no GitHub token is linked into the app.
 */
#define _POSIX_C_SOURCE 200809L
#include "index.h"
#include "sqlite3.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define SYNC_VERSION "1.1.0"
#define MAX_RESPONSE (4U * 1024U * 1024U)

typedef struct { char owner[128], repository[128], branch[128], token[512]; } Config;
typedef struct { char root[PATH_MAX], database[PATH_MAX], config[PATH_MAX], logs[PATH_MAX]; } Paths;

static void note(const char *format, ...) { va_list args; va_start(args, format); vfprintf(stderr, format, args); fputc('\n', stderr); va_end(args); }
static int join(char *out, size_t n, const char *a, const char *b) { int result = snprintf(out, n, "%s/%s", a, b); return result < 0 || (size_t)result >= n ? -1 : 0; }
static int mkdirs(const char *path, mode_t mode) {
    char copy[PATH_MAX]; if (strlen(path) >= sizeof(copy)) return -1; strcpy(copy, path);
    for (char *p = copy + 1; *p; ++p) if (*p == '/') { *p = 0; if (mkdir(copy, mode) && errno != EEXIST) return -1; *p = '/'; }
    return mkdir(copy, mode) && errno != EEXIST ? -1 : 0;
}
static int writable_directory(const char *root) { char probe[PATH_MAX]; if (mkdirs(root, 0700) || join(probe, sizeof(probe), root, ".probe")) return -1; int fd = open(probe, O_WRONLY | O_CREAT | O_EXCL, 0600); if (fd < 0) return -1; close(fd); return unlink(probe); }
static int paths(Paths *paths) {
    const char *root = getenv("FINANCEWIKI_DATA_DIR");
    if (!root || !*root) root = "/var/local/financewiki-kindle";
    if (writable_directory(root)) { root = "/mnt/us/financewiki-kindle"; if (writable_directory(root)) return -1; }
    if (strlen(root) >= sizeof(paths->root)) return -1; strcpy(paths->root, root);
    return join(paths->database, sizeof(paths->database), root, "financewiki.db") || join(paths->config, sizeof(paths->config), root, "source.conf") || join(paths->logs, sizeof(paths->logs), root, "sync.log") ? -1 : 0;
}
static int valid_value(const char *value, size_t maximum, bool token) {
    size_t n = strlen(value); if (!n || n >= maximum) return 0;
    for (size_t i = 0; i < n; ++i) { unsigned char c = (unsigned char)value[i]; if (c < 0x21 || c > 0x7e || c == '"' || c == '\\' || (!token && !(isalnum(c) || c == '.' || c == '_' || c == '-'))) return 0; }
    return 1;
}
static int config_read(const char *path, Config *config) {
    *config = (Config){0}; FILE *file = fopen(path, "r"); if (!file) return -1; char line[1024];
    while (fgets(line, sizeof(line), file)) { char *newline = strpbrk(line, "\r\n"); if (newline) *newline = 0; char *equals = strchr(line, '='); if (!equals) continue; *equals++ = 0;
        if (!strcmp(line, "owner")) snprintf(config->owner, sizeof(config->owner), "%s", equals);
        else if (!strcmp(line, "repository")) snprintf(config->repository, sizeof(config->repository), "%s", equals);
        else if (!strcmp(line, "branch")) snprintf(config->branch, sizeof(config->branch), "%s", equals);
        else if (!strcmp(line, "token")) snprintf(config->token, sizeof(config->token), "%s", equals);
    }
    fclose(file);
    return valid_value(config->owner, sizeof(config->owner), false) && valid_value(config->repository, sizeof(config->repository), false) && valid_value(config->branch, sizeof(config->branch), false) && valid_value(config->token, sizeof(config->token), true) ? 0 : -1;
}
static int write_all(int fd, const char *data, size_t size) { while (size) { ssize_t wrote = write(fd, data, size); if (wrote < 0) { if (errno == EINTR) continue; return -1; } data += wrote; size -= (size_t)wrote; } return 0; }
static int config_write(const char *path, const Config *config) {
    if (!valid_value(config->owner, sizeof(config->owner), false) || !valid_value(config->repository, sizeof(config->repository), false) || !valid_value(config->branch, sizeof(config->branch), false) || !valid_value(config->token, sizeof(config->token), true)) return -1;
    char temporary[PATH_MAX], text[1200]; if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) >= (int)sizeof(temporary)) return -1;
    int fd = mkstemp(temporary); if (fd < 0) return -1; fchmod(fd, 0600);
    int size = snprintf(text, sizeof(text), "owner=%s\nrepository=%s\nbranch=%s\ntoken=%s\n", config->owner, config->repository, config->branch, config->token);
    int ok = size > 0 && (size_t)size < sizeof(text) && !write_all(fd, text, (size_t)size) && !fsync(fd) && !close(fd) && !rename(temporary, path); if (!ok) { close(fd); unlink(temporary); return -1; } return 0;
}
static int percent_encode(const char *text, char *out, size_t capacity, bool slash) {
    static const char hex[] = "0123456789ABCDEF"; size_t used = 0;
    for (; *text; ++text) { unsigned char c = (unsigned char)*text; bool plain = isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || (slash && c == '/'); if (used + (plain ? 1 : 3) >= capacity) return -1; if (plain) out[used++] = (char)c; else { out[used++] = '%'; out[used++] = hex[c >> 4]; out[used++] = hex[c & 15]; } }
    out[used] = 0; return 0;
}
/* Writes a private curl config so the token never appears in argv or logs. */
static int curl_config(const Paths *paths, const Config *config, const char *accept, char *out, size_t capacity) {
    char template[PATH_MAX]; if (join(template, sizeof(template), paths->root, "curl.conf.XXXXXX")) return -1; int fd = mkstemp(template); if (fd < 0) return -1; fchmod(fd, 0600);
    char text[900]; int n = snprintf(text, sizeof(text), "header = \"Authorization: Bearer %s\"\nheader = \"Accept: %s\"\nuser-agent = \"FinanceWiki-Kindle/%s\"\n", config->token, accept, SYNC_VERSION);
    int ok = n > 0 && (size_t)n < sizeof(text) && !write_all(fd, text, (size_t)n) && !close(fd); if (!ok) { close(fd); unlink(template); return -1; } if (snprintf(out, capacity, "%s", template) >= (int)capacity) { unlink(template); return -1; } return 0;
}
static int run_curl(const char *configuration, const char *url, const char *output, int *http) {
    int pipefd[2]; if (pipe(pipefd)) return -1; pid_t pid = fork(); if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (!pid) { const char *curl = getenv("FINANCEWIKI_CURL"); if (!curl || !*curl) curl = "curl"; dup2(pipefd[1], STDOUT_FILENO); int nullfd = open("/dev/null", O_WRONLY); if (nullfd >= 0) dup2(nullfd, STDERR_FILENO); close(pipefd[0]); close(pipefd[1]); if (nullfd > STDERR_FILENO) close(nullfd); execlp(curl, curl, "--config", configuration, "--silent", "--show-error", "--connect-timeout", "8", "--max-time", "45", "--output", output, "--write-out", "%{http_code}", "--url", url, (char *)NULL); _exit(127); }
    close(pipefd[1]); char result[32] = {0}; ssize_t got = read(pipefd[0], result, sizeof(result) - 1); close(pipefd[0]); int status = 0; if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || got <= 0) return -1; *http = atoi(result); return *http >= 200 && *http < 300 ? 0 : -1;
}
/* GET url into a NUL-terminated buffer. Empty bodies are allowed only for documents. */
static int fetch_url(const Paths *paths, const Config *config, const char *accept, const char *url, bool allow_empty, char **data, size_t *size) {
    *data = NULL; *size = 0; char temporary[PATH_MAX], curl[PATH_MAX];
    if (join(temporary, sizeof(temporary), paths->root, "response.XXXXXX")) return -1;
    int fd = mkstemp(temporary); if (fd < 0) return -1; close(fd); if (curl_config(paths, config, accept, curl, sizeof(curl))) { unlink(temporary); return -1; } int http = 0; int result = run_curl(curl, url, temporary, &http); unlink(curl); if (result) { unlink(temporary); return -1; }
    FILE *file = fopen(temporary, "rb"); if (!file) { unlink(temporary); return -1; } if (fseek(file, 0, SEEK_END)) { fclose(file); unlink(temporary); return -1; } long length = ftell(file); if (length < 0 || (length == 0 && !allow_empty) || (unsigned long)length > MAX_RESPONSE || fseek(file, 0, SEEK_SET)) { fclose(file); unlink(temporary); return -1; }
    char *buffer = malloc((size_t)length + 1); int ok = buffer && (length == 0 || fread(buffer, 1, (size_t)length, file) == (size_t)length); fclose(file); unlink(temporary); if (!ok) { free(buffer); return -1; } buffer[length] = 0; *data = buffer; *size = (size_t)length; return 0;
}
/* Repository file at a fixed commit, so the index and bodies always match. */
static int fetch_file(const Paths *paths, const Config *config, const char *ref, const char *repository_path, bool allow_empty, char **data, size_t *size) {
    char path[PATH_MAX], encoded_ref[512], url[PATH_MAX * 2];
    if (percent_encode(repository_path, path, sizeof(path), true) || percent_encode(ref, encoded_ref, sizeof(encoded_ref), false) || snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/contents/%s?ref=%s", config->owner, config->repository, path, encoded_ref) >= (int)sizeof(url)) return -1;
    return fetch_url(paths, config, "application/vnd.github.raw", url, allow_empty, data, size);
}
/* One small request (40 bytes) tells whether anything changed since the last sync. */
static int head_commit(const Paths *paths, const Config *config, char sha[41]) {
    char branch[512], url[1024]; char *body = NULL; size_t size = 0;
    if (percent_encode(config->branch, branch, sizeof(branch), false) || snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/commits/%s", config->owner, config->repository, branch) >= (int)sizeof(url)) return -1;
    if (fetch_url(paths, config, "application/vnd.github.sha", url, false, &body, &size)) return -1;
    size_t n = 0; while (n < size && isxdigit((unsigned char)body[n])) n++;
    int ok = n == 40; if (ok) { memcpy(sha, body, 40); sha[40] = 0; } free(body); return ok ? 0 : -1;
}
static int has_column(sqlite3 *db, const char *table, const char *column) {
    char sql[128]; snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table); sqlite3_stmt *q = NULL; int found = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &q, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(q) == SQLITE_ROW) { const unsigned char *name = sqlite3_column_text(q, 1); if (name && !strcmp((const char *)name, column)) found = 1; }
    sqlite3_finalize(q); return found;
}
/* Schema 4 = schema 3 + aliases (used for wiki-link resolution, as in the mobile viewer). */
static int schema(sqlite3 *db) {
    const char *sql = "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS snapshot(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS documents(path TEXT PRIMARY KEY,title TEXT NOT NULL,hash TEXT NOT NULL,category TEXT NOT NULL,directory TEXT NOT NULL,slug TEXT,modified TEXT,created TEXT,body TEXT NOT NULL,aliases TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX IF NOT EXISTS documents_directory ON documents(directory,path);";
    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) return -1;
    int aliases = has_column(db, "documents", "aliases"); if (aliases < 0) return -1;
    if (!aliases && sqlite3_exec(db, "ALTER TABLE documents ADD COLUMN aliases TEXT NOT NULL DEFAULT '';", NULL, NULL, NULL) != SQLITE_OK) return -1;
    return sqlite3_exec(db, "INSERT OR REPLACE INTO snapshot(key,value) VALUES('schema_version','4');", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}
static int database_open(const char *path, sqlite3 **out) { *out = NULL; if (sqlite3_open_v2(path, out, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK) return -1; if (schema(*out)) { sqlite3_close(*out); *out = NULL; return -1; } return 0; }
static int document_same(sqlite3 *db, const FWIndexDocument *document, bool *same) { sqlite3_stmt *query = NULL; *same = false; if (sqlite3_prepare_v2(db, "SELECT hash FROM documents WHERE path=?1", -1, &query, NULL) != SQLITE_OK || sqlite3_bind_text(query, 1, document->path, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto done; if (sqlite3_step(query) == SQLITE_ROW) { const unsigned char *hash = sqlite3_column_text(query, 0); *same = hash && !strcmp((const char *)hash, document->hash); } sqlite3_finalize(query); return 0; done: sqlite3_finalize(query); return -1; }
static int bind_meta(sqlite3_stmt *q, const FWIndexDocument *d) {
    return sqlite3_bind_text(q, 1, d->path, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 2, d->title, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 3, d->hash, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 4, d->category, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 5, d->directory, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 6, d->slug ? d->slug : "", -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 7, d->modified ? d->modified : "", -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 8, d->created ? d->created : "", -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(q, 10, d->aliases ? d->aliases : "", -1, SQLITE_TRANSIENT) == SQLITE_OK;
}
static int document_put(sqlite3 *db, const FWIndexDocument *d, const char *body) {
    sqlite3_stmt *query = NULL; const char *sql = "INSERT INTO documents(path,title,hash,category,directory,slug,modified,created,body,aliases) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) ON CONFLICT(path) DO UPDATE SET title=excluded.title,hash=excluded.hash,category=excluded.category,directory=excluded.directory,slug=excluded.slug,modified=excluded.modified,created=excluded.created,body=excluded.body,aliases=excluded.aliases";
    if (sqlite3_prepare_v2(db, sql, -1, &query, NULL) != SQLITE_OK) return -1;
    int ok = bind_meta(query, d) && sqlite3_bind_text(query, 9, body, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(query) == SQLITE_DONE;
    sqlite3_finalize(query); return ok ? 0 : -1;
}
/* Unchanged body: refresh index metadata only (title, folder, aliases), no download. */
static int document_meta(sqlite3 *db, const FWIndexDocument *d) {
    sqlite3_stmt *query = NULL; const char *sql = "UPDATE documents SET title=?2,hash=?3,category=?4,directory=?5,slug=?6,modified=?7,created=?8,aliases=?10 WHERE path=?1";
    if (sqlite3_prepare_v2(db, sql, -1, &query, NULL) != SQLITE_OK) return -1;
    int ok = bind_meta(query, d) && sqlite3_step(query) == SQLITE_DONE;
    sqlite3_finalize(query); return ok ? 0 : -1;
}
static int remove_missing(sqlite3 *db, const FWIndex *index) {
    if (sqlite3_exec(db, "CREATE TEMP TABLE incoming(path TEXT PRIMARY KEY);", NULL, NULL, NULL) != SQLITE_OK) return -1; sqlite3_stmt *insert = NULL; if (sqlite3_prepare_v2(db, "INSERT INTO incoming(path) VALUES(?1)", -1, &insert, NULL) != SQLITE_OK) return -1;
    int ok = 1; for (size_t i = 0; i < index->count; ++i) { sqlite3_reset(insert); sqlite3_clear_bindings(insert); if (sqlite3_bind_text(insert, 1, index->documents[i].path, -1, SQLITE_TRANSIENT) != SQLITE_OK || sqlite3_step(insert) != SQLITE_DONE) { ok = 0; break; } } sqlite3_finalize(insert); if (!ok) return -1;
    return sqlite3_exec(db, "DELETE FROM documents WHERE path NOT IN (SELECT path FROM incoming); DROP TABLE incoming;", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}
static int set_meta(sqlite3 *db, const char *key, const char *value) { sqlite3_stmt *query = NULL; if (sqlite3_prepare_v2(db, "INSERT INTO snapshot(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &query, NULL) != SQLITE_OK) return -1; int ok = sqlite3_bind_text(query, 1, key, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_bind_text(query, 2, value, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(query) == SQLITE_DONE; sqlite3_finalize(query); return ok ? 0 : -1; }
static int get_meta(sqlite3 *db, const char *key, char *out, size_t capacity) {
    sqlite3_stmt *q = NULL; out[0] = 0; if (sqlite3_prepare_v2(db, "SELECT value FROM snapshot WHERE key=?1", -1, &q, NULL) != SQLITE_OK) return -1;
    if (sqlite3_bind_text(q, 1, key, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(q) == SQLITE_ROW) { const unsigned char *v = sqlite3_column_text(q, 0); if (v) snprintf(out, capacity, "%s", (const char *)v); }
    sqlite3_finalize(q); return 0;
}
static int document_count(sqlite3 *db) { sqlite3_stmt *q = NULL; int n = 0; if (sqlite3_prepare_v2(db, "SELECT count(*) FROM documents", -1, &q, NULL) == SQLITE_OK && sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0); sqlite3_finalize(q); return n; }
/* Progress for the reader's sync screen (launch.sh pipes stdout into
   "financewiki --progress" when FW_SYNC_PROGRESS=1). One line per event:
     @state checking|offline|uptodate|index|done|paused
     @plan <to fetch> <total documents>
     @doc <n> <to fetch> <title>                                        */
static void progress(const char *format, ...) {
    if (!getenv("FW_SYNC_PROGRESS")) return;
    va_list args; va_start(args, format); vfprintf(stdout, format, args); va_end(args); fputc('\n', stdout); fflush(stdout);
}
#define SYNC_BATCH 20
static int sync_cache(const Paths *paths, const Config *config) {
    char commit[41] = {0}, previous[64], complete[8], line[96];
    progress("@state checking");
    if (head_commit(paths, config, commit)) { progress("@state offline"); note("FinanceWiki sync: network unavailable; using cached documents."); return 1; }
    sqlite3 *db = NULL; if (database_open(paths->database, &db)) return -1;
    get_meta(db, "commit", previous, sizeof(previous)); get_meta(db, "complete", complete, sizeof(complete));
    if (!strcmp(previous, commit) && !strcmp(complete, "1") && document_count(db) > 0) { sqlite3_close(db); progress("@state uptodate"); note("FinanceWiki sync: up to date at %.12s.", commit); return 0; }
    progress("@state index");
    char *raw = NULL; size_t raw_size = 0;
    if (fetch_file(paths, config, commit, "viewer-index.json", false, &raw, &raw_size)) { sqlite3_close(db); progress("@state offline"); note("FinanceWiki sync: viewer index unavailable; using cached documents."); return 1; }
    FWIndex index = {0}; char error[160] = {0}; if (fw_index_parse(raw, raw_size, &index, error, sizeof(error))) { free(raw); sqlite3_close(db); note("FinanceWiki sync: invalid viewer index (%s)", error[0] ? error : "parse error"); return -1; } free(raw);
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) { fw_index_free(&index); sqlite3_close(db); return -1; }
    int changed = 0, pending = 0, result = 0, planned = 0;
    for (size_t i = 0; i < index.count; ++i) { bool same = false; if (!document_same(db, &index.documents[i], &same) && !same) planned++; }
    progress("@plan %d %zu", planned, index.count);
    for (size_t i = 0; i < index.count; ++i) {
        bool same = false; if (document_same(db, &index.documents[i], &same)) { result = -1; break; }
        if (same) { if (document_meta(db, &index.documents[i])) { result = -1; break; } continue; }
        char *body = NULL; size_t body_size = 0;
        progress("@doc %d %d %s", changed + 1, planned, index.documents[i].title);
        if (fetch_file(paths, config, commit, index.documents[i].path, true, &body, &body_size) || document_put(db, &index.documents[i], body)) { free(body); result = -1; note("FinanceWiki sync: stopped at %s", index.documents[i].path); break; }
        free(body); changed++;
        /* Save progress in small batches: a dropped connection keeps what was already fetched. */
        if (++pending >= SYNC_BATCH) {
            if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK || sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) { result = -1; break; }
            pending = 0; snprintf(line, sizeof(line), "FinanceWiki sync %zu/%zu", i + 1, index.count); note("%s", line);
        }
    }
    char source[512], stamp[32]; snprintf(source, sizeof(source), "%s/%s@%s", config->owner, config->repository, config->branch); snprintf(stamp, sizeof(stamp), "%lld", (long long)time(NULL));
    if (!result) {
        if (remove_missing(db, &index) || set_meta(db, "source", source) || set_meta(db, "commit", commit) || set_meta(db, "complete", "1") || set_meta(db, "last_sync_epoch", stamp) || sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) result = -1;
    }
    if (result) {
        /* Keep committed batches and mark the cache incomplete; the next launch resumes. */
        if (set_meta(db, "complete", "0") || sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        int have = document_count(db); size_t total = index.count; sqlite3_close(db); fw_index_free(&index);
        note("FinanceWiki sync: incomplete (%d/%zu documents cached); the next launch resumes.", have, total);
        progress("@state paused"); (void)line; return -1;
    }
    sqlite3_close(db); fw_index_free(&index);
    note("FinanceWiki sync: %d changed document(s) applied at %.12s.", changed, commit); progress("@state done"); return 0;
}
static int cached(const Paths *paths) { sqlite3 *db = NULL; sqlite3_stmt *query = NULL; int available = 0; if (sqlite3_open_v2(paths->database, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK) goto done; if (sqlite3_prepare_v2(db, "SELECT count(*) FROM documents", -1, &query, NULL) == SQLITE_OK && sqlite3_step(query) == SQLITE_ROW) available = sqlite3_column_int(query, 0) > 0; done: sqlite3_finalize(query); if (db) sqlite3_close(db); return available; }
static int configure(const Paths *paths) {
    Config config = {0}; snprintf(config.owner, sizeof(config.owner), "financewiki-park"); snprintf(config.repository, sizeof(config.repository), "financewiki-private"); snprintf(config.branch, sizeof(config.branch), "main"); char input[600];
    fprintf(stderr, "GitHub fine-grained PAT (Contents: Read-only; input is not echoed): "); fflush(stderr); system("stty -echo 2>/dev/null"); if (!fgets(input, sizeof(input), stdin)) { system("stty echo 2>/dev/null"); return -1; } system("stty echo 2>/dev/null"); fputc('\n', stderr); input[strcspn(input, "\r\n")] = 0; snprintf(config.token, sizeof(config.token), "%s", input);
    if (config_write(paths->config, &config)) { note("FinanceWiki setup: invalid token or cannot save configuration."); return -1; } note("FinanceWiki setup: configuration saved with mode 0600."); return 0;
}
static void status_meta(const Paths *paths) {
    sqlite3 *db = NULL; char commit[64], complete[8];
    if (sqlite3_open_v2(paths->database, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK) { if (db) sqlite3_close(db); return; }
    get_meta(db, "commit", commit, sizeof(commit)); get_meta(db, "complete", complete, sizeof(complete));
    printf("documents=%d\ncommit=%s\ncomplete=%s\n", document_count(db), commit[0] ? commit : "none", complete[0] ? complete : "0"); sqlite3_close(db);
}
static void usage(void) { fputs("usage: financewiki-sync {--auto|--sync|--configure|--status|--version}\n", stderr); }
int main(int argc, char **argv) {
    Paths path = {0}; if (paths(&path)) { note("FinanceWiki sync: persistent storage unavailable."); return 1; } const char *command = argc == 2 ? argv[1] : "--auto";
    if (!strcmp(command, "--version")) { puts(SYNC_VERSION); return 0; }
    if (!strcmp(command, "--configure")) return configure(&path) ? 1 : 0;
    if (!strcmp(command, "--status")) { Config c; if (config_read(path.config, &c)) puts("configured=no"); else printf("configured=yes\nsource=%s/%s@%s\n", c.owner, c.repository, c.branch); printf("cached=%s\ndata_dir=%s\n", cached(&path) ? "yes" : "no", path.root); status_meta(&path); return 0; }
    if (strcmp(command, "--auto") && strcmp(command, "--sync")) { usage(); return 2; }
    Config config; if (config_read(path.config, &config)) { note("FinanceWiki sync: source is not configured; using cached documents."); return cached(&path) || !strcmp(command, "--auto") ? 0 : 1; }
    int result = sync_cache(&path, &config); if (result == 1 && cached(&path)) return 0; return result ? 1 : 0;
}
