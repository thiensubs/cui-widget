/* Canvas self-check: replay a batch of 2D-context commands against a <canvas>
 * element and assert the rendered pixels match (a filled rect, a stroked rect,
 * a filled circle, and fillText). Deterministic — no wall-clock. Exit 0 = pass.
 *   cuiw-canvas
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[4096];

/* A 200x100 canvas at CSS (16,16): body padding 16, canvas default block. */
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
/* Sample a pixel at CSS coords (CSS 16,16 is the canvas origin). */
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

int main(void) {
    double dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("light", 400, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }
    if (!send(r, kBuild)) return 1;

    /* A batch: red fillRect, blue strokeRect, green filled circle, black text. */
    const char *cmds =
        "[[\"fillStyle\",\"#ff0000\"],[\"fillRect\",10,10,50,30],"
        "[\"strokeStyle\",\"#0000ff\"],[\"lineWidth\",4],[\"strokeRect\",80,10,50,30],"
        "[\"beginPath\"],[\"arc\",30,70,15,0,6.2831],[\"fillStyle\",\"#00aa00\"],[\"fill\"],"
        "[\"fillStyle\",\"#000000\"],[\"font\",\"14px sans-serif\"],[\"fillText\",\"Hi\",120,80]]";
    snprintf(buf, sizeof buf,
             "{\"type\":\"ops\",\"ops\":[{\"op\":\"canvas\",\"id\":10,\"cmds\":%s}]}", cmds);
    if (!send(r, buf)) return 1;

    int R, G, B;
    int fails = 0;
#define CHECK(label, cx, cy, wr, wg, wb, tol) do { \
        sample(r, dpr, (cx), (cy), &R, &G, &B); \
        printf("%-22s rgb(%3d,%3d,%3d)  want ~(%d,%d,%d)\n", label, R, G, B, wr, wg, wb); \
        if (!near(R, wr, tol) || !near(G, wg, tol) || !near(B, wb, tol)) { \
            fprintf(stderr, "FAIL: %s\n", label); fails++; \
        } \
    } while (0)

    /* Inside the red rect (canvas 30,20 -> page 46,36). */
    CHECK("fillRect center", 46, 36, 255, 0, 0, 8);
    /* Just outside the red rect but inside the canvas (canvas 70,20 -> 86,36). */
    CHECK("fillRect outside", 86, 36, 255, 255, 255, 8);
    /* On the blue stroke of strokeRect (canvas 80,30 -> 96,46) - top edge. */
    CHECK("strokeRect edge", 96, 27, 0, 0, 255, 40);
    /* Center of the green circle (canvas 30,70 -> 46,86). */
    CHECK("arc fill center", 46, 86, 0, 170, 0, 40);

    cuiw_destroy(r);
    if (fails) { fprintf(stderr, "canvas: %d FAIL\n", fails); return 1; }
    printf("canvas: PASS\n");
    return 0;
}
