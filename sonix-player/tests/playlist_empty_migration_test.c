
#define main previous_import_suite_main
#include "playlist_import_atomic_test.c"
#undef main
int main(void) {
 assert(previous_import_suite_main()==0);open_db();
 char dir[]="/tmp/sonix-empty-owned-XXXXXX";assert(mkdtemp(dir));char path[512];snprintf(path,sizeof(path),"%s/Empty.m3u",dir);
 FILE *f=fopen(path,"w");assert(f);fputs("#EXTM3U\n#[Sonix Playlist]\n",f);assert(!fclose(f));
 playlists_init(dir);unsigned g=generation[GEN_PLAYLISTS];
 assert(playlists_import(path,false,NULL)==0&&!library_playlist_exists("Empty"));assert(generation[GEN_PLAYLISTS]==g);
 fail_sql="RELEASE queue_stage_publish";assert(fixture_rows_from_file("Empty",path)==-1);assert(!library_playlist_exists("Empty")&&generation[GEN_PLAYLISTS]==g);
 assert(fixture_rows_from_file("Empty",path)==0&&library_playlist_exists("Empty")&&library_playlist_count("Empty")==0);
 library_playlist_stage_t *st=library_playlist_stage_begin_import(library_database_epoch(),generation[GEN_PLAYLISTS]);assert(st);
 assert(!library_playlist_stage_publish(st,"Empty",LIBRARY_PLAYLIST_REPLACE,NULL));library_playlist_stage_discard(st);
 st=library_playlist_stage_begin(library_database_epoch(),NULL);assert(st);assert(!library_playlist_stage_publish(st,"Queue empty",LIBRARY_PLAYLIST_CREATE,NULL));library_playlist_stage_discard(st);
 assert(fixture_rows_from_file("Empty",path)==-1&&library_playlist_exists("Empty")&&library_playlist_count("Empty")==0);
 assert(sqlite3_close(db)==SQLITE_OK);db=NULL;assert(!unlink(path)&&!rmdir(dir));
 puts("PASS valid empty migration preserved, manual empty import refused, failed publication returns error without target");return 0;
}
