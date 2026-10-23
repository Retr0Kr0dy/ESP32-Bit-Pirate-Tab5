/*
 * display.c — Tab5 MIPI-DSI bring-up with runtime panel selection.
 *
 * Ported near-verbatim from Tab5-Launchpad/main/display.c (itself forked
 * from Tab5-Orion/main/display.c), which is this project's proven
 * reference for Tab5 display bring-up. Kept under the original `orion_`
 * prefix for this pass -- see display.h's header comment.
 *
 * The one real change from the Launchpad original: orion_display_splash()
 * no longer depends on studio_ui.h (a Launchpad-app-specific UI library
 * that was not vendored into this project) -- it blits the official Bit
 * Pirate logo (lib/BitPirateCore/Data/WelcomeScreen.h, the same bitmap
 * every other board's Tab5DeviceView::logo()/equivalent shows) directly
 * with sgfx_blit() instead of going through a display-library abstraction.
 *
 * ── Why this is not sgfx_autoinit() / SGFX_DRV_TAB5_AUTO ───────────────────
 * M5Stack ships the Tab5 with one of three different display ICs (ILI9881C,
 * ST7121, ST7123) behind the same MIPI-DSI connector, and each one needs a
 * *different* DSI lane bitrate and DPI pixel clock. Those values are consumed
 * by sgfx_hal_make_dsi() when the bus object is constructed, which happens
 * strictly before any panel driver's init() runs. sgfx_autoinit() bakes
 * exactly one SGFX_DSI_* config and one driver in at compile time, so it
 * structurally cannot pick between the three.
 *
 * The reliable identity signal lives on the I2C side: SIC's Tab5 board
 * preinit() releases LCD_RST/TP_RST via the GPIO expanders and reads the touch
 * controller's firmware-version register at 0x55, caching the answer in
 * tab5_panel_detected(). sic_begin_legacy() has already done that by the time
 * app_main() calls this, so all this function does is map the answer onto the
 * right {timings, ops} pair and build the bus once, correctly.
 *
 * ── Two SGFX integration gaps worked around here ───────────────────────────
 *   1. sgfx_open_dsi() never populates dev->caps / dev->scratch — it memsets
 *      the device and sets only bus+drv, leaving the clip box 0x0 and every
 *      draw call a silent no-op. Worked around by not calling it: this file
 *      does sgfx_hal_make_dsi() + sgfx_init() itself.
 *   2. All three DSI panel drivers leave fill_rect NULL, because a video-mode
 *      DSI panel has no CASET/RASET windowing. Worked around by wrapping the
 *      selected panel's ops table with fill_rect/set_window/write_pixels/
 *      present members of our own, using real double-buffering
 *      (sgfx_hal_dsi_get_back_fb()/sgfx_hal_dsi_flip()).
 *
 * ── Backlight ──────────────────────────────────────────────────────────────
 * LEDC low-speed, GPIO22, 12-bit, 5 kHz. Timer/channel choice matters: SIC's
 * camera driver uses LEDC_TIMER_1 for XCLK (keeping CHANNEL_0), so TIMER_0 +
 * CHANNEL_1 here is safe and both can run at once.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/ppa.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sgfx_hal.h"

#include "sic/sic.h"
#include "boards/tab5/panel_detect.h"

#include "esp_log.h"

#include "gui_rotation_bridge.h"
#include "Data/WelcomeScreen.h"

#include "display.h"

static const char* TAG = "display";

/* ── Panel driver symbols (all three compiled; one selected at runtime) ──── */
extern const sgfx_driver_ops_t sgfx_ili9881c_ops;
extern const sgfx_caps_t       sgfx_ili9881c_caps_default;
extern const sgfx_driver_ops_t sgfx_st7121_ops;
extern const sgfx_caps_t       sgfx_st7121_caps_default;
extern const sgfx_driver_ops_t sgfx_st7123_ops;
extern const sgfx_caps_t       sgfx_st7123_caps_default;

/* ── Backlight (LEDC on GPIO22) ─────────────────────────────────────────── */
#define ORION_BL_GPIO     22
#define ORION_BL_MODE     LEDC_LOW_SPEED_MODE
#define ORION_BL_TIMER    LEDC_TIMER_0
#define ORION_BL_CHANNEL  LEDC_CHANNEL_1
#define ORION_BL_RES      LEDC_TIMER_12_BIT
#define ORION_BL_MAX_DUTY 4095
#define ORION_BL_FREQ_HZ  5000

static int s_bl_pct  = -1;
static int s_bl_init = 0;

