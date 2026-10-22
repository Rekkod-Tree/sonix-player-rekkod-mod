// Opt-in ordinary full-application caret regression; disposable roots only.
#include "src/gui/audio/eqsettings.h"
static lv_obj_t *caret_screen,*caret_parent,*caret_field;
static unsigned caret_checks;
static void caret_assert(lv_obj_t *field,bool shown,const char *text,int cursor) {
 assert(lv_obj_get_style_anim_duration(field,LV_PART_CURSOR)==(shown?900:0));
 assert((lv_anim_get(field,NULL)!=NULL)==shown);
 assert(!strcmp(lv_textarea_get_text(field),text));assert(lv_textarea_get_cursor_pos(field)==cursor);caret_checks++;
}
static lv_obj_t *caret_visible(lv_obj_t *root,const void *image,const char *label) {
 if(!root||!lv_obj_is_visible(root))return NULL;
 if((image&&lv_obj_check_type(root,&lv_image_class)&&lv_image_get_src(root)==image)||(label&&lv_obj_check_type(root,&lv_label_class)&&!strcmp(lv_label_get_text(root),label)))return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){lv_obj_t *p=caret_visible(lv_obj_get_child(root,(int)i),image,label);if(p)return p;}return NULL;
}
static void caret_click(lv_obj_t *object) {
 assert(object&&lv_obj_is_visible(object));lv_obj_update_layout(lv_screen_active());lv_area_t a;lv_obj_get_coords(object,&a);
 assert(a.x1>=0&&a.x2<480&&a.y1>=0&&a.y2<lv_display_get_vertical_resolution(lv_display_get_default()));
 input((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);input((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);
}
static void caret_park_states(lv_timer_t *timer) {
 static int step;static bool dark;static lv_obj_t *name_field;
 switch(step++){
 case 0:
  gui_init_rest();pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,read_pointer);
  caret_screen=lv_obj_create(NULL);caret_parent=lv_obj_create(caret_screen);caret_field=lv_textarea_create(caret_parent);lv_obj_add_state(caret_field,LV_STATE_FOCUSED);
  lv_textarea_set_text(caret_field,"Kept chapter 3");lv_textarea_set_cursor_pos(caret_field,5);keyboard_style_caret(caret_field);break;
 case 1:
  theme_set_dark(dark);lv_screen_load(caret_screen);caret_assert(caret_field,true,"Kept chapter 3",5);
  lv_obj_set_hidden(caret_parent,true);keyboard_refresh_caret(caret_field);caret_assert(caret_field,false,"Kept chapter 3",5);
  theme_set_accent(dark?2:1);caret_assert(caret_field,false,"Kept chapter 3",5);
  keyboard_style_caret(caret_field);caret_assert(caret_field,false,"Kept chapter 3",5);
  lv_screen_load(main_menu_screen);caret_assert(caret_field,false,"Kept chapter 3",5);lv_screen_load(caret_screen);keyboard_refresh_caret(caret_field);caret_assert(caret_field,false,"Kept chapter 3",5);
  lv_obj_set_hidden(caret_parent,false);keyboard_refresh_caret(caret_field);caret_assert(caret_field,true,"Kept chapter 3",5);
  lv_screen_load(main_menu_screen);caret_assert(caret_field,false,"Kept chapter 3",5);lv_screen_load(caret_screen);caret_assert(caret_field,true,"Kept chapter 3",5);
  playlistpage_open();break;
 case 2:
  name_field=find_field(playlistpage_screen);assert(name_field);assert(!lv_obj_is_visible(name_field));
  assert(lv_obj_get_style_anim_duration(name_field,LV_PART_CURSOR)==0&&!lv_anim_get(name_field,NULL));
  caret_click(caret_visible(playlistpage_screen,NULL,tr("playlist_new_playlist")));break;
 case 3:
  assert(lv_obj_is_visible(name_field));lv_textarea_set_text(name_field,"Keep name");lv_textarea_set_cursor_pos(name_field,4);
  caret_assert(name_field,true,"Keep name",4);caret_click(caret_visible(playlistpage_screen,&icon_close,NULL));break;
 case 4:
  assert(!lv_obj_is_visible(name_field));caret_assert(name_field,false,"Keep name",4);
  theme_set_dark(!dark);caret_assert(name_field,false,"Keep name",4);theme_set_dark(dark);caret_assert(name_field,false,"Keep name",4);
  switch_screen_no_history(main_menu_screen);switch_screen_no_history(playlistpage_screen);caret_assert(name_field,false,"Keep name",4);
  caret_click(caret_visible(playlistpage_screen,NULL,tr("playlist_new_playlist")));break;
 case 5:
  assert(lv_obj_is_visible(name_field));assert(lv_obj_get_style_anim_duration(name_field,LV_PART_CURSOR)==900&&lv_anim_get(name_field,NULL));
  lv_textarea_set_text(name_field,"New text");lv_textarea_set_cursor_pos(name_field,3);playlistpage_open();caret_assert(name_field,false,"New text",3);
  printf("UI_TEST_PASS caret visible900 hidden0 noanim ancestor/theme/restyle/reentry and real playlist name show/hide %s checks=%u\n",dark?"dark":"light",caret_checks);
  filespage_open();break;
 case 6:caret_click(caret_visible(filespage_screen,&icon_folder_new,NULL));break;
 case 7:{lv_obj_t *f=find_field(filespage_screen);assert(f&&lv_obj_is_visible(f));
  lv_textarea_set_text(f,"Keep folder");lv_textarea_set_cursor_pos(f,4);caret_assert(f,true,"Keep folder",4);
  caret_click(caret_visible(filespage_screen,&icon_close,NULL));switch_screen(eqsettings_screen());break;}
 case 8:caret_click(caret_visible(eqsettings_screen(),NULL,tr("save_preset")));break;
 case 9:{lv_obj_t *f=find_field(eqsettings_screen());assert(f&&lv_obj_is_visible(f));
  lv_textarea_set_text(f,"Keep preset");lv_textarea_set_cursor_pos(f,3);caret_assert(f,true,"Keep preset",3);
  caret_click(caret_visible(eqsettings_screen(),&icon_close,NULL));
  printf("UI_TEST_PASS narrow caret Files/EQ visible900 preserved %s checks=%u\n",dark?"dark":"light",caret_checks);
  if(!dark){dark=true;step=1;}else{lv_timer_delete(timer);fflush(stdout);exit(0);}break;}
 }
}
