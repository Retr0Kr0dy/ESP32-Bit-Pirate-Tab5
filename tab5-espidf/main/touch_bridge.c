#include "touch_bridge.h"

#include "sic/sic.h"
#include "display.h"

int bp_touch_read_points(bp_touch_point_t* out, int max_points)
{
    const touch_t* t = sic_touch(0);
    if (!t || !t->v || !t->v->read_points) return -1;

    sic_touch_point_t raw[8];
    int n = max_points;
    if (n > 8) n = 8;
    int rc = t->v->read_points(t, raw, n);
    if (rc < 0) return rc;

    for (int i = 0; i < rc; ++i) {
        int lx, ly;
        orion_display_phys_to_logical(raw[i].x, raw[i].y, &lx, &ly);
        out[i].x = (uint16_t)lx;
        out[i].y = (uint16_t)ly;
        out[i].id = raw[i].id;
        out[i].pressed = raw[i].pressed;
    }
    return rc;
}
