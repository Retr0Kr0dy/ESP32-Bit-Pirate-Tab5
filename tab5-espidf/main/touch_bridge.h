/*
 * touch_bridge.h — thin C wrapper over SIC's sic_touch(0), callable from
 * the Arduino-as-component C++ task without that task needing to include
 * any SIC headers directly.
 *
 * Wires through to SIC's real touch driver and converts physical panel
 * coordinates to logical (rotation-aware) coordinates via display.h's
 * orion_display_phys_to_logical(). Tab5TouchInput is the real caller.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x, y;   /* logical (rotation-aware) coordinates */
    uint8_t  id;
    uint8_t  pressed;
} bp_touch_point_t;

/* Returns the number of active contacts written to out (0..max_points), or
 * <0 on error (e.g. no touch driver registered). */
int bp_touch_read_points(bp_touch_point_t* out, int max_points);

#ifdef __cplusplus
}
#endif
