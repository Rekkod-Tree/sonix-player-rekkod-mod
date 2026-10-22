// Ordinary full-application import mini-player regression, disposable media only.
static lv_obj_t *im_visible(lv_obj_t *root, const void *image, const char *label) {
 if(!root || !lv_obj_is_visible(root)) return NULL;
 if((image && lv_obj_check_type(root,&lv_image_class) && lv_image_get_src(root)==image) ||
    (label && lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),label))) return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++) {lv_obj_t *p=im_visible(lv_obj_get_child(root,(int)i),image,label);if(p)return p;}
 return NULL;
}
static void im_click(lv_obj_t *object) {
 assert(object && lv_obj_is_visible(object));
 lv_obj_update_layout(playlistpage_screen);lv_obj_update_layout(lv_layer_top());
 lv_area_t a;lv_obj_get_coords(object,&a);
 int h=lv_display_get_vertical_resolution(lv_display_get_default());
 assert(a.x1>=0 && a.x2<480 && a.y1>=0 && a.y2<h);
 input((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);input((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);
}
static void im_capture(const char *state,bool dark) {char name[96];snprintf(name,sizeof(name),"import-mini-%s-%s",dark?"dark":"light",state);capture(name);}
static void import_mini_states(void) {
 static int step, waits;static bool dark;static char name[64];
 switch(step++) {
 case 0: gui_init_rest();pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,read_pointer);
  assert(library_scan_start(getenv("SONIX_SD_ROOT")));break;
 case 1: if(library_scan_running()){assert(++waits<40);step--;break;}player_play_file(getenv("SONIX_UI_TEST_TRACK"));break;
 case 2: if(!sx_mini_has_track()){assert(++waits<40);step--;break;}player_sheet_close(false);break;
 case 3: {
  theme_set_dark(dark);snprintf(name,sizeof(name),"Review38-%s",dark?"dark":"light");
  char path[1024];snprintf(path,sizeof(path),"%s/%s.m3u",getenv("SONIX_SD_ROOT"),name);
  FILE *f=fopen(path,"w");assert(f);fprintf(f,"#EXTM3U\n%s\n",getenv("SONIX_UI_TEST_TRACK"));assert(!fclose(f));
  playlistpage_open();break;}
 case 4: assert(sx_mini_has_track() && sx_mini_visible());im_capture("normal",dark);im_click(im_visible(playlistpage_screen,&icon_import,NULL));break;
 case 5: assert(!sx_mini_visible());assert(im_visible(playlistpage_screen,NULL,tr("playlist_import_playlists")));
  im_capture("choose",dark);im_click(im_visible(playlistpage_screen,&icon_close,NULL));break;
 case 6: assert(sx_mini_visible());im_click(im_visible(playlistpage_screen,&icon_import,NULL));break;
 case 7: assert(!sx_mini_visible());im_click(im_visible(playlistpage_screen,NULL,tr("playlist_import")));waits=0;break;
 case 8: if(!im_visible(playlistpage_screen,NULL,tr("playlist_import_finished"))){assert(++waits<40);step--;break;}
  assert(library_playlist_count(name)==1);assert(!sx_mini_visible());im_capture("report",dark);
  im_click(im_visible(playlistpage_screen,NULL,tr("ok")));break;
 case 9: assert(sx_mini_visible());im_capture("restored",dark);
  printf("UI_TEST_PASS import mini choose/close/reopen/import/report/close %s\n",dark?"dark":"light");
  if(!dark){dark=true;step=3;}else{fflush(stdout);exit(0);}break;
 }
}
