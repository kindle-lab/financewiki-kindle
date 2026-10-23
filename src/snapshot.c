/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "snapshot.h"
#include "sqlite3.h"
#include <ctype.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *copy_text(const unsigned char *text) { size_t n=text?strlen((const char *)text)+1:0; char *r=n?malloc(n):NULL; if(r) memcpy(r,text,n); return r; }
static char *copy_n(const char *text,size_t n) { char *r=malloc(n+1); if(r) { memcpy(r,text,n); r[n]=0; } return r; }
int snapshot_open(Snapshot *s,const char *path) {
    *s=(Snapshot){0}; sqlite3_stmt *q=NULL;
    if(sqlite3_open_v2(path,&s->db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,NULL)!=SQLITE_OK) goto fail;
    if(sqlite3_prepare_v2(s->db,"SELECT value FROM snapshot WHERE key='schema_version'",-1,&q,NULL)!=SQLITE_OK || sqlite3_step(q)!=SQLITE_ROW || strcmp((const char *)sqlite3_column_text(q,0),"4")) goto fail_q;
    sqlite3_finalize(q); q=NULL;
    if(sqlite3_prepare_v2(s->db,"SELECT count(*) FROM documents",-1,&q,NULL)!=SQLITE_OK || sqlite3_step(q)!=SQLITE_ROW || (s->count=sqlite3_column_int(q,0))<0) goto fail_q;
    sqlite3_finalize(q); return 0;
fail_q: sqlite3_finalize(q);
fail: snapshot_close(s); return -1;
}
void snapshot_close(Snapshot *s) { if(s->db) sqlite3_close(s->db); *s=(Snapshot){0}; }
int snapshot_load(Snapshot *s,int offset,SnapshotDoc *d) {
    *d=(SnapshotDoc){0}; sqlite3_stmt *q=NULL;
    if(offset<0||offset>=s->count) return -1;
    if(sqlite3_prepare_v2(s->db,"SELECT path,title,body,category,directory FROM documents ORDER BY path LIMIT 1 OFFSET ?1",-1,&q,NULL)!=SQLITE_OK || sqlite3_bind_int(q,1,offset)!=SQLITE_OK || sqlite3_step(q)!=SQLITE_ROW) goto fail;
    d->id=copy_text(sqlite3_column_text(q,0)); d->title=copy_text(sqlite3_column_text(q,1)); d->body=copy_text(sqlite3_column_text(q,2)); d->category=copy_text(sqlite3_column_text(q,3)); d->directory=copy_text(sqlite3_column_text(q,4));
    /* Empty bodies and the root directory ("") are valid. */
    if(!d->body) d->body=copy_n("",0); if(!d->directory) d->directory=copy_n("",0);
    sqlite3_finalize(q);
    if(d->id&&d->title&&d->body&&d->category&&d->directory) return 0; snapshot_doc_free(d); return -1;
fail: sqlite3_finalize(q); snapshot_doc_free(d); return -1;
}
void snapshot_doc_free(SnapshotDoc *d) { free(d->id); free(d->title); free(d->body); free(d->category); free(d->directory); *d=(SnapshotDoc){0}; }
int snapshot_offset_of(Snapshot *s,const char *path) {
    sqlite3_stmt *q=NULL; int offset=-1;
    if(sqlite3_prepare_v2(s->db,"SELECT (SELECT count(*) FROM documents b WHERE b.path<a.path) FROM documents a WHERE a.path=?1",-1,&q,NULL)!=SQLITE_OK) return -1;
    if(sqlite3_bind_text(q,1,path,-1,SQLITE_TRANSIENT)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW) offset=sqlite3_column_int(q,0);
    sqlite3_finalize(q); return offset;
}
static int push(SnapshotList *l,bool folder,int offset,char *label,char *path) {
    if(!label||!path) { free(label); free(path); return -1; }
    SnapshotEntry *grown=realloc(l->items,(size_t)(l->count+1)*sizeof(*grown)); if(!grown) { free(label); free(path); return -1; }
    l->items=grown; l->items[l->count++]=(SnapshotEntry){folder,offset,label,path}; return 0;
}
void snapshot_list_free(SnapshotList *l) { for(int i=0;i<l->count;i++) { free(l->items[i].label); free(l->items[i].path); } free(l->items); *l=(SnapshotList){0}; }
static int by_label(const void *a,const void *b) { return strcmp(((const SnapshotEntry *)a)->label,((const SnapshotEntry *)b)->label); }
int snapshot_folder(Snapshot *s,const char *dir,SnapshotList *out) {
    *out=(SnapshotList){0}; sqlite3_stmt *q=NULL; size_t dn=strlen(dir); SnapshotList folders={0};
    /* Subfolders: the next path segment of every deeper directory. */
    if(sqlite3_prepare_v2(s->db,"SELECT DISTINCT directory FROM documents",-1,&q,NULL)!=SQLITE_OK) return -1;
    while(sqlite3_step(q)==SQLITE_ROW) {
        const char *d=(const char *)sqlite3_column_text(q,0); if(!d||!*d) continue;
        const char *rest;
        if(!dn) rest=d; else if(!strncmp(d,dir,dn)&&d[dn]=='/') rest=d+dn+1; else continue;
        size_t seg=strcspn(rest,"/"); if(!seg) continue;
        bool seen=false; for(int i=0;i<folders.count;i++) if(strlen(folders.items[i].label)==seg&&!strncmp(folders.items[i].label,rest,seg)) seen=true;
        if(seen) continue;
        size_t pn=(size_t)(rest-d)+seg;
        if(push(&folders,true,-1,copy_n(rest,seg),copy_n(d,pn))) { sqlite3_finalize(q); snapshot_list_free(&folders); return -1; }
    }
    sqlite3_finalize(q); q=NULL;
    if(folders.count) qsort(folders.items,(size_t)folders.count,sizeof(SnapshotEntry),by_label);
    *out=folders;
    const char *sql="SELECT (SELECT count(*) FROM documents b WHERE b.path<a.path),title,path FROM documents a WHERE directory=?1 "
        "ORDER BY CASE WHEN lower(path) IN ('index.md','_index.md') OR lower(path) LIKE '%/index.md' OR lower(path) LIKE '%/_index.md' OR path LIKE '%인덱스.md' THEN 0 ELSE 1 END,title,path";
    if(sqlite3_prepare_v2(s->db,sql,-1,&q,NULL)!=SQLITE_OK || sqlite3_bind_text(q,1,dir,-1,SQLITE_TRANSIENT)!=SQLITE_OK) { sqlite3_finalize(q); snapshot_list_free(out); return -1; }
    while(sqlite3_step(q)==SQLITE_ROW)
        if(push(out,false,sqlite3_column_int(q,0),copy_text(sqlite3_column_text(q,1)),copy_text(sqlite3_column_text(q,2)))) { sqlite3_finalize(q); snapshot_list_free(out); return -1; }
    sqlite3_finalize(q); return 0;
}
/* normalizeLookup: trim, "\" -> "/", drop "./" and ".md", lower-case (ASCII; Hangul has no case). */
static void lookup_key(const char *in,char *out,size_t cap) {
    while(*in==' '||*in=='\t') in++;
    size_t n=strlen(in); while(n&&(in[n-1]==' '||in[n-1]=='\t')) n--;
    if(n>=2&&in[0]=='.'&&(in[1]=='/'||in[1]=='\\')) { in+=2; n-=2; }
    if(n>=3&&in[n-3]=='.'&&tolower((unsigned char)in[n-2])=='m'&&tolower((unsigned char)in[n-1])=='d') n-=3;
    size_t j=0; for(size_t i=0;i<n&&j+1<cap;i++) { char c=in[i]=='\\'?'/':in[i]; out[j++]=(char)tolower((unsigned char)c); } out[j]=0;
}
static int hex(int c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
/* normalizeRepositoryPath: decode %XX, resolve "." and ".." against the current directory. */
static bool repository_path(const char *current,const char *target,char *out,size_t cap) {
    char decoded[1024]; size_t j=0;
    for(size_t i=0;target[i]&&j+1<sizeof(decoded);i++) {
        if(target[i]=='%'&&hex(target[i+1])>=0&&hex(target[i+2])>=0) { decoded[j++]=(char)(hex(target[i+1])*16+hex(target[i+2])); i+=2; }
        else decoded[j++]=target[i]=='\\'?'/':target[i];
    }
    decoded[j]=0;
    char parts[64][256]; int n=0;
    if(decoded[0]!='/') {
        const char *slash=strrchr(current,'/');
        if(slash) { char base[1024]; size_t bl=(size_t)(slash-current); if(bl>=sizeof(base)) return false; memcpy(base,current,bl); base[bl]=0;
            for(char *t=strtok(base,"/");t&&n<64;t=strtok(NULL,"/")) snprintf(parts[n++],sizeof(parts[0]),"%s",t); }
    }
    for(char *t=strtok(decoded,"/");t;t=strtok(NULL,"/")) {
        if(!strcmp(t,".")) continue;
        if(!strcmp(t,"..")) { if(!n) return false; n--; continue; }
        if(n>=64) return false; snprintf(parts[n++],sizeof(parts[0]),"%s",t);
    }
    if(!n) return false; size_t used=0; out[0]=0;
    for(int i=0;i<n;i++) { int w=snprintf(out+used,cap-used,"%s%s",i?"/":"",parts[i]); if(w<0||(size_t)w>=cap-used) return false; used+=(size_t)w; }
    return true;
}
static int lower_path_offset(Snapshot *s,const char *path) {
    sqlite3_stmt *q=NULL; int offset=-1;
    if(sqlite3_prepare_v2(s->db,"SELECT (SELECT count(*) FROM documents b WHERE b.path<a.path) FROM documents a WHERE lower(a.path)=lower(?1) ORDER BY a.path LIMIT 1",-1,&q,NULL)!=SQLITE_OK) return -1;
    if(sqlite3_bind_text(q,1,path,-1,SQLITE_TRANSIENT)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW) offset=sqlite3_column_int(q,0);
    sqlite3_finalize(q); return offset;
}
int snapshot_resolve(Snapshot *s,const char *raw,const char *current) {
    char target[1024]; while(*raw==' '||*raw=='\t') raw++; snprintf(target,sizeof(target),"%s",raw);
    char *hash=strchr(target,'#'); if(hash) *hash=0;
    size_t n=strlen(target); while(n&&(target[n-1]==' '||target[n-1]=='\t')) target[--n]=0;
    if(!n) return -1; /* "#heading" points at the current document */
    bool path_like=strchr(target,'/')||strchr(target,'\\')||(n>=3&&!strcasecmp(target+n-3,".md"));
    if(path_like) {
        char path[1024];
        if(repository_path(current?current:"",target,path,sizeof(path))) {
            int offset=lower_path_offset(s,path); if(offset>=0) return offset;
            size_t pl=strlen(path);
            if(!(pl>=3&&!strcasecmp(path+pl-3,".md"))&&pl+3<sizeof(path)) { strcat(path,".md"); offset=lower_path_offset(s,path); if(offset>=0) return offset; }
        }
    }
    char key[1024]; lookup_key(target,key,sizeof(key)); if(!*key) return -1;
    char dir[1024]=""; if(current) { const char *slash=strrchr(current,'/'); if(slash&&(size_t)(slash-current)<sizeof(dir)) { memcpy(dir,current,(size_t)(slash-current)); dir[slash-current]=0; } }
    const char *sql="SELECT (SELECT count(*) FROM documents b WHERE b.path<a.path) FROM documents a WHERE "
        "lower(a.path)=?1||'.md' OR lower(a.path)=?1 OR lower(a.slug)=?1 OR lower(trim(a.title))=?1 "
        "OR instr(char(10)||lower(a.aliases)||char(10),char(10)||?1||char(10))>0 "
        "ORDER BY CASE WHEN a.directory=?2 THEN 0 ELSE 1 END,a.path LIMIT 1";
    sqlite3_stmt *q=NULL; int offset=-1;
    if(sqlite3_prepare_v2(s->db,sql,-1,&q,NULL)!=SQLITE_OK) return -1;
    if(sqlite3_bind_text(q,1,key,-1,SQLITE_TRANSIENT)==SQLITE_OK && sqlite3_bind_text(q,2,dir,-1,SQLITE_TRANSIENT)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW) offset=sqlite3_column_int(q,0);
    sqlite3_finalize(q); return offset;
}
static size_t boundary(const char *s,size_t n) { while(n&&((unsigned char)s[n]&0xc0)==0x80) n--; return n; }
bool snapshot_page_limited(const char *body,unsigned page,size_t page_bytes,char *out,size_t cap) {
    if(!body||page_bytes==0||cap<5) return false; size_t len=strlen(body),start=(size_t)page*page_bytes;
    if(start>=len) return false; start=boundary(body,start); while(start<len&&(body[start]=='\n'||body[start]==' '||body[start]=='\r')) start++; if(start>=len) return false;
    size_t end=start+page_bytes; if(end>len) end=len; else end=boundary(body,end); if(end<=start) return false; size_t n=end-start;
    if(n>=cap-4) n=boundary(body,start+cap-4)-start; memcpy(out,body+start,n); out[n]=0; if(end<len) strcat(out,"…"); return true;
}
bool snapshot_page(const char *body,unsigned page,char *out,size_t cap) { return snapshot_page_limited(body,page,560,out,cap); }
