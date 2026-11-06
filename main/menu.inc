/* lilt 风格弹簧手势演示；LVGL 回调运行在持锁的 UI 任务内。 */
#include "motion.h"
#include "esp_heap_caps.h"
static lv_obj_t *clock_page, *menu_page, *menu_rows[5];
static motion_t page_motion, list_motion, row_scale[5];
static bool touch_down, moved, from_clock;
static lv_point_t touch_origin, touch_last;
static float origin_page, origin_list, finger_velocity;
static uint32_t touch_tick, frame_tick;
static int active_row = -1;
static lv_obj_t *clock_snapshot;
static lv_draw_buf_t *clock_pixels;
static bool clock_cached;
static void cache_clock(void)
{
    if(clock_cached) return;
    if(clock_pixels) {
        if(lv_snapshot_take_to_draw_buf(clock_page,LV_COLOR_FORMAT_RGB565,clock_pixels)!=LV_RESULT_OK) return;
    } else clock_pixels=lv_snapshot_take(clock_page,LV_COLOR_FORMAT_RGB565);
    if(!clock_pixels) { ESP_LOGW("motion-perf","snapshot allocation failed"); return; }
    lv_image_set_src(clock_snapshot,clock_pixels);
    lv_obj_set_y(clock_snapshot,(int)-page_motion.x);
    lv_obj_remove_flag(clock_snapshot,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(clock_page,LV_OBJ_FLAG_HIDDEN);
    clock_cached=true;
}

/* 只统计切页区间，避免逐帧日志干扰测量。 */
static uint32_t perf_last, perf_sum, perf_max, perf_count;
static uint32_t render_start, render_sum, render_count;
static void menu_perf(lv_event_t *e)
{
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_RENDER_START) { render_start=lv_tick_get(); return; }
    if(code==LV_EVENT_RENDER_READY) {
        if(page_motion.x>1 && page_motion.x<465) {
            render_sum+=lv_tick_get()-render_start;
            render_count++;
        }
        return;
    }
    if(code!=LV_EVENT_REFR_READY) return;
    uint32_t now=lv_tick_get();
    bool sliding=page_motion.x>1 && page_motion.x<465;
    if(sliding) {
        if(perf_last) {
            uint32_t dt=now-perf_last;
            perf_sum+=dt;
            if(dt>perf_max) perf_max=dt;
            perf_count++;
        }
        perf_last=now;
    } else if(perf_last) {
        if(perf_count) ESP_LOGI("motion-perf","transition frames=%lu avg_ms=%lu max_ms=%lu",
            (unsigned long)perf_count,(unsigned long)(perf_sum/perf_count),(unsigned long)perf_max);
        if(render_count) ESP_LOGI("motion-perf","render avg_ms=%lu samples=%lu",
            (unsigned long)(render_sum/render_count),(unsigned long)render_count);
        ESP_LOGI("motion-perf","memory internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u min_internal=%u",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        perf_last=perf_sum=perf_max=perf_count=0;
        render_sum=render_count=0;
    }
}

static void menu_frame(lv_timer_t *t)
{
    (void)t;
    uint32_t now=lv_tick_get();
    float dt=(now-frame_tick)*.001f;
    frame_tick=now;
    if (!touch_down) {
        motion_step(&page_motion,dt,.39f,.85f);
        motion_step(&list_motion,dt,.29f,.75f);
    }
    if(clock_cached) {
        lv_obj_set_y(clock_snapshot,(int)-page_motion.x);
        if(!touch_down && page_motion.x==0 && page_motion.v==0) {
            lv_obj_remove_flag(clock_page,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(clock_snapshot,LV_OBJ_FLAG_HIDDEN);
            clock_cached=false;
        }
    }
    lv_obj_set_y(clock_page,(int)-page_motion.x);
    lv_obj_set_y(menu_page,466-(int)page_motion.x);
    for(int i=0;i<5;i++) {
        motion_step(&row_scale[i],dt,.25f,.45f);
        lv_obj_set_y(menu_rows[i],118+i*78+(int)list_motion.x);
        lv_obj_set_style_transform_scale(menu_rows[i],(int)row_scale[i].x,0);
    }
}

static void menu_touch(lv_event_t *e)
{
    if(!scr_on) return;   /* 息屏时手势一律让位，只留唤醒 */
    lv_event_code_t code=lv_event_get_code(e);
    lv_indev_t *indev=lv_indev_active();
    if(!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev,&p);
    if(code==LV_EVENT_PRESSED) {
        if(touch_down) return;
        touch_down=true; moved=false;
        touch_origin=touch_last=p;
        touch_tick=lv_tick_get();
        origin_page=page_motion.x; origin_list=list_motion.x;
        from_clock=page_motion.target<233;
        finger_velocity=0;
        active_row=-1;
        if(!from_clock) for(int i=0;i<5;i++) {
            if(lv_event_get_target(e)==menu_rows[i]) {active_row=i; row_scale[i].target=242;}
        }
    } else if(code==LV_EVENT_PRESSING && touch_down) {
        int dy=p.y-touch_origin.y;
        uint32_t now=lv_tick_get(), elapsed=now-touch_tick;
        if(elapsed) finger_velocity=(p.y-touch_last.y)*1000.f/elapsed;
        touch_last=p; touch_tick=now;
        if(abs(dy)>10) moved=true;
        if(!moved) return;
        if(from_clock && !clock_cached) cache_clock();
        if(active_row>=0) row_scale[active_row].target=256;
        if(from_clock || touch_origin.y<110) {
            float x=origin_page-dy;
            if(x<0) x=-motion_rubber(-x,466);
            if(x>466) x=466+motion_rubber(x-466,466);
            page_motion.x=x; page_motion.v=-finger_velocity;
        } else {
            float x=origin_list+dy;
            if(x>0) x=motion_rubber(x,260);
            if(x< -180) x=-180-motion_rubber(-180-x,260);
            list_motion.x=x; list_motion.v=finger_velocity;
        }
    } else if((code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) && touch_down) {
        touch_down=false;
        if(moved && (from_clock || touch_origin.y<110)) {
            page_motion.target=page_motion.x-finger_velocity*.16f>233 ? 466:0;
        } else if(moved) {
            float target=list_motion.x+finger_velocity*.12f;
            list_motion.target=fminf(0,fmaxf(-180,target));
        }
        if(active_row>=0) {
            row_scale[active_row].target=256;
            if(!moved && code==LV_EVENT_RELEASED) {
                if(active_row==0) page_motion.target=0;
                else if(active_row==2) music_enter();
                else row_scale[active_row].v=650;
            }
        }
        active_row=-1;
    }
}

static lv_obj_t *plain_page(lv_obj_t *parent)
{
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o,466,466);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x080E0A),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o,menu_touch,LV_EVENT_ALL,NULL);
    return o;
}

