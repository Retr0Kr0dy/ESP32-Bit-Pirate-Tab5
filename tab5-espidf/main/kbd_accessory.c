/*
 * kbd_accessory.c — driver for the real M5Stack Tab5 keyboard accessory.
 *
 * See kbd_accessory.h for the architecture rationale (Orion app-layer, not
 * SIC). Register map and 5x14 physical key layout are transcribed from
 * M5Stack's official protocol doc (Tab5_Keyboard-I2C-Protocol-EN-V1.0.pdf)
 * and M5Tab5-Keyboard-UserDemo's own reference source
 * (components/m5_tab5_keyboard_component + main/ui_pages/.../
 * m5tab5_keyboard_def.h's tab5_key_map[]).
 *
 * Runs a background FreeRTOS task that continuously drains the chip's
 * event FIFO and drives the RGB LED in real time -- not just while a
 * console command happens to be polling. Decoded events are pushed into a
 * queue; orion_kbd_poll() is a thin non-blocking consumer of that queue, so
 * the LED reacts to every keystroke regardless of whether anything is
 * actually reading events (matches the "keyboard should light up when
 * used" expectation, same as e.g. the camera preview's background capture
 * task decouples capture from display).
 *
 * LED behaviour -- the accessory has two RGB
 * zones (RGB1/RGB2 in the protocol's 7-byte color window), each driven
 * independently for two different jobs:
 *
 *   Zone 0 (RGB1) -- keystroke reaction:
 *   - a modifier (Ctrl/Alt/Aa-shift/Sym) held down: LED goes solid, a
 *     freshly random color, for as long as it's held.
 *   - a plain key pressed with no modifier held: LED blinks that same
 *     random-color-while-held pattern, off on release.
 *   - a plain key pressed *while* a modifier is held: LED switches to a
 *     new random color for the duration of that key, then reverts to the
 *     modifier's own color on release -- off only happens when the
 *     modifier itself is released.
 *
 *   Zone 1 (RGB2) -- link heartbeat, reflecting the background poll task's
 *   own I2C health (consecutive EVENT_NUM read failures against the real
 *   chip, not a proxy for anything else in the system):
 *   - OK (green blink): reads are succeeding normally.
 *   - WARN (orange blink): a read just failed, or one failed recently
 *     (sticky for a few seconds so a single blip stays visible instead of
 *     flickering past unnoticed).
 *   - ERROR (solid red): sustained consecutive failures -- the chip has
 *     effectively gone silent (unplugged, crashed, bus fault).
 *
 * Both zones flash white together once at boot -- a deliberate "Orion has
 * taken control" signal, distinct from the chip's own power-on self-test
 * blink which happens before Orion ever talks to it.
 */
#include "kbd_accessory.h"
/* Ported into ESP32-Bit-Pirate-P4-Tab5 from Tab5-Orion's main/kbd_accessory.c
 * (verbatim register map, keymap and LED logic). Orion's app-stall "canary"
 * input to the heartbeat LED is dropped here -- this project has no
 * equivalent watchdog/canary system, and the LED is a nice-to-have, not a
 * feature anything depends on. update_heartbeat() below now reflects pure
 * I2C link health only (sustained read failures), not app liveness. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "sic/bus/i2c_bus.h"
#include "boards/tab5/ioexpander.h"  /* TAB5_KBD_I2C_* */

#define KBD_I2C_ADDR 0x6D

/* Register map (Tab5_Keyboard-I2C-Protocol-EN-V1.0.pdf). */
#define REG_INT_CFG       0x00
#define REG_INT_STA       0x01
#define REG_EVENT_NUM     0x02
#define REG_BRIGHTNESS    0x03
#define REG_KEYBOARD_MODE 0x10
#define REG_RGB_MODE      0x11
#define REG_KEY_EVENT     0x20  /* Normal-mode event: [7]=press [6:4]=row [3:0]=col */
#define REG_RGB_COLOR     0x60  /* 7-byte window: B,G,R,reserved,B,G,R (RGB1,RGB2) */

#define KB_MODE_NORMAL   0
#define RGB_MODE_CUSTOM  1
#define KBD_LED_BRIGHTNESS 5   /* 0-100; minimal by default (user preference) --
                                 * Orion's own default was 20, "bumped for
                                 * visibility" to 80 in its UI; this project
                                 * deliberately goes the other way. */

static int reg_read(uint8_t reg, uint8_t* val)
{
    return sic_i2c_writeread(TAB5_KBD_I2C_BUS, KBD_I2C_ADDR, &reg, 1, val, 1);
}
static int reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return sic_i2c_write(TAB5_KBD_I2C_BUS, KBD_I2C_ADDR, buf, 2);
}

