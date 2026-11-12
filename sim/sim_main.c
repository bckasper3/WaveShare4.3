/* PC UI simulator: the device UI (main/ui) on LVGL 8.3 with fake backends.
 *   el_sim.exe                 interactive SDL window, mouse = touch
 *   el_sim.exe --shots <dir>   headless: one PNG per screen / state (virtual clock)
 *   el_sim.exe --navtest       headless: taps every route between pages, exit code 1 on failure */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "lvgl.h"
#include "sim.h"
#include "ui.h"
#include "ui_theme.h"
#include "app_state.h"
#include "ui_screens.h"
#include "el_ui_settings.h"

#define W 800
#define H 480

/* ---------------- clock ---------------- */
static bool     s_virtual;
static uint32_t s_vms;
uint32_t sim_tick_ms(void) { return s_virtual ? s_vms : SDL_GetTicks(); }

/* ---------------- display ---------------- */
static lv_color_t s_fb[W * H];
static lv_color_t s_draw[W * H];
static uint8_t    s_backlight = 100;
static SDL_Renderer *s_ren;
static SDL_Texture  *s_tex;

void sim_backlight(uint8_t pct) { s_backlight = pct; }

static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px)
{
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&s_fb[y * W + a->x1], &px[(y - a->y1) * (a->x2 - a->x1 + 1)], (size_t)(a->x2 - a->x1 + 1) * sizeof(lv_color_t));
    if (s_tex && lv_disp_flush_is_last(d)) {
        SDL_UpdateTexture(s_tex, NULL, s_fb, W * sizeof(lv_color_t));
        SDL_SetTextureColorMod(s_tex, s_backlight ? 255 : 20, s_backlight ? 255 : 20, s_backlight ? 255 : 20);
        SDL_RenderClear(s_ren);
        SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
        SDL_RenderPresent(s_ren);
    }
    lv_disp_flush_ready(d);
}

static int s_mx, s_my;
static bool s_mdown;
static void mouse_cb(lv_indev_drv_t *d, lv_indev_data_t *data)
{
    (void)d;
    data->point.x = (lv_coord_t)s_mx;
    data->point.y = (lv_coord_t)s_my;
    data->state = s_mdown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void lvgl_setup(void)
{
    lv_init();
    static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf, s_draw, NULL, W * H);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = W;
    dd.ver_res = H;
    dd.flush_cb = flush_cb;
    dd.draw_buf = &buf;
    lv_disp_drv_register(&dd);
    static lv_indev_drv_t id;
    lv_indev_drv_init(&id);
    id.type = LV_INDEV_TYPE_POINTER;
    id.read_cb = mouse_cb;
    lv_indev_drv_register(&id);
}

/* ---------------- PNG (stored deflate, no dependencies) ---------------- */
static uint32_t crc_tab[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t h[8];
    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = crc_upd(c, data, len) ^ 0xFFFFFFFFu;
    be32(h, c);
    fwrite(h, 1, 4, f);
}

static bool write_png(const char *path)
{
    const size_t row = 1 + W * 3, raw_n = row * H;
    uint8_t *raw = malloc(raw_n);
    size_t blocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n);
    if (!raw || !z) { free(raw); free(z); return false; }
    for (int y = 0; y < H; y++) {
        uint8_t *p = raw + y * row;
        *p++ = 0;
        for (int x = 0; x < W; x++) {
            lv_color_t c = s_fb[y * W + x];
            uint32_t c32 = lv_color_to32(c);
            *p++ = (uint8_t)(c32 >> 16);
            *p++ = (uint8_t)(c32 >> 8);
            *p++ = (uint8_t)c32;
        }
    }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint16_t len = (uint16_t)(raw_n - off > 65535 ? 65535 : raw_n - off);
        z[o++] = off + len >= raw_n ? 1 : 0;
        z[o++] = len & 0xFF; z[o++] = len >> 8;
        z[o++] = ~len & 0xFF; z[o++] = (uint8_t)(~len >> 8);
        memcpy(z + o, raw + off, len);
        o += len;
    }
    be32(z + o, (b << 16) | a);
    o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return false; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, W); be32(ihdr + 4, H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return true;
}

/* ---------------- headless script ---------------- */
static const char *s_dir;
static int s_shot;

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 10) {
        s_vms += 10;
        sim_backend_step();
        lv_timer_handler();
    }
}

static void shot(const char *name)
{
    run_ms(700);                         /* animations, the 500 ms refresh tick */
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof path, "%s/%02d_%s.png", s_dir, ++s_shot, name);
    if (!write_png(path)) fprintf(stderr, "cannot write %s\n", path);
    else printf("%s\n", path);
}

