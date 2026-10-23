/*
 * kbd_accessory.h — driver for the real M5Stack Tab5 keyboard accessory.
 *
 * Orion app-layer, not SIC: the accessory is a custom STM32F030C8T6
 * controller (I2C addr 0x6D, on the 2x4 GPIO header — TAB5_KBD_I2C_BUS,
 * see boards/tab5/ioexpander.h) speaking its own device-side debounced
 * press/release FIFO protocol, with a 5-row x 14-col (70-key) physical
 * layout — too many keys for SIC's generic kscan_t 64-bit state bitmap, and
 * a poor fit for its poll-and-diff model anyway since the chip already
 * delivers discrete edge events rather than a raw matrix to scan. See
 * board_tab5.c's keyboard-accessory comment for the full rationale; this
 * is the same "doesn't fit SIC/SGFX's registry shape -> Orion app layer"
 * precedent already used for USB HID and Wi-Fi/BT.
 *
 * Register map and physical key layout sourced from M5Stack's official
 * protocol doc (Tab5_Keyboard-I2C-Protocol-EN-V1.0.pdf) and
 * M5Tab5-Keyboard-UserDemo's own reference driver/keymap
 * (github.com/m5stack/M5Tab5-Keyboard-UserDemo).
 */
#pragma once

#include <stdint.h>
#include "sic/hal.h"  /* sic_key_event_t, sic_keycode_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the keyboard's I2C bus (idempotent — safe even if main.c already
 * opened TAB5_KBD_I2C_BUS), verifies the chip answers, and selects Normal
 * mode (raw row/col press/release events, register 0x10 = 0). Returns 0 on
 * success, negative on failure (chip absent/unresponsive). */
int orion_kbd_init(void);

/* True once orion_kbd_init() has succeeded. */
int orion_kbd_ready(void);

/* Polls for one key event. Returns 1 and fills *ev on a real event, 0 if
 * the device's queue is currently empty, <0 on I2C failure or if
 * orion_kbd_init() was never called/failed. Non-blocking — callers loop
 * this themselves (matches sic_key_poll()'s contract, so cmd_kbd's existing
 * printing code needs no changes beyond the call site). */
int orion_kbd_poll(sic_key_event_t* ev);

/* LED controls (Settings app). Zone 0 is the keystroke-reaction LED,
 * zone 1 the link-health heartbeat (see
 * kbd_accessory.c's own header comment for what each normally does).
 * Disabling either forces that zone off immediately, not just on its next
 * would-be update, and apply_led()/update_heartbeat() skip touching it
 * again while disabled. Brightness (REG_BRIGHTNESS) is a single register
 * shared by both zones in this chip's protocol -- there is no per-zone
 * brightness to expose, so one getter/setter covers both, matching what
 * the hardware actually supports rather than faking independent control. */
void orion_kbd_set_keystroke_led(int enabled);
int  orion_kbd_get_keystroke_led(void);
void orion_kbd_set_heartbeat_led(int enabled);
int  orion_kbd_get_heartbeat_led(void);
void orion_kbd_set_led_brightness(int pct); /* 0-100 */
int  orion_kbd_get_led_brightness(void);

/* Raw zone-0 override, for idle sleep's charging indicator (repurposing
 * the keystroke-reaction LED while asleep, since nothing is being typed
 * then). Deliberately bypasses the keystroke-LED
 * enabled flag above -- this is a different feature (charge status, not
 * keystroke reaction) reusing the same physical zone, not a keystroke
 * event, so it shouldn't be silenced by a user having turned that feature
 * off. Safe to call repeatedly (zone_set() itself no-ops on an unchanged
 * color) -- callers driving a blink pattern should call this on their own
 * timer. A real key press during sleep would still call apply_led() and
 * overwrite this, but orion_touchmenu_run()'s sleep loop never forwards
 * key events while asleep, so that's not a real conflict in practice. */
void orion_kbd_set_zone0_raw(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