static int orion_backlight_init(void)
{
    if (s_bl_init) return 0;

    ledc_timer_config_t tcfg = {
        .speed_mode      = ORION_BL_MODE,
        .duty_resolution = ORION_BL_RES,
        .timer_num       = ORION_BL_TIMER,
        .freq_hz         = ORION_BL_FREQ_HZ,
        /* Must match the camera driver's explicit LEDC_USE_PLL_DIV_CLK
         * exactly, not LEDC_AUTO_CLK -- ESP32-P4 has one shared global LEDC
         * slow clock across every low-speed timer (no per-timer mux), so two
         * timers independently auto-selecting a source race and whichever
         * inits second fails with "timer clock conflict". */
        .clk_cfg         = LEDC_USE_PLL_DIV_CLK,
    };
    if (ledc_timer_config(&tcfg) != ESP_OK) return -1;

    ledc_channel_config_t ccfg = {
        .gpio_num   = ORION_BL_GPIO,
        .speed_mode = ORION_BL_MODE,
        .channel    = ORION_BL_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = ORION_BL_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ccfg) != ESP_OK) return -1;

    s_bl_init = 1;
    s_bl_pct  = 0;
    return 0;
}

static int orion_backlight_write_duty(uint32_t duty)
{
    if (!s_bl_init && orion_backlight_init() != 0) return -1;
    if (duty > ORION_BL_MAX_DUTY) duty = ORION_BL_MAX_DUTY;
    if (ledc_set_duty(ORION_BL_MODE, ORION_BL_CHANNEL, duty) != ESP_OK) return -1;
    if (ledc_update_duty(ORION_BL_MODE, ORION_BL_CHANNEL) != ESP_OK) return -1;
    return 0;
}

int orion_backlight_set(int percent)
{
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    /* Gamma-corrected (2.2) percent->duty curve so the low end of a future
     * brightness slider isn't bunched into the first few percent. */
    double frac = pow((double)percent / 100.0, 2.2);
    uint32_t duty = (uint32_t)(ORION_BL_MAX_DUTY * frac + 0.5);
    if (duty < 1) duty = 1;
    if (orion_backlight_write_duty(duty) != 0) return -1;
    s_bl_pct = percent;
    return 0;
}

int orion_backlight_get(void) { return s_bl_pct; }

int orion_backlight_off_full(void)
{
    return orion_backlight_write_duty(0);
}

/* ── Direct-framebuffer draw shim (see file header, gap #2) ─────────────── */

#define ORION_SCRATCH_BYTES 4096

static sgfx_device_t     s_dev;
static uint8_t           s_scratch[ORION_SCRATCH_BYTES];
static sgfx_driver_ops_t s_ops;              /* panel ops + our own members  */
static uint8_t*          s_fb;               /* hardware scanout buffer     */
static size_t            s_stride;           /* its row pitch, in bytes     */
static int               s_ready;
static const char*       s_panel_name = "none";
static int               s_w, s_h;

static int s_dx0, s_dy0, s_dx1, s_dy1;       /* dirty box, x1/y1 exclusive */

/* ── Rotation ─────────────────────────────────────────────────────────── */
static int s_rotation;
static int s_auto_rotation = 1;

#define ORION_ROT_STACK_MAX 4
static int s_rot_stack[ORION_ROT_STACK_MAX];
static int s_rot_stack_n;

static void apply_rotation_to_caps(void)
{
    int lw = (s_rotation & 1) ? s_h : s_w;
    int lh = (s_rotation & 1) ? s_w : s_h;
    s_dev.caps.width  = (uint16_t)lw;
    s_dev.caps.height = (uint16_t)lh;
    sgfx_reset_clip(&s_dev);
}

void orion_display_set_rotation(int rot)
{
    rot &= 3;
    if (rot == s_rotation) return;
    s_rotation = rot;
    if (s_ready) apply_rotation_to_caps();
}

int orion_display_get_rotation(void) { return s_rotation; }
void orion_display_set_auto_rotation(int enabled) { s_auto_rotation = enabled ? 1 : 0; }
int orion_display_auto_rotation_enabled(void) { return s_auto_rotation; }

void orion_display_push_rotation(int rot)
{
    if (s_rot_stack_n < ORION_ROT_STACK_MAX) s_rot_stack[s_rot_stack_n++] = s_rotation;
    orion_display_set_rotation(rot);
}

void orion_display_pop_rotation(void)
{
    if (s_rot_stack_n > 0) orion_display_set_rotation(s_rot_stack[--s_rot_stack_n]);
}

int orion_display_phys_w(void) { return s_w; }
int orion_display_phys_h(void) { return s_h; }

