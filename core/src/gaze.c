/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/gaze.h"

#include <math.h>

oc_calib oc_calib_compute(oc_point tl, oc_point tr, oc_point bl, oc_point br)
{
    oc_calib c = { 0 };

    double left_x   = (tl.x + bl.x) / 2.0;
    double right_x  = (tr.x + br.x) / 2.0;
    double top_y    = (tl.y + tr.y) / 2.0;
    double bottom_y = (bl.y + br.y) / 2.0;

    double range_x = right_x - left_x; /* negative on a mirrored feed -- fine */
    double range_y = bottom_y - top_y;

    if (fabs(range_x) < OC_MIN_CALIB_SPAN || fabs(range_y) < OC_MIN_CALIB_SPAN) {
        c.valid = false;
        return c;
    }

    c.valid = true;
    c.min_x = left_x;
    c.min_y = top_y;
    c.range_x = range_x;
    c.range_y = range_y;
    c.pad_x = range_x * 0.2;
    c.pad_y = range_y * 0.2;
    return c;
}

static void map_padded(const oc_calib *cal, bool mirror_x,
                       double nx, double ny, double *mx, double *my)
{
    if (cal && cal->valid) {
        *mx = (nx - (cal->min_x - cal->pad_x)) / (cal->range_x + cal->pad_x * 2.0);
        *my = (ny - (cal->min_y - cal->pad_y)) / (cal->range_y + cal->pad_y * 2.0);
        return;
    }
    *mx = mirror_x ? 1.0 - nx : nx;
    *my = ny;
}

oc_point oc_gaze_position(const oc_calib *cal, bool mirror_x, double nx, double ny)
{
    oc_point p;
    map_padded(cal, mirror_x, nx, ny, &p.x, &p.y);
    /* Clamped, unlike the quadrant mapping, which has no need of it. This one
     * is a place on the screen: a patient who turns further than they did
     * during calibration should see the pointer pinned to the edge, not
     * vanished off it. */
    if (p.x < 0.0) p.x = 0.0; else if (p.x > 1.0) p.x = 1.0;
    if (p.y < 0.0) p.y = 0.0; else if (p.y > 1.0) p.y = 1.0;
    return p;
}

int oc_gaze_quadrant(const oc_calib *cal, bool mirror_x, double nx, double ny)
{
    double mx, my;
    map_padded(cal, mirror_x, nx, ny, &mx, &my);

    if (mx < 0.5 && my < 0.5) return 0; /* top-left */
    if (mx > 0.5 && my < 0.5) return 1; /* top-right */
    if (mx < 0.5 && my > 0.5) return 2; /* bottom-left */
    return 3;                           /* bottom-right */
}

bool oc_gaze_confident_switch(const oc_calib *cal, bool mirror_x,
                              double nx, double ny, double dead)
{
    double mx, my;
    if (cal && cal->valid) {
        mx = (nx - cal->min_x) / cal->range_x; /* unpadded, deliberately */
        my = (ny - cal->min_y) / cal->range_y;
    } else {
        mx = mirror_x ? 1.0 - nx : nx;
        my = ny;
    }
    return (mx > 0.5 + dead || mx < 0.5 - dead) &&
           (my > 0.5 + dead || my < 0.5 - dead);
}

double oc_smooth_step(double current, double target)
{
    return current + OC_SMOOTH * (target - current);
}
