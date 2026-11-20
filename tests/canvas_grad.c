/* Canvas gradient fill self-check: a gradient fillStyle paints a fillRect as a
 * texture that runs from one stop colour to the other across the rect. Asserts
 * the left edge is near the first stop and the right edge near the last.
 * Deterministic. Exit 0 = pass.   cuiw-canvas-grad
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>

static char buf[4096];

static const char *kBuild =
    "{\"type\":\"ops\",\"ops\":["
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff}"
    "canvas{width:200px;height:100px;background:#ffffff}}\"},"
    "{\"op\":\"el\",\"id\":10,\"tag\":\"canvas\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null}"
    "]}";

static int send(cuiw_renderer_t r, const char *json) {
    if (!cuiw_send(r, json)) { fprintf(stderr, "send rejected: %s\n", json); return 0; }
    return 1;
}
static void sample(cuiw_renderer_t r, double dpr, double cx, double cy,
                   int *R, int *G, int *B) {
    cuiw_frame f = cuiw_paint(r);
    if (!f.rgba) { fprintf(stderr, "no frame\n"); exit(1); }
    int x = (int)(cx * dpr), y = (int)(cy * dpr);
    const unsigned char *p = f.rgba + (size_t)y * f.stride + (size_t)x * 4;
    *R = p[0]; *G = p[1]; *B = p[2];
    cuiw_frame_free(&f);
}
static int near(int v, int want, int tol) { return abs(v - want) <= tol; }
static int fails = 0;
static void CHECK(cuiw_renderer_t r, double dpr, const char *label,
                  double cx, double cy, int wr, int wg, int wb, int tol) {
    int R, G, B;
    sample(r, dpr, cx, cy, &R, &G, &B);
    printf("%-16s rgb(%3d,%3d,%3d)  want ~(%d,%d,%d)\n", label, R, G, B, wr, wg, wb);
    if (!near(R, wr, tol) || !near(G, wg, tol) || !near(B, wb, tol)) {
        fprintf(stderr, "FAIL: %s\n", label); fails++;
    }
}

int main(void) {
    double dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("light", 300, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }
    if (!send(r, kBuild)) return 1;
    /* A red->blue horizontal gradient across a 180-wide rect at canvas (10,10). */
    const char *cmds =
        "[[\"fillStyle\",\"linear-gradient(90deg,#ff0000,#0000ff)\"],"
        "[\"fillRect\",10,10,180,80]]";
    snprintf(buf, sizeof buf,
             "{\"type\":\"ops\",\"ops\":[{\"op\":\"canvas\",\"id\":10,\"cmds\":%s}]}", cmds);
    if (!send(r, buf)) return 1;

    /* Canvas origin is page (16,16). Rect spans canvas x 10..190 -> page 26..206.
     * Left edge ~red, right edge ~blue, middle ~purple. Sample mid-height. */
    CHECK(r, dpr, "grad left (red)", 30, 56, 255, 0, 0, 60);
    CHECK(r, dpr, "grad right (blue)", 200, 56, 0, 0, 255, 60);
    CHECK(r, dpr, "grad mid (purple)", 116, 56, 128, 0, 128, 70);

    cuiw_destroy(r);
    if (fails) { fprintf(stderr, "canvas-grad: %d FAIL\n", fails); return 1; }
    printf("canvas-grad: PASS\n");
    return 0;
}
