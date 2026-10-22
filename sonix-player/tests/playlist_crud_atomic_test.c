// Actual production library body and ordinary SQL return-code failures.
#include "src/system/db/sqlite3.h"
static int checked_prepare(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
static int checked_int64(sqlite3_stmt *,int,sqlite3_int64);
static int checked_int(sqlite3_stmt *,int,int);
static int checked_finalize(sqlite3_stmt *);
#define sqlite3_bind_int64 checked_int64
#define sqlite3_bind_int checked_int
#define sqlite3_finalize checked_finalize
#define sqlite3_prepare_v2 checked_prepare
#define main previous_atomic_suite_main
#include "playlist_atomic_core_test.c"
#undef main
#undef sqlite3_prepare_v2
#undef sqlite3_bind_int64
#undef sqlite3_bind_int
#undef sqlite3_finalize
static bool fail_seconds,fail_present,fail_final;
static int checked_int64(sqlite3_stmt *s,int i,sqlite3_int64 v){if(fail_seconds){fail_seconds=false;return SQLITE_ERROR;}return sqlite3_bind_int64(s,i,v);}
static int checked_int(sqlite3_stmt *s,int i,int v){if(fail_present){fail_present=false;return SQLITE_ERROR;}return sqlite3_bind_int(s,i,v);}
static int checked_finalize(sqlite3_stmt *s){int rc=sqlite3_finalize(s);if(fail_final){fail_final=false;return SQLITE_IOERR;}return rc;}
static bool fail_insert_prepare;
static int checked_prepare(sqlite3 *d,const char *sql,int n,sqlite3_stmt **stmt,const char **tail) {
 if(fail_insert_prepare && strstr(sql,"INSERT INTO")) {fail_insert_prepare=false;*stmt=NULL;return SQLITE_ERROR;}
 return sqlite3_prepare_v2(d,sql,n,stmt,tail);
}
static bool operation(int op, const library_playlist_row_t *row) {
 switch(op) {
 case 0: return library_playlist_create("Created");
 case 1: return library_playlist_drop("Kept");
 case 2: return library_playlist_rename("Kept","Renamed");
 default: return library_playlist_append("Kept",row);
 }
}
static void setup(void) {
 open_db();
 library_playlist_row_t old={.present=true,.seconds=77};
 strcpy(old.path,"old");strcpy(old.title,"Old title");strcpy(old.artist,"Old artist");
 assert(library_playlist_create("Kept"));assert(library_playlist_append("Kept",&old));
 sql("UPDATE M3U_Kept SET mount=91");
}
static void done(void) { assert(sqlite3_get_autocommit(db));assert(sqlite3_close(db)==SQLITE_OK);db=NULL; }
int main(void) {
 assert(previous_atomic_suite_main()==0);
 library_playlist_row_t row={.present=true,.seconds=55};strcpy(row.path,"new");
 for(int op=0;op<4;op++) {
  setup();unsigned g=generation[GEN_PLAYLISTS];sql("BEGIN");
  assert(!operation(op,&row));assert(!sqlite3_get_autocommit(db));
  assert(generation[GEN_PLAYLISTS]==g);expect_old("Kept");
  assert(!library_playlist_exists("Created")&&!library_playlist_exists("Renamed"));
  sql("COMMIT");expect_old("Kept");done();
  setup();g=generation[GEN_PLAYLISTS];assert(operation(op,&row));
  assert(generation[GEN_PLAYLISTS]!=g);assert(sqlite3_get_autocommit(db));
  if(op==0) {assert(library_playlist_exists("Created"));expect_old("Kept");}
  if(op==1) assert(!library_playlist_exists("Kept"));
  if(op==2) {assert(!library_playlist_exists("Kept"));expect_old("Renamed");}
  if(op==3) assert(total("Kept")==2);
  done();
  for(int failure=0;failure<5;failure++) {
   setup();g=generation[GEN_PLAYLISTS];
   if(failure==0)fail_sql="SAVEPOINT playlist_crud";
   if(failure==1)fail_sql="RELEASE playlist_crud";
   if(failure==2)fail_bind=true;
   if(failure==3)fail_step=true;
   if(failure==4 && op==3)fail_insert_prepare=true;
   if(failure==4 && op!=3)fail_sql=op==0?"CREATE TABLE":op==1?"DROP TABLE":op==2?"ALTER TABLE":"INSERT INTO";
   assert(!operation(op,&row));fail_sql=NULL;fail_bind=fail_step=false;
   assert(generation[GEN_PLAYLISTS]==g);expect_old("Kept");
   assert(!library_playlist_exists("Created")&&!library_playlist_exists("Renamed"));done();
  }
 }
 for(int f=0;f<3;f++){setup();unsigned before=generation[GEN_PLAYLISTS];fail_seconds=f==0;fail_present=f==1;fail_final=f==2;assert(!library_playlist_append("Kept",&row));assert(generation[GEN_PLAYLISTS]==before);expect_old("Kept");done();}
 setup();unsigned g=generation[GEN_PLAYLISTS];assert(!library_playlist_create("kept"));
 assert(!library_playlist_rename("Kept","kept"));assert(generation[GEN_PLAYLISTS]==g);expect_old("Kept");
 assert(library_playlist_create("A\"B's"));assert(library_playlist_append("a\"b's",&row));assert(total("A\"B's")==1);
 assert(library_playlist_rename("a\"b's","Quoted\"X's"));assert(library_playlist_drop("quoted\"x's"));
 sql("BEGIN");assert(!library_playlist_rename("Kept","Kept"));assert(!sqlite3_get_autocommit(db));sql("ROLLBACK");
 assert(library_playlist_rename("Kept","Kept"));expect_old("Kept");done();
 puts("PASS direct CRUD owned short transactions, foreign TX, rollback, aliases and metadata/generations");return 0;
}