void orion_display_logical_to_phys(int lx, int ly, int lw, int lh,
                                   int* px, int* py, int* pw, int* ph)
{
    switch (s_rotation) {
    case 1: /* 90 CW: logical canvas is s_h x s_w */
        *px = s_w - ly - lh; *py = lx; *pw = lh; *ph = lw;
        break;
    case 2: /* 180 */
        *px = s_w - lx - lw; *py = s_h - ly - lh; *pw = lw; *ph = lh;
        break;
    case 3: /* 270 CW (90 CCW): logical canvas is s_h x s_w */
        *px = ly; *py = s_h - lx - lw; *pw = lh; *ph = lw;
        break;
    default: /* 0 */
        *px = lx; *py = ly; *pw = lw; *ph = lh;
        break;
    }
}

void orion_display_phys_to_logical(int px, int py, int* lx, int* ly)
{
    switch (s_rotation) {
    case 1: *lx = py;         *ly = s_w - px; break;
    case 2: *lx = s_w - px;   *ly = s_h - py; break;
    case 3: *lx = s_h - py;   *ly = px;       break;
    default: *lx = px;        *ly = py;       break;
    }
}

#define ORION_ROT_DEBOUNCE_MS 300

void orion_display_poll_auto_rotation(void)
{
    if (!s_auto_rotation) return;
    static int      s_candidate;
    static uint32_t s_candidate_since;

    if (s_rot_stack_n > 0) return;

    const imu_t* imu = sic_imu(0);
    if (!imu || !imu->v) return;
    sic_imu_sample_t s;
    if (imu->v->read(imu, &s) != 0) return;
    if (fabsf(s.az) > fabsf(s.ax) && fabsf(s.az) > fabsf(s.ay)) return; /* flat */

    int sample;
    if (fabsf(s.ax) > fabsf(s.ay)) {
        sample = (s.ax > 0) ? 3 : 1;
    } else {
        sample = (s.ay > 0) ? 0 : 2;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (sample != s_candidate) {
        s_candidate = sample;
        s_candidate_since = now;
    }
    if (sample == s_rotation) return;
    if (now - s_candidate_since >= ORION_ROT_DEBOUNCE_MS) {
        orion_display_set_rotation(s_candidate);
    }
}

/* This project's GUI is landscape-only (see main.c's own comment: Tab5Gui's
 * LVGL layout has one set of 1280x720 region constants, not rotation-aware
 * the way Orion's own menu system is) -- so unlike
 * orion_display_poll_auto_rotation() above (ported verbatim, unused, kept
 * for reference/future portrait support), this variant only ever commits to
 * rotation 1 or 3 (the two landscape states -- see apply_rotation_to_caps():
 * rot&1 is what selects the swapped/landscape dimensions). A portrait-ish
 * IMU reading (ax/ay says "upright") is deliberately ignored rather than
 * mapped to 0/2, since this GUI has no portrait layout to switch to -- the
 * keyboard-accessory case only ever docks the tablet in one of the two
 * landscape orientations (right-side-up or upside-down), which is exactly
 * what this fixes: attaching the keyboard in its natural grip flips which
 * landscape orientation is "up" relative to rotation 1's boot-time default.
 * Same IMU read, same debounce constant, same sample mapping as the ported
 * function above -- only the final commit is restricted. Deliberately does
 * NOT gate on s_auto_rotation (main.c turns that off specifically to keep
 * the generic 4-way function above inert); this is an independent knob. */
void orion_display_poll_auto_rotation_landscape(void)
{
    static int      s_candidate = -1;
    static uint32_t s_candidate_since;

    if (s_rot_stack_n > 0) return; /* respect push/pop pins, same as above */

    const imu_t* imu = sic_imu(0);
    if (!imu || !imu->v) return;
    sic_imu_sample_t s;
    if (imu->v->read(imu, &s) != 0) return;
    if (fabsf(s.az) > fabsf(s.ax) && fabsf(s.az) > fabsf(s.ay)) return; /* flat */

    int sample;
    if (fabsf(s.ax) > fabsf(s.ay)) {
        sample = (s.ax > 0) ? 3 : 1;
    } else {
        return; /* portrait-ish tilt -- no landscape candidate, keep current */
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (sample != s_candidate) {
        s_candidate = sample;
        s_candidate_since = now;
    }
    if (sample == s_rotation) return;
    if (now - s_candidate_since >= ORION_ROT_DEBOUNCE_MS) {
        orion_display_set_rotation(s_candidate);
        /* orion_display_set_rotation() only updates the pixel coordinate
         * mapping -- LVGL has no idea anything happened, so under
         * direct_mode nothing gets redrawn until some unrelated future
         * event (cursor blink, touch, new output) happens to trigger a
         * cycle. See gui_rotation_bridge.h for the full reasoning; this
         * is the fix for "rotation works but isn't smooth / only seems
         * to work once" -- force the corrective redraw immediately
         * instead of waiting on incidental activity to patch it up. */
        tab5gui_notify_rotation_changed();
    }
}

int orion_display_detect_boot_rotation_landscape(void)
{
    const imu_t* imu = sic_imu(0);
    if (!imu || !imu->v) return -1;
    sic_imu_sample_t s = {0};
    // Chip reads all-zero for a bit after init (not just stale) -- retry
    // until something real shows up, confirmed on real hardware.
    bool gotReal = false;
    for (int i = 0; i < 25; ++i) { // ~500ms budget
        if (imu->v->read(imu, &s) != 0) return -1;
        if (fabsf(s.ax) > 0.05f || fabsf(s.ay) > 0.05f || fabsf(s.az) > 0.05f) { gotReal = true; break; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!gotReal) return -1;
    if (fabsf(s.ax) < 0.03f && fabsf(s.ay) < 0.03f) return -1; // no tilt signal
    return (fabsf(s.ax) > fabsf(s.ay)) ? ((s.ax > 0) ? 3 : 1) : -1;
}

static void dirty_reset(void)
{
    s_dx0 = s_dy0 = 0x7fffffff;
    s_dx1 = s_dy1 = 0;
}

static void dirty_add(int x, int y, int w, int h)
{
    if (x < s_dx0) s_dx0 = x;
    if (y < s_dy0) s_dy0 = y;
    if (x + w > s_dx1) s_dx1 = x + w;
    if (y + h > s_dy1) s_dy1 = y + h;
}

static int orion_dsi_fill_rect(sgfx_device_t* d, int x, int y, int w, int h,
                               sgfx_rgba8_t c)
{
    (void)d;
    if (!s_fb) return SGFX_ERR_NOSUP;
    int px, py, pw, ph;
    orion_display_logical_to_phys(x, y, w, h, &px, &py, &pw, &ph);

    uint16_t v = (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
    for (int j = 0; j < ph; ++j) {
        uint16_t* row = (uint16_t*)(s_fb + (size_t)(py + j) * s_stride) + px;
        for (int i = 0; i < pw; ++i) row[i] = v;
    }
    dirty_add(px, py, pw, ph);
    return SGFX_OK;
}

/* set_window/write_pixels: real pixel-buffer blit support for sgfx_blit().
 * Same reasoning/derivation as fill_rect above -- see the Launchpad original
 * this was ported from for the full per-rotation derivation notes. */
static int s_win_px, s_win_py, s_win_pw, s_win_ph;
static int s_win_lw;
static int s_win_row;

static int orion_dsi_set_window(sgfx_device_t* d, int x, int y, int w, int h)
{
    (void)d;
    if (!s_fb) return SGFX_ERR_NOSUP;
    orion_display_logical_to_phys(x, y, w, h, &s_win_px, &s_win_py, &s_win_pw, &s_win_ph);
    s_win_lw = w;
    s_win_row = 0;
    dirty_add(s_win_px, s_win_py, s_win_pw, s_win_ph);
    return SGFX_OK;
}

static int orion_dsi_write_pixels(sgfx_device_t* d, const void* px, size_t count, sgfx_pixfmt_t fmt)
{
    (void)d;
    if (!s_fb) return SGFX_ERR_NOSUP;
    if (fmt != SGFX_FMT_RGB565) return SGFX_ERR_NOSUP;
    int j = s_win_row++;
    const uint16_t* src = (const uint16_t*)px;
    int n = (int)count;
    if (n > s_win_lw) n = s_win_lw;

    switch (s_rotation) {
    case 1: {
        int phys_x = s_win_px + s_win_pw - 1 - j;
        if ((unsigned)phys_x >= (unsigned)s_w) break;
        for (int i = 0; i < n; ++i) {
            int phys_y = s_win_py + i;
            if ((unsigned)phys_y >= (unsigned)s_h) continue;
            *((uint16_t*)(s_fb + (size_t)phys_y * s_stride) + phys_x) = src[i];
        }
        break;
    }
    case 2: {
        int phys_y = s_win_py + s_win_ph - 1 - j;
        if ((unsigned)phys_y >= (unsigned)s_h) break;
        uint16_t* row = (uint16_t*)(s_fb + (size_t)phys_y * s_stride);
        int base_x = s_win_px + s_win_pw - 1;
        for (int i = 0; i < n; ++i) {
            int phys_x = base_x - i;
            if ((unsigned)phys_x >= (unsigned)s_w) continue;
            row[phys_x] = src[i];
        }
        break;
    }
    case 3: {
        int phys_x = s_win_px + j;
        if ((unsigned)phys_x >= (unsigned)s_w) break;
        int base_y = s_win_py + s_win_ph - 1;
        for (int i = 0; i < n; ++i) {
            int phys_y = base_y - i;
            if ((unsigned)phys_y >= (unsigned)s_h) continue;
            *((uint16_t*)(s_fb + (size_t)phys_y * s_stride) + phys_x) = src[i];
        }
        break;
    }
    default: {
        int phys_y = s_win_py + j;
        if ((unsigned)phys_y >= (unsigned)s_h) break;
        uint16_t* row = (uint16_t*)(s_fb + (size_t)phys_y * s_stride) + s_win_px;
        for (int i = 0; i < n; ++i) row[i] = src[i];
        break;
    }
    }
    return SGFX_OK;
}

/* ── PPA (Pixel Processing Accelerator) hardware rotation ──────────────────
 * See display.h's own comment on orion_dsi_blit_rotated_ppa() for the "why":
 * the per-pixel write_pixels() rotated cases above (case 1/case 3, the ONLY
 * ones this project ever actually uses -- landscape-only, see main.c) do a
 * scattered single-pixel write per source pixel, measured at ~57ms for a
 * full 1280x720 frame. ESP32-P4's PPA does this same rotate as one DMA
 * transaction instead of ~900K individual scalar writes.
 *
 * Rotation angle mapping: PPA_SRM_ROTATION_ANGLE_* is counter-clockwise;
 * this project's s_rotation is clockwise (see orion_display_logical_to_phys()
 * -- "case 1: 90 CW", "case 3: 270 CW (90 CCW)"). 90 CW == 270 CCW and
 * 270 CW == 90 CCW, so the mapping below is deliberately "crossed", not a
 * typo. */
static ppa_client_handle_t s_ppa_client;
static bool                s_ppa_init_tried;

static bool ensure_ppa_client(void)
{
    if (s_ppa_client) return true;
    if (s_ppa_init_tried) return false; /* already failed once -- don't retry every single frame */
    s_ppa_init_tried = true;

    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    esp_err_t err = ppa_register_client(&cfg, &s_ppa_client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PPA client register failed (%d) -- falling back to the CPU rotation path", (int)err);
        s_ppa_client = NULL;
        return false;
    }
    ESP_LOGI(TAG, "PPA client ready: hardware-accelerated rotation blit enabled");
    return true;
}

int orion_dsi_blit_rotated_ppa(const void* srcBuf, int srcFullW, int srcFullH,
                                int lx, int ly, int lw, int lh)
{
    if (!s_fb || !s_ready) return SGFX_ERR_NOSUP;
    if (lw <= 0 || lh <= 0) return SGFX_OK; /* nothing to do, not an error */

    ppa_srm_rotation_angle_t angle;
    switch (s_rotation) {
    case 1: angle = PPA_SRM_ROTATION_ANGLE_270; break; /* project's 90 CW == PPA's 270 CCW */
    case 2: angle = PPA_SRM_ROTATION_ANGLE_180; break;
    case 3: angle = PPA_SRM_ROTATION_ANGLE_90;  break; /* project's 270 CW == PPA's 90 CCW */
    default: return SGFX_ERR_NOSUP; /* rotation 0: this project never actually runs landscape-only at rotation 0, let the CPU path (fast, unrotated case) handle it */
    }
    if (!ensure_ppa_client()) return SGFX_ERR_NOSUP;

    /* (lx,ly,lw,lh) is the LOGICAL dirty sub-rect (pre-rotation, LVGL's own
     * coordinate space). Reuse the exact same logical->physical mapping
     * fill_rect() above uses, so the PPA output block lands at the same
     * physical offset the CPU path would have written to. */
    int px, py, pw, ph;
    orion_display_logical_to_phys(lx, ly, lw, lh, &px, &py, &pw, &ph);

    ppa_srm_oper_config_t op = {0};
    op.in.buffer          = srcBuf;
    op.in.pic_w           = (uint32_t)srcFullW;
    op.in.pic_h           = (uint32_t)srcFullH;
    op.in.block_offset_x  = (uint32_t)lx;
    op.in.block_offset_y  = (uint32_t)ly;
    op.in.block_w         = (uint32_t)lw;
    op.in.block_h         = (uint32_t)lh;
    op.in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer         = s_fb;
    op.out.buffer_size    = (size_t)s_stride * (size_t)s_h;
    op.out.pic_w          = (uint32_t)s_w;
    op.out.pic_h          = (uint32_t)s_h;
    op.out.block_offset_x = (uint32_t)px;
    op.out.block_offset_y = (uint32_t)py;
    op.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = angle;
    op.scale_x = 1.0f;
    op.scale_y = 1.0f;
    op.mode = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_client, &op);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PPA SRM failed (%d) this frame -- caller must fall back", (int)err);
        return SGFX_ERR_NOSUP;
    }
    /* Bypassed orion_dsi_write_pixels()'s own dirty_add() calls entirely --
     * only the (px,py,pw,ph) physical sub-rect was just rewritten by the
     * DMA transfer, not the whole buffer. */
    dirty_add(px, py, pw, ph);
    return SGFX_OK;
}

static uint8_t* s_front_fb;

static int orion_dsi_present(sgfx_device_t* d)
{
    (void)d;
    if (s_dx1 <= s_dx0 || s_dy1 <= s_dy0) {
        return SGFX_OK; /* clean */
    }
    /* Real double-buffering: flip the buffer we just drew into (s_fb) to
     * become the new front/scanned buffer, synced to the panel's own vsync. */
    int rc = sgfx_hal_dsi_flip(s_dev.bus, 200);
    s_front_fb = s_fb;
    s_fb = (uint8_t*)sgfx_hal_dsi_get_back_fb(s_dev.bus, &s_stride);
    dirty_reset();
    return rc;
}

/* See display.h for the full contract. */
int orion_dsi_present_kick(void)
{
    if (s_dx1 <= s_dx0 || s_dy1 <= s_dy0) {
        return 1; /* clean: nothing dirty, no swap requested, do not wait() */
    }
    return sgfx_hal_dsi_flip_kick(s_dev.bus);
}

int orion_dsi_present_wait(uint32_t timeout_ms)
{
    int rc = sgfx_hal_dsi_flip_wait(s_dev.bus, timeout_ms);
    s_front_fb = s_fb;
    s_fb = (uint8_t*)sgfx_hal_dsi_get_back_fb(s_dev.bus, &s_stride);
    dirty_reset();
    return rc;
}

/* ── Bring-up ──────────────────────────────────────────────────────────── */

int orion_display_init(void)
{
    sgfx_hal_cfg_dsi_t cfg;
    const sgfx_driver_ops_t* ops;
    const sgfx_caps_t*       caps;

    /* Reset is deliberately -1: LCD_RST hangs off a PI4IOE5V6408 expander
     * bit and was already released by SIC's tab5_ioexp_init(), so SGFX must
     * not drive a GPIO for it. */
    cfg.lane_count = 2;
    cfg.width      = 720;
    cfg.height     = 1280;
    cfg.fb_fmt     = SGFX_FMT_RGB565;
    cfg.pin_rst    = -1;
    cfg.ldo_chan   = 3;
    cfg.ldo_mv     = 2500;
    cfg.num_fbs    = 2; /* real double-buffering, tear-free */

    tab5_panel_kind_t kind = tab5_panel_detected();
    if (kind == TAB5_PANEL_UNKNOWN) {
        ESP_LOGW(TAG, "panel UNKNOWN (SIC detect did not run or found "
                      "nothing) - assuming ILI9881C");
        kind = TAB5_PANEL_ILI9881C_GT911;
    }

    switch (kind) {
    case TAB5_PANEL_ST7121:
        s_panel_name = "ST7121";
        ops  = &sgfx_st7121_ops;  caps = &sgfx_st7121_caps_default;
        cfg.lane_mbps = 965; cfg.dpi_clk_hz = 70000000;
        cfg.hsync =  2; cfg.hbp =  40; cfg.hfp =  40;
        cfg.vsync = 20; cfg.vbp =  24; cfg.vfp = 200;
        break;
    case TAB5_PANEL_ST7123:
        s_panel_name = "ST7123";
        ops  = &sgfx_st7123_ops;  caps = &sgfx_st7123_caps_default;
        cfg.lane_mbps = 965; cfg.dpi_clk_hz = 70000000;
        cfg.hsync =  2; cfg.hbp =  40; cfg.hfp =  40;
        cfg.vsync =  2; cfg.vbp =   8; cfg.vfp = 220;
        break;
    case TAB5_PANEL_ILI9881C_GT911:
    default:
        s_panel_name = "ILI9881C";
        ops  = &sgfx_ili9881c_ops; caps = &sgfx_ili9881c_caps_default;
        cfg.lane_mbps = 730; cfg.dpi_clk_hz = 60000000;
        cfg.hsync = 40; cfg.hbp = 140; cfg.hfp =  40;
        cfg.vsync =  4; cfg.vbp =  20; cfg.vfp =  20;
        break;
    }

    ESP_LOGI(TAG, "DSI panel=%s lanes=%d %uMbps dpi=%uHz",
             s_panel_name, cfg.lane_count,
             (unsigned)cfg.lane_mbps, (unsigned)cfg.dpi_clk_hz);

    sgfx_bus_t* bus = (sgfx_bus_t*)calloc(1, sizeof(sgfx_bus_t));
    if (!bus) { ESP_LOGE(TAG, "bus alloc failed"); return -1; }

    int rc = sgfx_hal_make_dsi(bus, &cfg);
    if (rc < 0) {
        ESP_LOGE(TAG, "make_dsi rc=%d", rc);
        free(bus);
        return rc;
    }

    s_ops = *ops;
    s_ops.fill_rect    = orion_dsi_fill_rect;
    s_ops.set_window   = orion_dsi_set_window;
    s_ops.write_pixels = orion_dsi_write_pixels;
    s_ops.present      = orion_dsi_present;
    dirty_reset();

    rc = sgfx_init(&s_dev, bus, &s_ops, caps, s_scratch, sizeof s_scratch);
    if (rc) {
        ESP_LOGE(TAG, "%s init rc=%d", s_panel_name, rc);
        free(bus);
        return rc;
    }
    s_w = s_dev.caps.width;
    s_h = s_dev.caps.height;

    if (!(s_fb = (uint8_t*)sgfx_hal_dsi_get_back_fb(bus, &s_stride))) {
        ESP_LOGE(TAG, "scanout buffer unavailable");
        return -1;
    }
    if (s_stride == 0) s_stride = (size_t)s_w * 2u;

    ESP_LOGI(TAG, "%s ok %dx%d fb=%p stride=%u",
             s_panel_name, s_w, s_h, (void*)s_fb, (unsigned)s_stride);

    s_ready = 1;

    sgfx_clear(&s_dev, (sgfx_rgba8_t){0, 0, 0, 255});
    sgfx_present(&s_dev);

    if (orion_backlight_set(50) != 0)
        ESP_LOGW(TAG, "backlight init failed (GPIO%d)", ORION_BL_GPIO);
    else
        ESP_LOGI(TAG, "backlight on (GPIO%d, %d%%)", ORION_BL_GPIO, s_bl_pct);

    return 0;
}

int            orion_display_ready(void) { return s_ready; }
const char*    orion_panel_name(void)    { return s_panel_name; }
sgfx_device_t* orion_gfx(void)           { return s_ready ? &s_dev : NULL; }
int            orion_display_w(void)     { return s_dev.caps.width; }
int            orion_display_h(void)     { return s_dev.caps.height; }

void* orion_fb_ptr(size_t* stride_bytes)
{
    if (stride_bytes) *stride_bytes = s_stride;
    return s_front_fb ? s_front_fb : s_fb;
}

/* ── Boot splash ───────────────────────────────────────────────────────── */
/* The official Bit Pirate logo bitmap, same asset every board's own splash
 * uses -- centered on whatever rotation/resolution s_dev.caps already holds
 * (set by orion_display_set_rotation(), called before this in main.c). */
void orion_display_splash(void)
{
    if (!s_ready) return;
    sgfx_clear(&s_dev, (sgfx_rgba8_t){0, 0, 0, 255});

    int imgX = (s_dev.caps.width  - WELCOME_IMAGE_WIDTH)  / 2;
    int imgY = (s_dev.caps.height - WELCOME_IMAGE_HEIGHT) / 2;
    sgfx_blit(&s_dev, imgX, imgY, WELCOME_IMAGE_WIDTH, WELCOME_IMAGE_HEIGHT,
              SGFX_FMT_RGB565, WelcomeScreen, WELCOME_IMAGE_WIDTH * sizeof(uint16_t));

    sgfx_present(&s_dev);
}

/* ── Screenshot dump (debug tool, serial/JTAG console) ───────────────────
 * See display.h's own comment for the why/format. Base64 alphabet and the
 * 3-byte-in/4-char-out streaming shape are the plain RFC 4648 standard --
 * nothing project-specific, just inlined here to avoid pulling in
 * arduino-esp32's base64.cpp (returns a single ~1.6MB Arduino String,
 * which is wasteful for a one-shot debug dump we can stream instead). */
static const char kB64Table[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* buf/bufLen batch output so a ~1.2MB dump does ~1200 fwrite() calls
 * instead of ~300000 (one per 3 input bytes) -- that per-call overhead,
 * not the USB-Serial-JTAG link itself, was the real bottleneck. */
typedef struct { uint8_t carry[3]; int carry_n; int col; char buf[1024]; size_t bufLen; } b64_ctx_t;

static void b64_emit4(const uint8_t in[3], char out[4])
{
    out[0] = kB64Table[in[0] >> 2];
    out[1] = kB64Table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
    out[2] = kB64Table[((in[1] & 0x0F) << 2) | (in[2] >> 6)];
    out[3] = kB64Table[in[2] & 0x3F];
}

static void b64_flush(b64_ctx_t* ctx)
{
    if (ctx->bufLen) {
        fwrite(ctx->buf, 1, ctx->bufLen, stdout);
        ctx->bufLen = 0;
    }
}

static void b64_put(b64_ctx_t* ctx, const char* s, size_t n)
{
    if (ctx->bufLen + n > sizeof(ctx->buf)) b64_flush(ctx);
    memcpy(ctx->buf + ctx->bufLen, s, n);
    ctx->bufLen += n;
}

/* Wraps output every ~200 chars with a newline -- not required by base64
 * itself, just so a host script reading the console line-by-line doesn't
 * have to buffer one ~1.2MB unbroken line. */
static void b64_feed(b64_ctx_t* ctx, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ctx->carry[ctx->carry_n++] = data[i];
        if (ctx->carry_n == 3) {
            char out[4];
            b64_emit4(ctx->carry, out);
            b64_put(ctx, out, 4);
            ctx->carry_n = 0;
            ctx->col += 4;
            if (ctx->col >= 200) { b64_put(ctx, "\n", 1); ctx->col = 0; }
        }
    }
}

static void b64_finish(b64_ctx_t* ctx)
{
    if (ctx->carry_n > 0) {
        uint8_t in[3] = {0, 0, 0};
        for (int k = 0; k < ctx->carry_n; ++k) in[k] = ctx->carry[k];
        char out[4];
        b64_emit4(in, out);
        if (ctx->carry_n == 1) { out[2] = '='; out[3] = '='; }
        else if (ctx->carry_n == 2) { out[3] = '='; }
        b64_put(ctx, out, 4);
    }
    b64_put(ctx, "\n", 1);
    b64_flush(ctx);
}

static inline uint8_t rgb565_to_gray(uint16_t px)
{
    uint8_t r8 = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31);
    uint8_t g8 = (uint8_t)(((px >> 5)  & 0x3F) * 255 / 63);
    uint8_t b8 = (uint8_t)((px         & 0x1F) * 255 / 31);
    return (uint8_t)((r8 * 299u + g8 * 587u + b8 * 114u) / 1000u);
}

void orion_display_dump_screenshot_b64(void)
{
    if (!s_ready) { printf("SCREENSHOT: display not ready\n"); return; }

    size_t stride;
    const uint8_t* fb = (const uint8_t*)orion_fb_ptr(&stride);
    int w = orion_display_phys_w();
    int h = orion_display_phys_h();
    if (!fb || w <= 0 || h <= 0) { printf("SCREENSHOT: no framebuffer\n"); return; }

    /* BMP8 grayscale, top-down (negative height matches fb row order
     * directly, no row-reversal needed). Row stride must be a multiple
     * of 4 bytes -- true for w=720 already (180*4), no padding needed
     * on this panel, but computed generically in case that ever changes. */
    int rowBytes = (w + 3) & ~3;
    uint32_t pixelBytes = (uint32_t)rowBytes * (uint32_t)h;
    uint32_t dataOffset = 14 + 40 + 256 * 4;
    uint32_t fileSize = dataOffset + pixelBytes;

    uint8_t hdr[14 + 40 + 256 * 4];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(&hdr[2], &fileSize, 4);
    memcpy(&hdr[10], &dataOffset, 4);
    uint32_t dibSize = 40;
    memcpy(&hdr[14], &dibSize, 4);
    int32_t bw = w, bh = -h;
    memcpy(&hdr[18], &bw, 4);
    memcpy(&hdr[22], &bh, 4);
    uint16_t planes = 1, bpp = 8;
    memcpy(&hdr[26], &planes, 2);
    memcpy(&hdr[28], &bpp, 2);
    memcpy(&hdr[34], &pixelBytes, 4);
    uint32_t colorsUsed = 256;
    memcpy(&hdr[46], &colorsUsed, 4);
    for (int i = 0; i < 256; ++i) {
        uint8_t* p = &hdr[14 + 40 + i * 4];
        p[0] = p[1] = p[2] = (uint8_t)i;
    }

    printf("===SCREENSHOT_BMP_B64_BEGIN w=%d h=%d bytes=%u===\n", w, h, (unsigned)fileSize);

    b64_ctx_t ctx = {0};
    b64_feed(&ctx, hdr, sizeof(hdr));

    uint8_t* rowBuf = (uint8_t*)malloc((size_t)rowBytes);
    if (!rowBuf) { printf("SCREENSHOT: OOM\n"); return; }
    for (int y = 0; y < h; ++y) {
        const uint16_t* srcRow = (const uint16_t*)(fb + (size_t)y * stride);
        if (rowBytes > w) memset(rowBuf, 0, (size_t)rowBytes);
        for (int x = 0; x < w; ++x) rowBuf[x] = rgb565_to_gray(srcRow[x]);
        b64_feed(&ctx, rowBuf, (size_t)rowBytes);
    }
    free(rowBuf);
    b64_finish(&ctx);

    printf("===SCREENSHOT_BMP_B64_END===\n");
    fflush(stdout);
}
