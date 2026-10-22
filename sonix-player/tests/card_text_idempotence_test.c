// Real LVGL objects/allocator/layout; application functions are extracted unchanged.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "src/system/net/wifi.h"

static bool glyph(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t letter, uint32_t next) {
 (void)next; memset(out, 0, sizeof(*out)); out->resolved_font=font;
 out->adv_w=letter=='\n'?0:8; out->format=LV_FONT_GLYPH_FORMAT_NONE; return true;
}
#define FONT(name,height) lv_font_t name={.get_glyph_dsc=glyph,.line_height=height,.base_line=2}
FONT(font_ui_14,14); FONT(font_ui_17,17); FONT(font_ui_22,22); FONT(font_ui_24_bold,24);
lv_style_t sx_style_panel_text,sx_style_panel_text2,sx_style_panel_action;
static lv_obj_t *status_label;
static bool available=true,busy,on;
static wifi_status_t status;
static int language;
bool wifi_available(void) {return available;}
bool wifi_busy(void) {return busy;}
bool wifi_get_enabled(void) {return on;}
void wifi_get_status(wifi_status_t *out) {*out=status;}
static const char *tr(const char *key) {
 if(!strcmp(key,"wifi_off_note"))return language?"Wi-Fi disabled":"Wi-Fi off";
 if(!strcmp(key,"turning_on"))return "Turning on";
 if(!strcmp(key,"wifi_turning_off_2"))return "Turning off";
 return "No hardware";
}
static unsigned allocs,texts,styles,invalidations;
void *__real_lv_malloc(size_t size);
void *__real_lv_realloc(void *p,size_t size);
void __real_lv_label_set_text(lv_obj_t *label,const char *text);
void __real_lv_obj_add_style(lv_obj_t *o,lv_style_t *s,lv_style_selector_t part);
void __real_lv_obj_remove_style(lv_obj_t *o,lv_style_t *s,lv_style_selector_t part);
lv_result_t __real_lv_obj_invalidate(const lv_obj_t *o);
void *__wrap_lv_malloc(size_t size) {allocs++;return __real_lv_malloc(size);}
void *__wrap_lv_realloc(void *p,size_t size) {allocs++;return __real_lv_realloc(p,size);}
void __wrap_lv_label_set_text(lv_obj_t *o,const char *text) {texts++;__real_lv_label_set_text(o,text);}
void __wrap_lv_obj_add_style(lv_obj_t *o,lv_style_t *s,lv_style_selector_t p) {styles++;__real_lv_obj_add_style(o,s,p);}
void __wrap_lv_obj_remove_style(lv_obj_t *o,lv_style_t *s,lv_style_selector_t p) {styles++;__real_lv_obj_remove_style(o,s,p);}
lv_result_t __wrap_lv_obj_invalidate(const lv_obj_t *o) {invalidations++;return __real_lv_obj_invalidate(o);}
#include "actual_functions.inc"
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {(void)area;(void)pixels;lv_display_flush_ready(display);}
static void settle(void) {lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);}
static void reset_counts(void) {allocs=texts=styles=invalidations=0;}
static void same_geometry(lv_obj_t *o,lv_area_t expected) {
 lv_area_t now;lv_obj_get_coords(o,&now);assert(!memcmp(&now,&expected,sizeof(now)));
}
int main(void) {
 lv_init();lv_display_t *display=lv_display_create(480,800);assert(display);
 static uint8_t pixels[480*800*2];lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);
 lv_display_set_flush_cb(display,flush);
 lv_style_init(&sx_style_panel_text);lv_style_init(&sx_style_panel_text2);lv_style_init(&sx_style_panel_action);
 lv_style_set_text_color(&sx_style_panel_text,lv_color_hex(0xeeeeee));
 lv_style_set_text_color(&sx_style_panel_text2,lv_color_hex(0x8899aa));
 lv_style_set_text_color(&sx_style_panel_action,lv_color_hex(0x123456));
 lv_obj_t *card=lv_obj_create(lv_screen_active());lv_obj_set_size(card,440,300);
 card_parts_t p={0};lv_obj_set_user_data(card,&p);
 p.head=lv_obj_create(card);lv_obj_set_size(p.head,400,260);
 p.text=lv_obj_create(p.head);lv_obj_set_size(p.text,360,180);
 // Initial selected font already inherited: colour role must still be installed.
 lv_obj_set_style_text_font(p.text,&font_ui_22,0);
 lv_obj_t *title=sx_card_title(card,"Library"),*subtitle=sx_card_subtitle(card,"Ready",false),*value=sx_card_value(card,"On");
 assert(lv_obj_get_style_text_font(subtitle,0)==&font_ui_22);
 assert(lv_color_eq(lv_obj_get_style_text_color(subtitle,0),lv_color_hex(0x8899aa)));
 settle();lv_area_t title_box,sub_box,value_box;
 lv_obj_get_coords(title,&title_box);lv_obj_get_coords(subtitle,&sub_box);lv_obj_get_coords(value,&value_box);
 reset_counts();
 for(int i=0;i<100;i++) {assert(sx_card_title(card,"Library")==title);assert(sx_card_subtitle(card,"Ready",false)==subtitle);assert(sx_card_value(card,"On")==value);}
 unsigned a=allocs,t=texts,s=styles,inv=invalidations;
