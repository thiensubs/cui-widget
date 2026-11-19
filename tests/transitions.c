/* Transition self-check: drive a color transition with the virtual clock and
 * assert the rendered pixel interpolates from the start color to the end color.
 * Deterministic — no wall-clock, no sleeping. Exit 0 = pass, 1 = fail.
 *   cuiw-transitions
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[2048];

/* Build a doc: a body + one box (id 10) 100x100 with a background-color
 * transition. The box sits at CSS (16,16) so its center is well inside. */
static const char *kBuild =
    "{\"type\":\"ops\",\"ops\":["
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff;font-size:15px}"
    ".box{width:100px;height:100px;background:#ff0000;transition:background-color 100ms linear}}\"},"
    "{\"op\":\"el\",\"id\":10,\"tag\":\"div\",\"attrs\":[[\"class\",\"box\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null}"
    "]}";

static int send(cuiw_renderer_t r, const char *json) {
    if (!cuiw_send(r, json)) { fprintf(stderr, "send rejected: %s\n", json); return 0; }
    return 1;
}
static void set_clock(cuiw_renderer_t r, double now_ms) {
    snprintf(buf, sizeof buf, "{\"type\":\"clock\",\"virtual\":true,\"now\":%f}", now_ms);
    send(r, buf);
}
/* Sample the box's center pixel (CSS 66,66 -> device *dpr). */
static void sample(cuiw_renderer_t r, double dpr, int *R, int *G, int *B) {
    cuiw_frame f = cuiw_paint(r);
    if (!f.rgba) { fprintf(stderr, "no frame\n"); exit(1); }
    int x = (int)(66 * dpr), y = (int)(66 * dpr);
    const unsigned char *p = f.rgba + (size_t)y * f.stride + (size_t)x * 4;
    *R = p[0]; *G = p[1]; *B = p[2];
    cuiw_frame_free(&f);
}
static int near(int v, int want, int tol) { return abs(v - want) <= tol; }

int main(void) {
    double dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("light", 400, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }

    /* Freeze the clock at t=0 BEFORE building, so the initial style records its
     * baseline at t=0 and does not transition (CSS: no transition on first style). */
    set_clock(r, 0.0);
    if (!send(r, kBuild)) return 1;

    int R, G, B;
    sample(r, dpr, &R, &G, &B);
    printf("t=0            rgb(%3d,%3d,%3d)  want red   (255,0,0)\n", R, G, B);
    if (!near(R, 255, 8) || !near(G, 0, 8) || !near(B, 0, 8)) {
        fprintf(stderr, "FAIL: initial color is not red\n"); return 1;
    }

    /* Change the box to blue. The transition should now animate red -> blue over
     * 100ms linear. Send at t=0, then step the clock and repaint. */
    if (!send(r, "{\"type\":\"ops\",\"ops\":["
                 "{\"op\":\"style\",\"id\":10,\"name\":\"background-color\",\"value\":\"#0000ff\"}]}"))
        return 1;

    /* t=0 right after the change: still at the start color (interpolation at 0). */
    sample(r, dpr, &R, &G, &B);
    printf("t=0 (changed)  rgb(%3d,%3d,%3d)  want red   (255,0,0)\n", R, G, B);
    if (!near(R, 255, 8) || !near(G, 0, 8) || !near(B, 0, 8)) {
        fprintf(stderr, "FAIL: color jumped at t=0 instead of starting the transition\n");
        return 1;
    }

    /* t=50ms = halfway: linear, so ~50% red + 50% blue -> ~(128,0,128). */
    set_clock(r, 50.0);
    sample(r, dpr, &R, &G, &B);
    printf("t=50 (mid)     rgb(%3d,%3d,%3d)  want ~     (128,0,128)\n", R, G, B);
    if (!near(R, 128, 20) || !near(G, 0, 12) || !near(B, 128, 20)) {
        fprintf(stderr, "FAIL: mid-transition color is not halfway\n"); return 1;
    }

    /* t=100ms = done: fully blue. */
    set_clock(r, 100.0);
    sample(r, dpr, &R, &G, &B);
    printf("t=100 (done)   rgb(%3d,%3d,%3d)  want blue  (0,0,255)\n", R, G, B);
    if (!near(R, 0, 8) || !near(G, 0, 8) || !near(B, 255, 8)) {
        fprintf(stderr, "FAIL: color did not reach the end value\n"); return 1;
    }

    /* Well past the end: stays at the end value (no overshoot, run retired). */
    set_clock(r, 500.0);
    sample(r, dpr, &R, &G, &B);
    printf("t=500 (settled) rgb(%3d,%3d,%3d) want blue  (0,0,255)\n", R, G, B);
    if (!near(R, 0, 8) || !near(G, 0, 8) || !near(B, 255, 8)) {
        fprintf(stderr, "FAIL: color did not stay settled\n"); return 1;
    }

    cuiw_destroy(r);
    printf("transitions: PASS\n");
    return 0;
}