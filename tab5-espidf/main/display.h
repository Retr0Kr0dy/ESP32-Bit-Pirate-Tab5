/*
 * display.h — public API for the Tab5 MIPI-DSI bring-up in display.c.
 *
 * Ported from Tab5-Launchpad/main/display.c (itself forked from
 * Tab5-Orion). Kept under the original `orion_` prefix for this pass —
 * see display.c's header comment for why — a cosmetic rename to a
 * Bit-Pirate-specific prefix is a trivial, zero-risk cleanup for later,
 * not worth doing in the same pass as a first real-hardware bring-up.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Matches sgfx.h's own `typedef struct sgfx_device sgfx_device_t;` tag
 * name exactly -- this forward declaration must be structurally identical
 * (C requires the same tag) so callers that only need the pointer type
 * (touch_bridge.c, the Tab5*.cpp GUI stubs) don't have to pull in sgfx.h. */
typedef struct sgfx_device sgfx_device_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Bring-up. Logs progress via ESP_LOGx (dropped its konsole dependency --
 * this project's boot logging is now plain ESP_LOG, not lib/konsole, see
 * main.c). Returns 0 on success. */
int orion_display_init(void);
void orion_display_splash(void);

/* Dumps the current screen as a base64 grayscale BMP over stdout, framed by
 * "===SCREENSHOT_BMP_B64_BEGIN/END===" markers. No-op if not ready. */
void orion_display_dump_screenshot_b64(void);

int            orion_display_ready(void);
const char*    orion_panel_name(void);
sgfx_device_t* orion_gfx(void);
int            orion_display_w(void);
int            orion_display_h(void);
int            orion_display_phys_w(void);
int            orion_display_phys_h(void);

/* Rotation */
void orion_display_set_rotation(int rot);
int  orion_display_get_rotation(void);
void orion_display_set_auto_rotation(int enabled);
int  orion_display_auto_rotation_enabled(void);
void orion_display_push_rotation(int rot);
void orion_display_pop_rotation(void);
void orion_display_poll_auto_rotation(void);
/* Landscape-only variant actually used by this project -- see display.c's
 * comment at its definition. */
void orion_display_poll_auto_rotation_landscape(void);
/* One-shot IMU read for choosing the BOOT-time rotation (1 or 3) before the
 * screen ever draws anything -- same sampling logic as
 * orion_display_poll_auto_rotation_landscape(), minus its debounce/candidate
 * tracking (nothing to debounce against on the very first read). Returns 1
 * or 3 on a confident landscape reading, or -1 if the IMU is unavailable,
 * the board is lying flat, or tilted portrait-ish -- callers should fall
 * back to a fixed default (rotation 1) only in that -1 case. */
int orion_display_detect_boot_rotation_landscape(void);
void orion_display_logical_to_phys(int lx, int ly, int lw, int lh,
                                    int *px, int *py, int *pw, int *ph);
void orion_display_phys_to_logical(int px, int py, int *lx, int *ly);

/* Backlight */
int  orion_backlight_set(int percent);
int  orion_backlight_get(void);
int  orion_backlight_off_full(void);

/* Diagnostics */
void*  orion_fb_ptr(size_t *stride_bytes);

/* Split scanout present: kick requests one swap (0: started, 1: no
 * dirty frame, negative: error). After a successful kick, call wait()
 * exactly once before another kick. Current LVGL caller waits synchronously
 * while holding its lock to avoid the async-flush deadlock. */
int orion_dsi_present_kick(void);
int orion_dsi_present_wait(uint32_t timeout_ms);

/* Hardware-accelerated replacement for the CPU per-pixel rotation loop
 * orion_dsi_write_pixels() otherwise does (see display.c's own comment on
 * that function) -- measured at ~57ms for a full 1280x720 RGB565 frame on
 * real hardware, the dominant cost in the whole display pipeline (the vsync
 * wait itself is under 15ms). ESP32-P4 has a dedicated Pixel Processing
 * Accelerator (PPA) peripheral for exactly this: DMA-driven rotate, not a
 * CPU scalar loop.
 *
 * Blits just the (lx,ly,lw,lh) sub-rectangle of `srcBuf` (a tightly-packed
 * RGB565 buffer, srcFullW x srcFullH, LOGICAL/pre-rotation orientation) into
 * the physical scanout buffer, applying the CURRENT rotation in hardware --
 * pass (0,0,srcFullW,srcFullH) for a full-frame blit. Blit cost scales with
 * area, so the caller should pass the real dirty rect, not always the
 * whole screen.
 *
 * Returns SGFX_OK on success. Returns SGFX_ERR_NOSUP if the PPA client
 * couldn't be initialized (tried once, cached) or the current rotation
 * isn't 1/2/3 -- callers MUST fall back to the existing CPU blit path in
 * that case, never treat this as a hard failure. srcBuf must be allocated
 * with MALLOC_CAP_DMA (PPA reads it via DMA). */
int orion_dsi_blit_rotated_ppa(const void* srcBuf, int srcFullW, int srcFullH,
                                int lx, int ly, int lw, int lh);

#ifdef __cplusplus
}
#endif
