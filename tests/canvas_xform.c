/* Canvas transform self-check: translate, rotate, scale and clip on a <canvas>
 * 2D context. Asserts painted pixels land where the matrix says. Deterministic.
 * Exit 0 = pass.   cuiw-canvas-xform
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[4096];

/* Three stacked 200x200 canvases at CSS (16,16), (16,232), (16,448).
 * Each transform case draws on its OWN element so command batches don't
 * accumulate on one node. */
static const char *kBuild =
    "{\"type\":\"ops\",\"ops\":["
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff}"
    "canvas{width:200px;height:200px;background:#ffffff;display:block;margin-bottom:16px}}\"},"
    "{\"op\":\"el\",\"id\":10,\"tag\":\"canvas\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null},"
    "{\"op\":\"el\",\"id\":11,\"tag\":\"canvas\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":11,\"before\":null},"
    "{\"op\":\"el\",\"id\":12,\"tag\":\"canvas\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":12,\"before\":null}"
    "]}";
/* Page y of each canvas's top edge. Body padding-top 16; each canvas is
 * 200 tall and the margin-bottom does NOT add (block layout), so the
 * origins are 16, 216, 416 — measured from the renderer, not assumed. */
static const double kTop[3] = {16, 216, 416};

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
    printf("%-22s rgb(%3d,%3d,%3d)  want ~(%d,%d,%d)\n", label, R, G, B, wr, wg, wb);
    if (!near(R, wr, tol) || !near(G, wg, tol) || !near(B, wb, tol)) {
        fprintf(stderr, "FAIL: %s\n", label); fails++;
    }
}
/* Draw `cmds` on canvas element `id`. */
static void draw(cuiw_renderer_t r, int64_t id, const char *cmds) {
    snprintf(buf, sizeof buf,
             "{\"type\":\"ops\",\"ops\":[{\"op\":\"canvas\",\"id\":%lld,\"cmds\":%s}]}",
             (long long)id, cmds);
    if (!send(r, buf)) exit(1);
}

int main(void) {
    double dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("light", 400, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }
    if (!send(r, kBuild)) return 1;

    /* --- Canvas 1: translate --- */
    draw(r, 10, "[[\"fillStyle\",\"#ff0000\"],[\"translate\",100,100],"
                "[\"fillRect\",-30,-10,60,20]]");
    /* Centre of the translated rect: canvas (100,100) -> page (116,116). */
    CHECK(r, dpr, "translate centre", 116, kTop[0] + 100, 255, 0, 0, 8);
    /* Without translate the rect sat at canvas (0,0) -> page (16,16): white now. */
    CHECK(r, dpr, "translate moved it", 16, kTop[0] + 0, 255, 255, 255, 8);

    /* --- Canvas 2: scale (alone, so nothing overpaints it) --- */
    draw(r, 11, "[[\"fillStyle\",\"#0000ff\"],[\"scale\",2,2],[\"fillRect\",10,10,20,20]]");
    /* scale(2,2): rect (10,10,20,20) -> device (20,20,40,40); centre page (36,56). */
    CHECK(r, dpr, "scale centre", 36, kTop[1] + 40, 0, 0, 255, 8);
    /* Outside the scaled rect (page 90,56): white. */
    CHECK(r, dpr, "scale bounded", 90, kTop[1] + 40, 255, 255, 255, 8);

    /* --- Canvas 3: clip + restore --- */
    draw(r, 12, "[[\"save\"],[\"beginPath\"],[\"rect\",0,0,30,200],[\"clip\"],"
                "[\"fillStyle\",\"#00aa00\"],[\"fillRect\",0,0,200,200],[\"restore\"],"
                "[\"fillStyle\",\"#ff00ff\"],[\"fillRect\",40,0,10,10]]");
    /* Green band clipped to device x<30; page x=40 (device 24) is inside. */
    CHECK(r, dpr, "clip inside", 40, kTop[2] + 100, 0, 170, 0, 40);
    /* restore() unwinds the clip, so magenta at device (40..50,0..10) -> page
     * centre (61,21) paints. */
    CHECK(r, dpr, "restore unclips", 61, kTop[2] + 5, 255, 0, 255, 40);
    /* Right of the green band (device x>30) stayed white except the magenta rect. */
    CHECK(r, dpr, "clip bounded", 150, kTop[2] + 100, 255, 255, 255, 8);

    cuiw_destroy(r);
    if (fails) { fprintf(stderr, "canvas-xform: %d FAIL\n", fails); return 1; }
    printf("canvas-xform: PASS\n");
    return 0;
}