static void close_all(void) { ui_dialog_close(); run_ms(20); }

static bool pass_text(const char *t, void *u) { (void)t; (void)u; return true; }
static void pass_choice(int i, void *u) { (void)i; (void)u; }
static void pass_confirm(void *u) { (void)u; }

/* ---------------- navigation test (real touches through LVGL hit-testing) ---------------- */
static int s_nav_fail, s_nav_routes;

static void tap(int x, int y)
{
    s_mx = x;
    s_my = y;
    s_mdown = true;
    run_ms(60);
    s_mdown = false;
    run_ms(60);
}

#define TAP_MENU()  tap(40, 22)
#define TAP_BACK()  tap(44, 80)
#define TAP_HOME()  tap(120, 80)

static void nav_expect(ui_screen_t want, const char *what, int from, int to)
{
    if (ui_current() == want) return;
    printf("FAIL %s (from page %d, to page %d): on page %d, wanted %d\n", what, from, to, (int)ui_current(), (int)want);
    s_nav_fail++;
}

static bool has_header(ui_screen_t s) { return s != SCR_HOME && s != SCR_MEASURING && s != SCR_RESULT; }

static int navtest(void)
{
    s_virtual = true;
    ui_init();
    run_ms(100);
    ui_boot_done();
    run_ms(1600);
    nav_expect(SCR_HOME, "boot ends on Home", -1, -1);
    sim_set_result(22.0f, 45.0f, 3, false);   /* gives the Last reading tile something to open */
    run_ms(700);

    for (int f = 0; f < SCR_COUNT; f++) {
        for (int i = 0; i < ui_menu_count(); i++) {
            ui_screen_t from = (ui_screen_t)f, to = ui_menu_screen(i);
            ui_go(SCR_HOME);
            if (from != SCR_HOME) ui_go(from);
            run_ms(60);
            TAP_MENU();
            if (!ui_dialog_open()) { printf("FAIL Menu did not open on page %d\n", f); s_nav_fail++; continue; }
            lv_coord_t x, y;
            ui_menu_center(i, &x, &y);
            tap(x, y);
            if (ui_dialog_open()) { printf("FAIL menu stayed open (from %d, tile %d)\n", f, i); s_nav_fail++; close_all(); continue; }
            nav_expect(to, "menu tile", f, (int)to);
            s_nav_routes++;
            if (to == from) continue;
            /* ...and the way back from the page we arrived on */
            if (has_header(to)) {
                TAP_BACK();
                nav_expect((from == SCR_MEASURING || from == SCR_RESULT) ? SCR_HOME : from, "Back", f, (int)to);
            }
        }
    }
    for (int s = 0; s < SCR_COUNT; s++) {
        if (!has_header((ui_screen_t)s)) continue;
        ui_go(SCR_HOME);
        ui_go((ui_screen_t)s);
        run_ms(60);
        TAP_HOME();
        nav_expect(SCR_HOME, "header Home", s, SCR_HOME);
    }
    /* Result has no header: its own Home button is the way out. */
    ui_go(SCR_RESULT);
    run_ms(700);
    tap(UI_M + 110, 426);
    nav_expect(SCR_HOME, "Result Home button", SCR_RESULT, SCR_HOME);

    /* A running measurement owns the screen: Menu must not open, Cancel is the way out. */
    sim_set_measuring(40, 1);
    ui_go(SCR_MEASURING);
    run_ms(700);
    TAP_MENU();
    if (ui_dialog_open()) { printf("FAIL Menu opened during a measurement\n"); s_nav_fail++; }
    nav_expect(SCR_MEASURING, "stay on Measuring", SCR_MEASURING, SCR_MEASURING);

    printf("%d routes tapped, %d failures\n", s_nav_routes, s_nav_fail);
    return s_nav_fail ? 1 : 0;
}