/* Two independently-tracked zones -- [RGB1_B,RGB1_G,RGB1_R,reserved(0),
 * RGB2_B,RGB2_G,RGB2_R] is one 7-byte window, so setting one zone without
 * disturbing the other means resending both from software-tracked state
 * rather than a real read-modify-write. */
static uint8_t s_zone[2][3]; /* [zone][r,g,b] */

static void zone_flush(void)
{
    uint8_t buf[8] = {
        REG_RGB_COLOR,
        s_zone[0][2], s_zone[0][1], s_zone[0][0], 0,
        s_zone[1][2], s_zone[1][1], s_zone[1][0],
    };
    (void)sic_i2c_write(TAB5_KBD_I2C_BUS, KBD_I2C_ADDR, buf, sizeof buf);
}

static void zone_set(int zone, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_zone[zone][0] == r && s_zone[zone][1] == g && s_zone[zone][2] == b) return;
    s_zone[zone][0] = r; s_zone[zone][1] = g; s_zone[zone][2] = b;
    zone_flush();
}

static void rgb_pick_random(uint8_t* r, uint8_t* g, uint8_t* b)
{
    uint32_t v = esp_random();
    /* Bias each channel into [64,255] so colors stay visibly lit rather
     * than landing near-black. */
    *r = (uint8_t)(64 + ((v >>  0) & 0xFF) % 192);
    *g = (uint8_t)(64 + ((v >>  8) & 0xFF) % 192);
    *b = (uint8_t)(64 + ((v >> 16) & 0xFF) % 192);
}

/* ---- physical keymap: 5 rows x 14 cols, index = row*14+col ------------- */

enum { MOD_NONE = 0, MOD_CTRL, MOD_ALT, MOD_SHIFT, MOD_SYM };

typedef struct {
    char lower;   /* ascii for plain / Mode-0; also used as the sole ascii
                    * for named control keys (ESC/TAB/BS/ENT/SPACE/DEL) */
    char upper;   /* ascii with Aa (shift) held */
    char sym;     /* ascii with SYM held; 0 = no override, falls back to lower */
    sic_keycode_t code; /* explicit for named/arrow keys; SIC_KEY_NONE for
                          * plain character keys (derived from ascii instead) */
    uint8_t mod;  /* MOD_* if this position is a modifier key itself */
} kbd_key_def_t;

#define K(lo, up, sy, code)      { lo, up, sy, code, MOD_NONE }
#define KMOD(m)                  { 0, 0, 0, SIC_KEY_NONE, m }