#if TEST_PATCHED
 assert(a==0&&t==0&&s==0&&inv==0);
#else
 assert(a>=300&&t==300&&s>=300&&inv>0);
#endif
 settle();same_geometry(title,title_box);same_geometry(subtitle,sub_box);same_geometry(value,value_box);
 printf("%s card repeat100 allocations=%u text_calls=%u style_calls=%u invalidate_calls=%u\n",TEST_PATCHED?"FIXED":"BASELINE",a,t,s,inv);
 // Text changes remain live; NULL/empty controls visibility; mode swaps preserve font roles.
 sx_card_title(card,"Library updated");assert(!strcmp(lv_label_get_text(title),"Library updated"));
 sx_card_value(card,NULL);assert(!strcmp(lv_label_get_text(value),""));
 sx_card_subtitle(card,NULL,false);assert(lv_obj_is_hidden(subtitle));
 sx_card_subtitle(card,"Ready",true);assert(!lv_obj_is_hidden(subtitle));
 assert(lv_obj_get_style_text_font(subtitle,0)==&font_ui_17);
 assert(lv_color_eq(lv_obj_get_style_text_color(subtitle,0),lv_color_hex(0x123456)));
 lv_style_set_text_color(&sx_style_panel_action,lv_color_hex(0xaabbcc));lv_obj_report_style_change(&sx_style_panel_action);
 sx_card_subtitle(card,"Ready",true);assert(lv_color_eq(lv_obj_get_style_text_color(subtitle,0),lv_color_hex(0xaabbcc)));
 sx_card_subtitle(card,"Ready",false);assert(lv_obj_get_style_text_font(subtitle,0)==&font_ui_22);
 assert(lv_color_eq(lv_obj_get_style_text_color(subtitle,0),lv_color_hex(0x8899aa)));
 // Ellipsized text is mutable in LVGL. The existing equality helper must stay safe.
 const char *long_text="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
 lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);lv_obj_set_size(title,80,24);
 sx_card_title(card,long_text);settle();assert(strcmp(lv_label_get_text(title),long_text));
 sx_card_title(card,long_text);lv_obj_set_width(title,700);settle();assert(!strcmp(lv_label_get_text(title),long_text));
 assert(lv_obj_get_style_text_font(title,0)==&font_ui_24_bold);
 // Actual status callback, real label: ordinary off/busy/no-hardware updates stop reallocating.
 status_label=lv_label_create(lv_screen_active());status.state=WIFI_STATE_OFF;refresh_status_label();settle();reset_counts();
 for(int i=0;i<100;i++)refresh_status_label();
 a=allocs;t=texts;inv=invalidations;
#if TEST_PATCHED
 assert(a==0&&t==0&&inv==0);
#else
 assert(a>=100&&t==100);
#endif
 printf("%s WiFi off repeat100 allocations=%u text_calls=%u invalidate_calls=%u\n",TEST_PATCHED?"FIXED":"BASELINE",a,t,inv);
 language=1;refresh_status_label();assert(!strcmp(lv_label_get_text(status_label),"Wi-Fi disabled"));
 busy=true;on=true;refresh_status_label();assert(!strcmp(lv_label_get_text(status_label),"Turning on"));reset_counts();
 for(int i=0;i<100;i++)refresh_status_label();
#if TEST_PATCHED
 assert(allocs==0&&texts==0);
#endif
 available=false;refresh_status_label();assert(!strcmp(lv_label_get_text(status_label),"No hardware"));reset_counts();
 for(int i=0;i<100;i++)refresh_status_label();
#if TEST_PATCHED
 assert(allocs==0&&texts==0);
#endif
 available=true;busy=false;status.state=WIFI_STATE_CONNECTED;refresh_status_label();assert(lv_obj_is_hidden(status_label));
 lv_display_delete(display);lv_style_reset(&sx_style_panel_text);lv_style_reset(&sx_style_panel_text2);lv_style_reset(&sx_style_panel_action);lv_deinit();
 puts("PASS real LVGL: initial inherited-font role, equal text no-op, mode/theme change, NULL/empty, geometry and ellipsis recovery");
 return 0;
}
