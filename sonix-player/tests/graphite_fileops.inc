// Real files-page controls and workers on the disposable host card only.
#include "src/system/device/system.h"
static char fileops_fixture[768];
static lv_obj_t *fileops_label(lv_obj_t *root, const char *text) {
	if (!visible(root)) return NULL;
	if (lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text)) return root;
	for(uint32_t i=0;i<lv_obj_get_child_count(root);i++) {
		lv_obj_t *found=fileops_label(lv_obj_get_child(root,(int)i),text);
		if(found) return found;
	}
	return NULL;
}
static void fileops_path(char *out,size_t size,const char *name) {
	int n=snprintf(out,size,"%s/%s",fileops_fixture,name);assert(n>0 && (size_t)n<size);
}
static bool fileops_exists(const char *name) {
	char path[1024];fileops_path(path,sizeof(path),name);return access(path,F_OK)==0;
}
static void fileops_verify(const char *name,const char *value) {
	char path[1024],bytes[64]={0};fileops_path(path,sizeof(path),name);
	FILE *f=fopen(path,"rb");assert(f);assert(fread(bytes,1,sizeof(bytes)-1,f)==strlen(value));fclose(f);assert(!strcmp(bytes,value));
}
static void fileops_menu(const char *name) {
	lv_obj_t *label=fileops_label(filespage_screen,name);assert(label);
	lv_obj_t *dots=find_visible_image(lv_obj_get_parent(label),&icon_ellipsis_vertical);assert(dots);
	qe_click(lv_obj_get_parent(dots));
}
static lv_obj_t *fileops_picker(const char *tag) {
	assert(!sx_mini_visible());
	lv_obj_t *here=fileops_label(filespage_screen,tr(tag));assert(here);
	return lv_obj_get_parent(lv_obj_get_parent(here));
}
static void fileops_form(const char *name) {
	lv_obj_t *field=find_field(filespage_screen);assert(field && visible(field));
	lv_textarea_set_text(field,name);
	qe_click(qe_key(filespage_screen,tr("ok")));
}
static void fileops_states(lv_timer_t *timer) {
	static unsigned step,waits,feedback_wait,picker_return;
	if(gui_modal_visible()) {assert(++waits<80);return;}
	waits=0;
	// Success feedback is a real overlay. Dismiss it before touching controls
	// below it, including the naming keyboard's accept key.
	const char *feedback[]={"files_copied","files_moved","files_deleted"};
	for(unsigned i=0;i<sizeof(feedback)/sizeof(feedback[0]);i++) {
		lv_obj_t *label=fileops_label(lv_layer_top(),tr(feedback[i]));
		if(label) {assert(++feedback_wait<4);qe_click(label);return;}
	}
	feedback_wait=0;
	switch(step++) {
	case 0: {
		lv_timer_set_period(timer,250);
		gui_init_rest();player_sheet_close(false);theme_set_dark(true);
		pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,read_pointer);
		device_state_restore_file(getenv("SONIX_UI_TEST_TRACK"),5);
		player_refresh_now_playing();player_sheet_close(false);
		sx_mini_set_mode(lv_display_get_vertical_resolution(lv_display_get_default())==800?SX_MINI_COMPACT:SX_MINI_NORMAL);
		const char *root=getenv("SONIX_SD_ROOT");assert(root && root[0]);
		storage_card_token_t card;char admitted[1024];
		assert(storage_card_try_admit(&card,admitted,sizeof(admitted)) && !strcmp(admitted,root));
		int n=snprintf(fileops_fixture,sizeof(fileops_fixture),"%s/000FileOps",root);assert(n>0 && (size_t)n<sizeof(fileops_fixture));
		assert(!mkdir(fileops_fixture,0700));char path[1024];fileops_path(path,sizeof(path),"Destination");assert(!mkdir(path,0700));
		fileops_path(path,sizeof(path),"Source.txt");FILE *f=fopen(path,"wx");assert(f);assert(fputs("owned fixture",f)>=0);assert(!fclose(f));
		filespage_open();break;
	}
	case 1: qe_click(fileops_label(filespage_screen,"000FileOps"));break;
	case 2: fileops_menu("Source.txt");break;
	case 3: qe_click(fileops_label(lv_layer_top(),tr("files_copy_to")));break;
	case 4:
		// An unrelated page can replace the shared suppression slot while a
		// picker remains behind Home. Loading it must restore its own state.
		if (picker_return == 0) {
			switch_screen_no_history(main_menu_screen);
			picker_return++;step--;break;
		}
		if (picker_return == 1) {
			sx_mini_input_overlay(playlistpage_screen,true);
			sx_mini_input_overlay(playlistpage_screen,false);
			switch_screen_no_history(filespage_screen);
			picker_return++;step--;break;
		}
		capture("fileops-picker-return");
		qe_click(fileops_label(fileops_picker("files_copy_here"),"000FileOps"));break;
	case 5: qe_click(fileops_label(fileops_picker("files_copy_here"),"Destination"));break;
	case 6: qe_click(fileops_label(fileops_picker("files_copy_here"),tr("files_copy_here")));break;
	case 7:
		assert(sx_mini_visible());
		fileops_verify("Destination/Source.txt","owned fixture");fileops_verify("Source.txt","owned fixture");capture("fileops-copy-complete");
		fileops_menu("Source.txt");break;
	case 8: qe_click(fileops_label(lv_layer_top(),tr("rename")));break;
	case 9: fileops_form("Renamed.txt");break;
	case 10:
		assert(sx_mini_visible());
		assert(!fileops_exists("Source.txt"));fileops_verify("Renamed.txt","owned fixture");capture("fileops-rename-complete");
		fileops_menu("Renamed.txt");break;
	case 11: qe_click(fileops_label(lv_layer_top(),tr("files_move_to")));break;
	case 12: qe_click(fileops_label(fileops_picker("files_move_here"),"000FileOps"));break;
	case 13: qe_click(fileops_label(fileops_picker("files_move_here"),"Destination"));break;
	case 14: qe_click(fileops_label(fileops_picker("files_move_here"),tr("files_move_here")));break;
	case 15:
		assert(!fileops_exists("Renamed.txt"));fileops_verify("Destination/Renamed.txt","owned fixture");capture("fileops-move-complete");
		qe_click(lv_obj_get_parent(find_visible_image(filespage_screen,&icon_folder_new)));break;
	case 16: fileops_form("Created");break;
	case 17: assert(fileops_exists("Created"));capture("fileops-mkdir-complete");fileops_menu("Created");break;
	case 18: qe_click(fileops_label(lv_layer_top(),tr("delete")));break;
	case 19: qe_click(fileops_label(lv_layer_top(),tr("delete")));break;
	case 20:
		assert(!fileops_exists("Created"));capture("fileops-delete-complete");
		qe_click(fileops_label(filespage_screen,"Destination"));break;
	case 21: fileops_menu("Source.txt");break;
	case 22: qe_click(fileops_label(lv_layer_top(),tr("delete")));break;
	case 23: qe_click(fileops_label(lv_layer_top(),tr("delete")));break;
	case 24:
		assert(!fileops_exists("Destination/Source.txt"));fileops_verify("Destination/Renamed.txt","owned fixture");
		assert(fileops_label(filespage_screen,"Renamed.txt"));capture("fileops-final-list");
		puts("UI_TEST_PASS file-manager copy/rename/move/mkdir/delete use actual controls, owned worker completion and exact file contents");fflush(stdout);exit(0);
	}
}