static const kbd_key_def_t g_keymap[70] = {
    /* Row 0 */
    K(0x1B, 0x1B, 0, SIC_KEY_ESC),      /* ESC */
    K('1','1',0, SIC_KEY_NONE), K('2','2',0, SIC_KEY_NONE),
    K('3','3',0, SIC_KEY_NONE), K('4','4',0, SIC_KEY_NONE),
    K('5','5',0, SIC_KEY_NONE), K('6','6',0, SIC_KEY_NONE),
    K('7','7',0, SIC_KEY_NONE), K('8','8',0, SIC_KEY_NONE),
    K('9','9',0, SIC_KEY_NONE), K('0','0',0, SIC_KEY_NONE),
    K('-','-',0, SIC_KEY_NONE), K('+','+',0, SIC_KEY_NONE),
    K(0x7F,0x7F,0, SIC_KEY_DEL),        /* DEL */

    /* Row 1 */
    K('`','`','~', SIC_KEY_NONE), K('!','!','?', SIC_KEY_NONE),
    K('@','@',0, SIC_KEY_NONE),   K('#','#',0, SIC_KEY_NONE),
    K('$','$',0, SIC_KEY_NONE),   K('%','%',0, SIC_KEY_NONE),
    K('^','^',0, SIC_KEY_NONE),   K('&','&',0, SIC_KEY_NONE),
    K('*','*','/', SIC_KEY_NONE), K('(','(','<', SIC_KEY_NONE),
    K(')',')','>', SIC_KEY_NONE), K('[','[','{', SIC_KEY_NONE),
    K(']',']','}', SIC_KEY_NONE), K('\\','\\','|', SIC_KEY_NONE),

    /* Row 2 */
    K(0x09,0x09,0, SIC_KEY_TAB),        /* TAB */
    K('q','Q',0, SIC_KEY_NONE), K('w','W',0, SIC_KEY_NONE),
    K('e','E',0, SIC_KEY_NONE), K('r','R',0, SIC_KEY_NONE),
    K('t','T',0, SIC_KEY_NONE), K('y','Y',0, SIC_KEY_NONE),
    K('u','U',0, SIC_KEY_NONE), K('i','I',0, SIC_KEY_NONE),
    K('o','O',0, SIC_KEY_NONE), K('p','P',0, SIC_KEY_NONE),
    K(';',';',':', SIC_KEY_NONE), K('\'','\'','"', SIC_KEY_NONE),
    K(0x08,0x08,0, SIC_KEY_BACKSPACE),  /* BS */

    /* Row 3 */
    KMOD(MOD_SYM),                      /* SYM */
    KMOD(MOD_SHIFT),                    /* Aa */
    K('a','A',0, SIC_KEY_NONE), K('s','S',0, SIC_KEY_NONE),
    K('d','D',0, SIC_KEY_NONE), K('f','F',0, SIC_KEY_NONE),
    K('g','G',0, SIC_KEY_NONE), K('h','H',0, SIC_KEY_NONE),
    K('j','J',0, SIC_KEY_NONE), K('k','K',0, SIC_KEY_NONE),
    K('l','L',0, SIC_KEY_NONE),
    K(0,0,0, SIC_KEY_UP),                /* UP */
    K('_','_','=', SIC_KEY_NONE),
    K(0x0D,0x0D,0, SIC_KEY_ENTER),       /* ENT */

    /* Row 4 */
    KMOD(MOD_CTRL),                     /* CTRL */
    KMOD(MOD_ALT),                      /* ALT */
    K('z','Z',0, SIC_KEY_NONE), K('x','X',0, SIC_KEY_NONE),
    K('c','C',0, SIC_KEY_NONE), K('v','V',0, SIC_KEY_NONE),
    K('b','B',0, SIC_KEY_NONE), K('n','N',0, SIC_KEY_NONE),
    K('m','M',0, SIC_KEY_NONE),
    K('.','.',',' , SIC_KEY_NONE),
    K(0,0,0, SIC_KEY_LEFT),              /* LEFT */
    K(0,0,0, SIC_KEY_DOWN),              /* DOWN */
    K(0,0,0, SIC_KEY_RIGHT),             /* RIGHT */
    K(0x20,0x20,0, SIC_KEY_SPACE),       /* SPACE */
};

#undef K
#undef KMOD

static int s_ready = 0;
static uint8_t s_ctrl_held, s_alt_held, s_shift_held, s_sym_held;

static QueueHandle_t s_evq;
static TaskHandle_t  s_task;

/* LED state, owned entirely by kbd_poll_task -- no locking needed since
 * it's the sole writer. */
static uint8_t s_mod_active_count;
static uint8_t s_plain_held_count;
static uint8_t s_mod_r, s_mod_g, s_mod_b;

static char ctrlify(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 1);
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 1);
    return c;
}

static int decode_event(uint8_t raw, sic_key_event_t* ev)
{
    int pressed = (raw & 0x80) ? 1 : 0;
    int row = (raw >> 4) & 0x07;
    int col = raw & 0x0F;
    int idx = row * 14 + col;
    if (idx < 0 || idx >= 70) return 0;

    const kbd_key_def_t* k = &g_keymap[idx];

    memset(ev, 0, sizeof *ev);
    ev->pressed = (uint8_t)pressed;

    if (k->mod != MOD_NONE) {
        switch (k->mod) {
        case MOD_CTRL:  s_ctrl_held  = (uint8_t)pressed; ev->code = SIC_KEY_CTRL;  break;
        case MOD_ALT:   s_alt_held   = (uint8_t)pressed; ev->code = SIC_KEY_ALT;   break;
        case MOD_SHIFT: s_shift_held = (uint8_t)pressed; ev->code = SIC_KEY_SHIFT; break;
        case MOD_SYM:   s_sym_held   = (uint8_t)pressed; ev->code = SIC_KEY_FN;    break;
        default: break;
        }
        ev->ascii = 0;
    } else {
        char c = (s_sym_held && k->sym) ? k->sym : (s_shift_held ? k->upper : k->lower);
        if (s_ctrl_held && c && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            c = ctrlify(c);
        }
        ev->ascii = c;
        ev->code  = (k->code != SIC_KEY_NONE) ? k->code
                    : (c ? (sic_keycode_t)(unsigned char)c : SIC_KEY_NONE);
    }

    ev->shift = s_shift_held;
    ev->ctrl  = s_ctrl_held;
    ev->alt   = s_alt_held;
    ev->fn    = s_sym_held;

    return 1;
}

