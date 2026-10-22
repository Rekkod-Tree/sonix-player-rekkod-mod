#define graphite_ui_test_init mono_unused_original_init
#include "graphite_ui_test.c"
#undef graphite_ui_test_init
#include "src/gui/shell/artview.h"
static lv_image_dsc_t picture;
static uint16_t source_pixels[56*56];
static lv_obj_t *test_screen,*photo,*content_row;
static uint32_t ticket;
static void assert_gray(const lv_image_dsc_t *dsc) {
 assert(dsc && dsc->header.cf==LV_COLOR_FORMAT_RGB565);
 const uint16_t *pixel=(const uint16_t*)dsc->data;
 for(unsigned i=0;i<dsc->header.w*dsc->header.h;i++) {
  unsigned r=pixel[i]>>11,g=(pixel[i]>>5)&63,b=pixel[i]&31;
  assert(r==b && abs((int)(r*2)-(int)g)<=2);
 }
}
static void adapter_tests(void) {
 test_screen=lv_obj_create(NULL);lv_screen_load(test_screen);
 lv_obj_update_layout(test_screen);
 photo=lv_image_create(test_screen);lv_obj_set_pos(photo,20,20);
 picture=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=56,.h=56,.stride=112},.data_size=sizeof(source_pixels),.data=(const uint8_t*)source_pixels};
 for(unsigned i=0;i<56*56;i++)source_pixels[i]=(i%3==0)?0xf800:(i%3==1)?0x07e0:0x001f;
 uint16_t original[56*56];memcpy(original,source_pixels,sizeof(original));
 ticket=cover_new_display_generation();
 assert(sx_artview_bind(photo,&picture,ticket));
 assert(((const lv_image_dsc_t*)lv_image_get_src(photo))->data==picture.data);
 lv_obj_update_layout(test_screen);assert(theme_set_monochrome_covers(true));
 fprintf(stderr,"FIRST stats=%u visible=%d source=%p original=%p\n",sx_artview_stats().conversions,lv_obj_is_visible(photo),lv_image_get_src(photo),&picture);assert_gray(lv_image_get_src(photo));assert(!memcmp(original,source_pixels,sizeof(original)));
 sx_artview_stats_t one=sx_artview_stats();
 for(int i=0;i<100;i++){assert(sx_artview_bind(photo,&picture,ticket));assert(sx_artview_refresh());}
 sx_artview_stats_t repeat=sx_artview_stats();
 assert(repeat.conversions==one.conversions && repeat.bytes==one.bytes);
 // Same address reused for different pixels requires the explicit content token.
 source_pixels[0]=0xffff;ticket=cover_new_display_generation();
 assert(sx_artview_bind(photo,&picture,ticket));assert(sx_artview_stats().conversions==one.conversions+1);
 assert(((const uint16_t*)((const lv_image_dsc_t*)lv_image_get_src(photo))->data)[0]==0xffff);
 lv_obj_set_hidden(photo,true);assert(sx_artview_refresh());assert(sx_artview_stats().bytes<one.bytes);
 lv_obj_set_hidden(photo,false);assert(sx_artview_refresh());assert_gray(lv_image_get_src(photo));
 theme_set_force_color_placeholders(true);assert_gray(lv_image_get_src(photo));
 content_row=sx_row_create(test_screen,NULL,false);lv_obj_set_pos(content_row,16,150);lv_obj_set_width(content_row,448);
 sx_row_set_title(content_row,"Цветная заглушка — свой цвет");sx_row_set_thumb(content_row,NULL,sx_placeholder_name("album"));
 sx_row_set_content(content_row,sx_palette_hue_of_name("album"),false,false,0);
 lv_obj_t *icon=find_image(content_row,&icon_music2);assert(icon);
 lv_obj_t *lead=lv_obj_get_parent(icon);
 lv_obj_update_layout(test_screen);lv_color_t colored=lv_obj_get_style_bg_color(lead,0);
 theme_set_force_color_placeholders(false);
 lv_color_t gray=lv_obj_get_style_bg_color(lead,0);assert(gray.red==gray.blue && abs((int)gray.red-(int)gray.green)<=4);
 theme_set_force_color_placeholders(true);assert(lv_color_eq(colored,lv_obj_get_style_bg_color(lead,0)));
 // A raw setter clears content registration, so status colors remain raw.
 sx_row_set_glyph(content_row,&icon_music2,lv_color_hex(0xff4422));theme_set_force_color_placeholders(false);
 assert(lv_color_eq(lv_obj_get_style_bg_color(lead,0),lv_color_hex(0xff4422)));
 // Raw lead + content metadata must reuse its existing photo gray allocation.
 sx_row_set_thumb(content_row,&picture,sx_placeholder_name("album"));sx_row_set_content(content_row,50,false,true,ticket);
 one=sx_artview_stats();
 for(int i=0;i<100;i++){sx_row_set_thumb(content_row,&picture,sx_placeholder_name("album"));sx_row_set_content(content_row,50,false,true,ticket);}
 assert(sx_artview_stats().conversions==one.conversions && sx_artview_stats().bytes==one.bytes);
 sx_row_set_letter(content_row,"A",sx_placeholder_name("artist"),true);sx_row_set_content(content_row,140,false,false,0);
 // Freeing a hidden source invalidates every borrowed view before pixels die.
 sx_artview_forget_pixels(picture.data,ticket);assert(!lv_image_get_src(photo));
 // Toggle while hidden/old source released cannot resurrect the old descriptor.
 lv_obj_set_hidden(photo,true);theme_set_monochrome_covers(false);theme_set_monochrome_covers(true);assert(!lv_image_get_src(photo));
 lv_obj_set_hidden(photo,false);sx_artview_bind(photo,&picture,cover_new_display_generation());
 // A background uses the same budget/ownership, without adding UI objects.
 lv_obj_t *background=lv_obj_create(test_screen);lv_obj_set_size(background,56,56);lv_obj_set_pos(background,100,20);lv_obj_update_layout(test_screen);
 assert(sx_artview_bind_background(background,&picture,ticket));assert_gray(lv_obj_get_style_bg_image_src(background,0));
 theme_set_monochrome_covers(false);assert(((const lv_image_dsc_t*)lv_obj_get_style_bg_image_src(background,0))->data==picture.data);
 theme_set_monochrome_covers(true);assert_gray(lv_obj_get_style_bg_image_src(background,0));
 sx_artview_forget_pixels(picture.data,0);assert(!lv_obj_get_style_bg_image_src(background,0));
 // RGB565 row copy preserves alpha/warp ownership outside this helper.
 uint16_t row[56];assert(sx_artview_copy_gray565(source_pixels,row,56,1,112,112));
 lv_image_dsc_t row_dsc=picture;row_dsc.header.h=1;row_dsc.data=(void*)row;assert_gray(&row_dsc);
 puts("MONO_TEST_PASS bind/rebind ticket, hidden source release, force/photo precedence, raw status, no steady reconversion");
 capture("mono-adapter-and-placeholder");
 lv_screen_load(main_menu_screen);lv_obj_delete(test_screen);test_screen=NULL;
 assert(sx_artview_stats().bytes==0 && sx_artview_stats().bindings==0);
}
static void budget_tests(void) {
 lv_obj_t *screen=lv_obj_create(NULL);lv_screen_load(screen);lv_obj_update_layout(screen);
 lv_obj_t *images[15];lv_image_dsc_t source[15];uint8_t *pixels[15];
 for(int i=0;i<15;i++) {
  unsigned w=i==0?480:i==1?480:56,h=i==0?488:i==1?800:56;
  pixels[i]=malloc(w*h*2);assert(pixels[i]);memset(pixels[i],0xf0,w*h*2);
  source[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=w,.h=h,.stride=w*2},.data_size=w*h*2,.data=pixels[i]};
  images[i]=lv_image_create(screen);lv_obj_set_pos(images[i],0,0);
  assert(sx_artview_bind(images[i],&source[i],cover_new_display_generation()));
 }
 assert(sx_artview_stats().bytes<=SX_ARTVIEW_BUDGET);
 printf("MONO_BUDGET normal15 views bytes=%zu peak=%zu failures=%u\n",sx_artview_stats().bytes,sx_artview_stats().peak_bytes,sx_artview_stats().failures);
 lv_obj_t *fit1=lv_image_create(screen),*fit2=lv_image_create(screen);
 assert(sx_artview_bind(fit1,&source[1],cover_new_display_generation()));
 assert(sx_artview_bind(fit2,&source[1],cover_new_display_generation()));
 printf("CAP3_FITTED bytes=%zu cap=%u\n",sx_artview_stats().bytes,SX_ARTVIEW_BUDGET);
 lv_obj_t *extra=lv_image_create(screen);
 assert(!sx_artview_bind(extra,&source[1],cover_new_display_generation()));
 assert(((const lv_image_dsc_t*)lv_image_get_src(extra))->data==source[1].data);
 printf("CAP3_OVERFLOW tracked original bytes=%zu failures=%u\n",sx_artview_stats().bytes,sx_artview_stats().failures);unsigned failed=sx_artview_stats().failures;for(int i=0;i<10;i++)sx_artview_refresh();assert(sx_artview_stats().failures==failed);
 // Occlusion releases underlying list views; return reconstructs only still-owned source data.
 sx_artview_opaque_overlay(images[0]);assert(sx_artview_stats().bytes<500000);
 sx_artview_opaque_overlay(NULL);
 lv_screen_load(main_menu_screen);lv_obj_delete(screen);for(int i=0;i<15;i++)free(pixels[i]);
 assert(!sx_artview_stats().bytes && !sx_artview_stats().bindings);
 puts("MONO_TEST_PASS hard budget and original fallback, no frame retry, occlusion/delete release");
}
static void shared_variant_tests(void) {
 theme_set_monochrome_covers(true);
 lv_obj_t *screen=lv_obj_create(NULL);lv_screen_load(screen);lv_obj_update_layout(screen);
 cover_image_t owner={0};owner.pixels=malloc(sizeof(source_pixels));assert(owner.pixels);memcpy(owner.pixels,source_pixels,sizeof(source_pixels));
 owner.dsc=picture;owner.dsc.data=owner.pixels;owner.display_generation=cover_new_display_generation();
 lv_obj_t *first=lv_image_create(screen),*second=lv_image_create(screen);lv_obj_set_pos(second,100,0);
 assert(sx_artview_bind(first,&owner.dsc,owner.display_generation));unsigned conversions=sx_artview_stats().conversions;size_t one=sx_artview_stats().bytes;
 assert(sx_artview_bind(second,&owner.dsc,owner.display_generation));
 assert(sx_artview_stats().conversions==conversions && sx_artview_stats().bytes-one<1024);
 assert(((const lv_image_dsc_t*)lv_image_get_src(first))->data==((const lv_image_dsc_t*)lv_image_get_src(second))->data);
 lv_obj_t *background=lv_obj_create(screen);lv_obj_set_size(background,56,56);lv_obj_update_layout(screen);
 assert(sx_artview_bind_background(background,&owner.dsc,owner.display_generation));assert(sx_artview_stats().conversions==conversions);
 assert(((const lv_image_dsc_t*)lv_obj_get_style_bg_image_src(background,0))->data==((const lv_image_dsc_t*)lv_image_get_src(second))->data);
 lv_obj_delete(first);assert_gray(lv_image_get_src(second));
 sx_artview_release_cover(&owner);assert(!lv_image_get_src(second));assert(!lv_obj_get_style_bg_image_src(background,0));assert(!owner.pixels);
 lv_screen_load(main_menu_screen);lv_obj_delete(screen);assert(!sx_artview_stats().bytes);
 puts("MONO_TEST_PASS sharedvariant independent detach/delete + ownerfree forget-all no doublefree");
}
static void metadata_limit_tests(void) {
 theme_set_monochrome_covers(false);
 lv_obj_t *screen=lv_obj_create(NULL);lv_screen_load(screen);lv_obj_update_layout(screen);
 for(int i=0;i<512;i++) {lv_obj_t *image=lv_image_create(screen);assert(sx_artview_bind(image,&picture,ticket));}
 assert(sx_artview_stats().bindings==512);
 lv_obj_t *extra=lv_image_create(screen);lv_image_set_src(extra,&picture);
 assert(!sx_artview_bind(extra,&picture,ticket));assert(!lv_image_get_src(extra));
 sx_artview_detach(extra);assert(!lv_image_get_src(extra));
 lv_obj_t *background=lv_obj_create(screen);lv_obj_set_style_bg_image_src(background,&picture,0);
 assert(!sx_artview_bind_background(background,&picture,ticket));assert(!lv_obj_get_style_bg_image_src(background,0));
 sx_artview_detach(background);assert(!lv_obj_get_style_bg_image_src(background,0));
 sx_artview_forget_pixels(picture.data,0);
 for(int i=0;i<512;i++)assert(!lv_image_get_src(lv_obj_get_child(screen,i)));
 lv_screen_load(main_menu_screen);lv_obj_delete(screen);
 assert(!sx_artview_stats().bindings && !sx_artview_stats().bytes);
 puts("MONO_TEST_PASS ordinary512metadata-cap/513cleared no borrowed untracked source, detach/forget safe");
}
static void run_mono(lv_timer_t *timer) {
 lv_timer_delete(timer);gui_init_rest();
 theme_set_palette(0);theme_set_force_color_placeholders(false);theme_set_monochrome_covers(false);
 adapter_tests();budget_tests();shared_variant_tests();metadata_limit_tests();puts("UI_TEST_PASS monochrome artwork adapter and content roles");fflush(stdout);exit(0);
}
void graphite_ui_test_init(void) {
 signal(SIGABRT,SIG_DFL);out_dir=getenv("SONIX_UI_TEST_DIR");assert(out_dir);
 lv_timer_create(run_mono,700,NULL);
}