static int shots(void)
{
    s_virtual = true;
    ui_init();
    run_ms(100);
    shot("boot");
    ui_boot_done();
    run_ms(1600);
    shot("home");

    sim_set_measuring(64, 2);
    ui_go(SCR_MEASURING);
    shot("measuring");

    sim_set_result(21.6f, 44.5f, 3, false);
    ui_go(SCR_RESULT);
    shot("result_normal");
    ui_go(SCR_HOME);
    sim_set_result(31.2f, 66.0f, 3, false);
    ui_go(SCR_RESULT);
    shot("result_alarm");
    ui_go(SCR_HOME);
    sim_set_result(-18.4f, 72.0f, 5, true);
    ui_go(SCR_RESULT);
    shot("result_freezer");
    ui_go(SCR_HOME);
    sim_set_fault(EL_MEAS_FAULT_SENSOR_NO_ACK);
    ui_go(SCR_RESULT);
    shot("result_fault");
    sim_set_result(21.6f, 44.5f, 3, true);
    ui_go(SCR_HOME);
    shot("home_after");

    ui_go(SCR_HISTORY);
    shot("history");
    ui_go(SCR_TREND);
    shot("trend_temperature");
    tap(700, 80);                                   /* the Humidity button in the header */
    shot("trend_humidity");

    ui_go(SCR_EXPORT);
    shot("export_ready");
    sim_set_export(true, 45);
    shot("export_busy");
    sim_set_export(false, 0);
    sim_clear_queue();

    ui_go(SCR_CALIBRATE);
    shot("calibrate");
    ui_settings_open(EL_UI_SG_MEASURE);
    shot("settings_logging");
    ui_go(SCR_DIAG);
    shot("diagnostics");
    ui_go(SCR_ABOUT);
    shot("about");
    ui_go(SCR_SET_TIME);
    shot("set_time");
    ui_settings_open(EL_UI_SG_DISPLAY);
    shot("settings_display");
    ui_settings_open(EL_UI_SG_COMMS);
    shot("settings_comms");
    sim_set_wifi(NET_OFF, 0, NULL);
    ui_go(SCR_WIFI);
    shot("wifi_off");
    sim_set_wifi(NET_CONNECTED, 0, NULL);
    shot("wifi_connected");
    sim_set_wifi(NET_FAILED, 3, "Wrong Wi-Fi password");
    shot("wifi_failed");
    ui_choice_dialog("Choose a Wi-Fi network", (const char *const[]){ UI_ICON_LOCK " Lab-Network  (excellent)",
                     UI_ICON_LOCK " Plant Floor 2.4  (good)", "Guest  (fair)", "+ Type a network name" }, 4, -1, NULL, NULL);
    shot("wifi_scan");
    close_all();
    sim_set_wifi(NET_CONNECTED, 0, NULL);
    ui_settings_open(EL_UI_SG_MEASURE);

    ui_go(SCR_HOME);
    ui_text_dialog("Location name", "Archive room B", EL_UI_CS_TEXT, 19, false, pass_text, NULL);
    shot("dlg_keyboard");
    close_all();
    ui_text_dialog("Admin PIN", "", EL_UI_CS_DIGITS, 8, true, pass_text, NULL);
    shot("dlg_pin");
    close_all();
    static const char *const OPT[] = { "SHT4x (SHT40/41/45)", "SHT3x (SHT30/31/35)" };
    ui_choice_dialog("Sensor type", OPT, 2, 0, pass_choice, NULL);
    shot("dlg_choice");
    close_all();
    ui_confirm_dialog("Delete all results?", "All 144 results will be removed from this logger. Export them first if "
                      "you need them. This cannot be undone.", "Delete all", true, pass_confirm, NULL);
    shot("dlg_confirm");
    close_all();
    ui_toast("Exported 144 results - safe to remove", 4000);
    shot("toast");
    run_ms(4500);
    sim_set_theme(true);
    run_ms(100);
    shot("home_dark");
    ui_go(SCR_RESULT);
    shot("result_dark");
    ui_go(SCR_TREND);
    shot("trend_dark");
    ui_menu_dialog();
    shot("menu_dark");
    close_all();
    sim_set_theme(false);
    ui_go(SCR_HOME);
    run_ms(700);
    ui_menu_dialog();
    shot("menu");
    return 0;
}

/* ---------------- interactive ---------------- */
static int window(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("Environment Logger (simulator)", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, W, H, 0);
    s_ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
    ui_init();
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return 0;
            if (e.type == SDL_MOUSEMOTION) { s_mx = e.motion.x; s_my = e.motion.y; }
            if (e.type == SDL_MOUSEBUTTONDOWN) { s_mdown = true; s_mx = e.button.x; s_my = e.button.y; }
            if (e.type == SDL_MOUSEBUTTONUP) s_mdown = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_s) sim_set_sd(!app_state_raw()->sd_present),
                ui_notify(app_state_raw()->sd_present ? EVT_SD_MOUNTED : EVT_SD_REMOVED, 0);
        }
        sim_backend_step();
        lv_timer_handler();
        SDL_Delay(5);
    }
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    crc_init();
    lvgl_setup();
    sim_backend_init();
    if (argc >= 3 && strcmp(argv[1], "--shots") == 0) {
        s_dir = argv[2];
        return shots();
    }
    if (argc >= 2 && strcmp(argv[1], "--navtest") == 0) return navtest();
    return window();
}