/* LED enable state + shared brightness -- see kbd_accessory.h's own
 * comment on why brightness is one shared control, not per-zone. */
static volatile int s_keystroke_led_enabled = 1;
static volatile int s_heartbeat_led_enabled = 1;
static int          s_led_brightness = KBD_LED_BRIGHTNESS;

void orion_kbd_set_keystroke_led(int enabled)
{
    enabled = enabled ? 1 : 0;
    if (enabled == s_keystroke_led_enabled) return;
    s_keystroke_led_enabled = enabled;
    if (!enabled) zone_set(0, 0, 0, 0); /* force off now, don't wait for the next keystroke */
}
int orion_kbd_get_keystroke_led(void) { return s_keystroke_led_enabled; }

void orion_kbd_set_heartbeat_led(int enabled)
{
    enabled = enabled ? 1 : 0;
    if (enabled == s_heartbeat_led_enabled) return;
    s_heartbeat_led_enabled = enabled;
    if (!enabled) zone_set(1, 0, 0, 0);
}
int orion_kbd_get_heartbeat_led(void) { return s_heartbeat_led_enabled; }

void orion_kbd_set_led_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_led_brightness = pct;
    (void)reg_write(REG_BRIGHTNESS, (uint8_t)pct);
}
int orion_kbd_get_led_brightness(void) { return s_led_brightness; }

void orion_kbd_set_zone0_raw(uint8_t r, uint8_t g, uint8_t b)
{
    zone_set(0, r, g, b);
}

static void apply_led(const sic_key_event_t* ev)
{
    if (!s_keystroke_led_enabled) return;

    int is_mod = (ev->code == SIC_KEY_CTRL || ev->code == SIC_KEY_ALT ||
                  ev->code == SIC_KEY_SHIFT || ev->code == SIC_KEY_FN);

    if (is_mod) {
        if (ev->pressed) {
            if (s_mod_active_count == 0) {
                rgb_pick_random(&s_mod_r, &s_mod_g, &s_mod_b);
                zone_set(0, s_mod_r, s_mod_g, s_mod_b);
            }
            s_mod_active_count++;
        } else {
            if (s_mod_active_count > 0) s_mod_active_count--;
            if (s_mod_active_count == 0) {
                zone_set(0, 0, 0, 0);
            }
        }
    } else {
        if (ev->pressed) {
            s_plain_held_count++;
            uint8_t r, g, b;
            rgb_pick_random(&r, &g, &b);
            zone_set(0, r, g, b);
        } else {
            if (s_plain_held_count > 0) s_plain_held_count--;
            if (s_mod_active_count > 0) {
                zone_set(0, s_mod_r, s_mod_g, s_mod_b);
            } else {
                zone_set(0, 0, 0, 0);
            }
        }
    }
}

/* ---- heartbeat (zone 1): reflects the I2C link's own health ------------ */

typedef enum { HB_OK = 0, HB_WARN, HB_ERROR } hb_state_t;

#define HB_ERROR_CONSEC    50               /* sustained failures -> solid red */
#define HB_WARN_STICKY_US  (3 * 1000 * 1000) /* stay orange this long after a blip */
#define HB_BLINK_PERIOD_US (500 * 1000)

static uint32_t s_i2c_fail_count;
static int64_t  s_last_fail_us = INT64_MIN / 2; /* far in the past at boot */
static int      s_hb_on;
static int64_t  s_hb_last_toggle_us;

static void update_heartbeat(int read_ok)
{
    int64_t now = esp_timer_get_time();

    if (read_ok) {
        s_i2c_fail_count = 0;
    } else {
        s_i2c_fail_count++;
        s_last_fail_us = now;
    }

    hb_state_t state;
    if (s_i2c_fail_count >= HB_ERROR_CONSEC) {
        state = HB_ERROR;
    } else if (s_i2c_fail_count > 0 || (now - s_last_fail_us) < HB_WARN_STICKY_US) {
        state = HB_WARN;
    } else {
        state = HB_OK;
    }

    /* Link-health bookkeeping above still runs unconditionally even while
     * the LED itself is disabled (s_i2c_fail_count/canary tracking is real,
     * independently useful state, not just LED input) -- only the actual
     * zone_set() calls below are skipped. */
    if (!s_heartbeat_led_enabled) return;

    if (state == HB_ERROR) {
        zone_set(1, 255, 0, 0);
        return;
    }

    if (now - s_hb_last_toggle_us >= HB_BLINK_PERIOD_US) {
        s_hb_on = !s_hb_on;
        s_hb_last_toggle_us = now;
        if (s_hb_on) {
            if (state == HB_WARN) zone_set(1, 255, 140, 0);
            else                  zone_set(1, 0, 200, 0);
        } else {
            zone_set(1, 0, 0, 0);
        }
    }
}