static void wire_touch(lv_obj_t *o)
{
    uint32_t n=lv_obj_get_child_count(o);
    for(uint32_t i=0;i<n;i++) wire_touch(lv_obj_get_child(o,i));
    lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}

static void build_shell(lv_obj_t *scr)
{
    lv_obj_remove_flag(scr,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr,lv_color_hex(0x080E0A),0);
    clock_page=plain_page(scr);
    build_face(clock_page);
    wire_touch(clock_page);
    clock_snapshot=lv_image_create(scr);
    lv_obj_add_flag(clock_snapshot,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(clock_snapshot,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_snapshot,menu_touch,LV_EVENT_ALL,NULL);
    menu_page=plain_page(scr);
    lv_obj_set_y(menu_page,466);
    lv_obj_t *title=lv_label_create(menu_page);
    lv_label_set_text(title,"PLAYGROUND");
    lv_obj_set_style_text_font(title,&lv_font_montserrat_18,0);
    lv_obj_set_style_text_color(title,lv_color_hex(0xB1C99B),0);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,65);
    const char *names[]={"Clock  /  return","Spring  /  tap","Music  /  demo","Timer  /  demo","Settings  /  demo"};
    for(int i=0;i<5;i++) {
        lv_obj_t *row=menu_rows[i]=lv_obj_create(menu_page);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row,300,64);
        lv_obj_set_pos(row,83,118+i*78);
        lv_obj_set_style_radius(row,22,0);
        lv_obj_set_style_bg_color(row,lv_color_hex(i%2?0x253C2C:0x17291F),0);
        lv_obj_set_style_bg_opa(row,LV_OPA_COVER,0);
        lv_obj_set_style_transform_pivot_x(row,150,0);
        lv_obj_set_style_transform_pivot_y(row,32,0);
        lv_obj_remove_flag(row,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label=lv_label_create(row);
        lv_label_set_text(label,names[i]);
        lv_obj_set_style_text_font(label,&lv_font_montserrat_18,0);
        lv_obj_set_style_text_color(label,lv_color_hex(0xB1C99B),0);
        lv_obj_center(label);
        row_scale[i].x=row_scale[i].target=256;
    }
    wire_touch(menu_page);
    lv_display_add_event_cb(lv_display_get_default(),menu_perf,LV_EVENT_ALL,NULL);
    frame_tick=lv_tick_get();
    lv_timer_create(menu_frame,16,NULL);
}
