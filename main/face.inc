/* Material 风格饼干表盘：静态圆瓣，无额外帧缓冲。 */
static lv_obj_t *hand_hour, *hand_min, *second_dot, *lbl_date, *lbl_digits, *lbl_minutes;
static lv_point_precise_t p_hour[2], p_min[2];

static lv_obj_t *disc(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, x - size / 2, y - size / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void set_hand(lv_obj_t *line, lv_point_precise_t *pts, int len, float deg)
{
    float a = (deg - 90.0f) * (float)M_PI / 180.0f;
    pts[0].x = CX;
    pts[0].y = CY;
    pts[1].x = CX + cosf(a) * len;
    pts[1].y = CY + sinf(a) * len;
    lv_line_set_points(line, pts, 2);
}

static lv_obj_t *make_hand(lv_obj_t *scr, lv_point_precise_t *pts, int w, uint32_t color)
{
    lv_obj_t *line = lv_line_create(scr);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_line_width(line, w, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    return line;
}

static void update_clock(lv_timer_t *timer)
{
    (void)timer;
    time_t now = time(NULL);
    static time_t last = (time_t)-1;
    if (now == last) return;
    last = now;
    struct tm tm;
    localtime_r(&now, &tm);
    char digits[16];
    snprintf(digits, sizeof(digits), "%02d", tm.tm_hour);
    if (strcmp(lv_label_get_text(lbl_digits), digits)) lv_label_set_text(lbl_digits, digits);
    snprintf(digits, sizeof(digits), "%02d", tm.tm_min);
    if (strcmp(lv_label_get_text(lbl_minutes), digits)) lv_label_set_text(lbl_minutes, digits);
    set_hand(hand_hour, p_hour, 96, (tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30);
    set_hand(hand_min, p_min, 137, (tm.tm_min + tm.tm_sec / 60.0f) * 6);
    float a = (tm.tm_sec * 6 - 90) * (float)M_PI / 180;
    lv_obj_set_pos(second_dot, CX + (int)(cosf(a) * 179) - 20,
                  CY + (int)(sinf(a) * 179) - 20);
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    char date[32];
    snprintf(date, sizeof(date), "%s  %02d.%02d", days[tm.tm_wday], tm.tm_mon + 1, tm.tm_mday);
    if (strcmp(lv_label_get_text(lbl_date), date)) lv_label_set_text(lbl_date, date);
}

static void build_face(lv_obj_t *scr)
{
    const uint32_t face = 0x17291F;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    /* 12 个圆瓣与中心圆取视觉并集，轮廓全部静态，避免软件模糊。 */
    disc(scr, CX, CY, 388, face);
    for (int i = 0; i < 12; i++) {
        float a = (i * 360.0f / 12 - 90) * (float)M_PI / 180;
        disc(scr, CX + (int)(cosf(a) * 174), CY + (int)(sinf(a) * 174), 94, face);
    }
    const char *nums[] = {"12", "3", "6", "9"};
    const int x[] = {0, 157, 0, -157}, y[] = {-158, 0, 157, 0};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *label = lv_label_create(scr);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x526C54), 0);
        lv_label_set_text(label, nums[i]);
        lv_obj_align(label, LV_ALIGN_CENTER, x[i], y[i]);
    }
    lv_obj_t *pill = lv_obj_create(scr);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 146, 34);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 85);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x253C2C), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lbl_date = lv_label_create(pill);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xB1C99B), 0);
    lv_label_set_text(lbl_date, "---  --.--");
    lv_obj_center(lbl_date);
    lbl_digits = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_digits, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_digits, lv_color_hex(0x8D9670), 0);
    lv_obj_set_style_text_outline_stroke_color(lbl_digits, lv_color_hex(0x8D9670), 0);
    lv_obj_set_style_text_outline_stroke_width(lbl_digits, 2, 0);
    lv_obj_set_style_text_outline_stroke_opa(lbl_digits, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(lbl_digits, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(lbl_digits, -6, 0);
    lv_label_set_text(lbl_digits, "00");
    lv_obj_set_style_transform_scale_x(lbl_digits, 384, 0);
    lv_obj_set_style_transform_scale_y(lbl_digits, 384, 0);
    lv_obj_align(lbl_digits, LV_ALIGN_CENTER, -91, -92);
    lbl_minutes = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_minutes, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_minutes, lv_color_hex(0x78967B), 0);
    lv_obj_set_style_text_outline_stroke_color(lbl_minutes, lv_color_hex(0x78967B), 0);
    lv_obj_set_style_text_outline_stroke_width(lbl_minutes, 2, 0);
    lv_obj_set_style_text_outline_stroke_opa(lbl_minutes, LV_OPA_COVER, 0);
    lv_label_set_text(lbl_minutes, "00");
    lv_obj_set_style_transform_scale_x(lbl_minutes, 384, 0);
    lv_obj_set_style_transform_scale_y(lbl_minutes, 384, 0);
    lv_obj_align(lbl_minutes, LV_ALIGN_CENTER, 70, 68);
    hand_hour = make_hand(scr, p_hour, 40, 0x436747);
    hand_min = make_hand(scr, p_min, 40, 0x537D69);
    second_dot = disc(scr, CX, CY - 179, 40, 0x537D69);
    update_clock(NULL);
    lv_timer_create(update_clock, 250, NULL);
}
