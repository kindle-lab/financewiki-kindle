/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "snapshot.h"
#include "sqlite3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static const char *schema="create table snapshot(key text primary key,value text);insert into snapshot values('schema_version','4');"
 "create table documents(path text primary key,title text,hash text,category text,directory text,slug text,modified text,created text,body text,aliases text not null default '');";
static const char *rows="insert into documents(path,title,hash,category,directory,slug,body,aliases) values"
 "('index.md','FinanceWiki','x','root','','index','첫 화면',''),"
 "('wiki/index.md','위키','x','wiki','wiki','index','',''),"
 "('wiki/a.md','가나다','x','wiki','wiki','a','첫 본문 한글입니다.','별칭A\nAlias B'),"
 "('wiki/b.md','대상','y','wiki','wiki','b','두번째 본문',''),"
 "('wiki/concepts/rate.md','금리','z','wiki','wiki/concepts','rate','',''),"
 "('sources/rate.md','금리','z','sources','sources','rate','',''),"
 "('sources/dart/x.md','공시','z','sources','sources/dart','x','','');";
int main(void) {
 char path[]="/tmp/financewiki-snapshot-test-XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd); sqlite3 *db=NULL; assert(sqlite3_open(path,&db)==SQLITE_OK);
 assert(sqlite3_exec(db,schema,NULL,NULL,NULL)==SQLITE_OK && sqlite3_exec(db,rows,NULL,NULL,NULL)==SQLITE_OK); sqlite3_close(db);
 Snapshot s={0}; assert(snapshot_open(&s,path)==0&&s.count==7);
 /* path order: index.md, sources/dart/x.md, sources/rate.md, wiki/a.md, wiki/b.md, wiki/concepts/rate.md, wiki/index.md */
 assert(snapshot_offset_of(&s,"wiki/a.md")==3 && snapshot_offset_of(&s,"nope.md")==-1);
 SnapshotDoc d={0}; assert(snapshot_load(&s,3,&d)==0&&!strcmp(d.title,"가나다")&&!strcmp(d.directory,"wiki")); char p[600];
 assert(snapshot_page(d.body,0,p,sizeof(p))&&strstr(p,"한글")); assert(!snapshot_page(d.body,1,p,sizeof(p)));
 assert(snapshot_page_limited(d.body,0,9,p,sizeof(p))); assert(snapshot_page_limited(d.body,1,9,p,sizeof(p))); assert(!snapshot_page_limited(d.body,9,9,p,sizeof(p))); snapshot_doc_free(&d);
 /* the root document has directory "" and an empty-bodied index page loads */
 assert(snapshot_load(&s,0,&d)==0&&!strcmp(d.directory,"")); snapshot_doc_free(&d);
 assert(snapshot_load(&s,6,&d)==0&&!strcmp(d.body,"")); snapshot_doc_free(&d);
 /* folder tree: subfolders by name, then index page first, then titles */
 SnapshotList l={0}; assert(snapshot_folder(&s,"",&l)==0);
 assert(l.count==3&&l.items[0].folder&&!strcmp(l.items[0].label,"sources")&&!strcmp(l.items[1].label,"wiki")&&!l.items[2].folder&&l.items[2].offset==0); snapshot_list_free(&l);
 assert(snapshot_folder(&s,"wiki",&l)==0);
 assert(l.count==4&&l.items[0].folder&&!strcmp(l.items[0].path,"wiki/concepts")&&!strcmp(l.items[1].path,"wiki/index.md")&&!strcmp(l.items[2].label,"가나다")&&!strcmp(l.items[3].label,"대상")); snapshot_list_free(&l);
 assert(snapshot_folder(&s,"sources",&l)==0&&l.count==2&&!strcmp(l.items[0].path,"sources/dart")); snapshot_list_free(&l);
 /* link resolution, mirroring the mobile viewer */
 assert(snapshot_resolve(&s,"대상","wiki/a.md")==4);                 /* title */
 assert(snapshot_resolve(&s,"b","wiki/a.md")==4);                    /* slug */
 assert(snapshot_resolve(&s,"alias b","index.md")==3);               /* alias, case-insensitive */
 assert(snapshot_resolve(&s,"별칭A#제목","index.md")==3);              /* fragment ignored */
 assert(snapshot_resolve(&s,"/wiki/b.md","sources/rate.md")==4);     /* repository-absolute path */
 assert(snapshot_resolve(&s,"b.md","wiki/a.md")==4);                 /* relative path */
 assert(snapshot_resolve(&s,"../sources/rate","wiki/a.md")==2);      /* ".." and missing .md */
 assert(snapshot_resolve(&s,"rate","wiki/a.md")==2);                 /* no same-directory match: first path wins */
 assert(snapshot_resolve(&s,"금리","sources/x.md")==2);               /* duplicate title: same directory wins */
 assert(snapshot_resolve(&s,"금리","wiki/concepts/other.md")==5);
 assert(snapshot_resolve(&s,"없는 문서","index.md")==-1 && snapshot_resolve(&s,"#only","index.md")==-1);
 snapshot_close(&s); unlink(path); puts("snapshot: schema 4, load, pages, folder tree and mobile-compatible link resolution passed");
}