static void kbd_poll_task(void* arg)
{
    (void)arg;
    for (;;) {
        uint8_t count = 0;
        int rc = reg_read(REG_EVENT_NUM, &count);
        update_heartbeat(rc >= 0);

        if (rc >= 0 && count > 0) {
            uint8_t raw = 0;
            if (reg_read(REG_KEY_EVENT, &raw) >= 0 && raw != 0xFF) {
                sic_key_event_t ev;
                if (decode_event(raw, &ev)) {
                    apply_led(&ev);
                    (void)xQueueSend(s_evq, &ev, 0); /* drop if consumer is behind */
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

int orion_kbd_init(void)
{
    s_ready = 0;
    s_ctrl_held = s_alt_held = s_shift_held = s_sym_held = 0;
    s_mod_active_count = s_plain_held_count = 0;

    /* Idempotent: main.c already opens this bus, but guard against
     * being called standalone (e.g. a future re-init command). */
    (void)sic_i2c_begin_bus(TAB5_KBD_I2C_BUS, TAB5_KBD_I2C_SDA,
                            TAB5_KBD_I2C_SCL, TAB5_KBD_I2C_HZ);

    uint8_t val = 0;
    if (reg_read(REG_INT_CFG, &val) < 0) return -1;

    if (reg_write(REG_KEYBOARD_MODE, KB_MODE_NORMAL) < 0) return -1;
    (void)reg_write(REG_EVENT_NUM, 0); /* clear any stale FIFO from boot */
    (void)reg_write(REG_RGB_MODE, RGB_MODE_CUSTOM);
    /* Applied here, at the earliest point this driver touches the chip at
     * all -- no settings-persistence layer in this project (unlike Orion's
     * own orion_settings_load_sd(), not ported here), so KBD_LED_BRIGHTNESS
     * above is the one and only source of truth for boot-time brightness. */
    (void)reg_write(REG_BRIGHTNESS, (uint8_t)s_led_brightness);

    /* Deliberate "Orion is ready" flash on both zones -- distinct from the
     * chip's own power-on self-test blink, which already happened before
     * this. The heartbeat task takes over zone 1 immediately afterward. */
    zone_set(0, 255, 255, 255);
    zone_set(1, 255, 255, 255);
    vTaskDelay(pdMS_TO_TICKS(150));
    zone_set(0, 0, 0, 0);
    zone_set(1, 0, 0, 0);

    if (!s_evq) s_evq = xQueueCreate(32, sizeof(sic_key_event_t));
    if (!s_evq) return -1;

    s_ready = 1;

    if (!s_task) {
        /* Pinned to CPU1, matching every other background I/O task in this
         * firmware (usb_lib_task/usb_client_task, the video pipeline's
         * reader/presenter/audio tasks, doom_audio_task) -- this poll loop
         * runs continuously in the background regardless of active screen
         * (device-wide keyboard LED heartbeat), including during H.264
         * playback, and was the one remaining unpinned task that could land
         * on CPU0 and contend with the main decode loop there.
         *
         * Priority is deliberately 2, BELOW every real CPU1 AV task --
         * vp_audio_decode_task=3 is the lowest of them (video reader=5,
         * presenter=6, audio output=10, usb_client=5). First attempt used
         * priority 5 (tied with the video reader, above audio decode): fixed
         * video fps by moving this task off CPU0, but promptly starved
         * vp_audio_decode_task instead -- same contention bug, just relocated
         * onto whatever CPU1 task happened to sit below it. Priority 2 keeps
         * this pinned to CPU1 (so it still never touches decode on CPU0) but
         * guarantees it only runs in CPU1's genuine idle gaps, never at the
         * expense of any AV-pipeline task. */
        xTaskCreatePinnedToCore(kbd_poll_task, "kbd_poll", 3072, NULL, 2, &s_task, 1);
    }
    return 0;
}

int orion_kbd_ready(void)
{
    return s_ready;
}

int orion_kbd_poll(sic_key_event_t* ev)
{
    if (!ev) return -1;
    if (!s_ready) return -1;
    return (xQueueReceive(s_evq, ev, 0) == pdTRUE) ? 1 : 0;
}
