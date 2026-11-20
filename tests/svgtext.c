/* SVG <text> + canvas drawImage self-check.
 *
 * 1. An <svg> with white <text> on a black page: scan the label's band and
 *    confirm at least one stroke pixel is bright (the text was drawn).
 * 2. A <canvas> that drawImage()s a solid red 8x8 PNG (data: URL) into a rect:
 *    the centre of that rect is red.
 * Exit 0 = pass.
 *   cuiw-svgtext
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Solid red 8x8 PNG, base64, as a data: URL. */
static const char *kRedPng =
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAIAAABLbSncAAAACXBIWXMAAAABAAAAAQBPJcTW"
    "AAAAEklEQVR4nGP8y4AdsOAQH6QSANErARssm0F5AAAAAElFTkSuQmCC";

static int send(cuiw_renderer_t r, const char *json) {
    if (!cuiw_send(r, json)) { fprintf(stderr, "send rejected\n"); return 0; }
    return 1;
}

/* True if any pixel in the CSS-rect (x0,y0)-(x1,y1) has R >= want_r. */
static int scan_for_red(cuiw_renderer_t r, double dpr,
                        double x0, double y0, double x1, double y1, int want_r) {
    cuiw_frame f = cuiw_paint(r);
    if (!f.rgba) { fprintf(stderr, "no frame\n"); exit(1); }
    int found = 0;
    for (double cy = y0; cy <= y1 && !found; cy += 1.0)
        for (double cx = x0; cx <= x1 && !found; cx += 1.0) {
            int x = (int)(cx * dpr), y = (int)(cy * dpr);
            const unsigned char *p = f.rgba + (size_t)y * f.stride + (size_t)x * 4;
            if (p[0] >= want_r) found = 1;
        }
    cuiw_frame_free(&f);
    return found;
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

int main(void) {
    double dpr = 2.0;
    int fails = 0;

    /* 1. SVG <text>: white "Hi" on black, big font, at viewBox (20,60). */
    cuiw_renderer_t r = cuiw_create("dark", 200, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }
    char b[1024];
    snprintf(b, sizeof b,
        "{\"type\":\"ops\",\"ops\":["
        "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
        "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:0;background:#000000}\"},"
        "{\"op\":\"el\",\"id\":10,\"tag\":\"svg\",\"ns\":\"svg\",\"attrs\":["
        "[\"viewBox\",\"0 0 200 100\"],[\"width\",\"200\"],[\"height\",\"100\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null},"
        "{\"op\":\"el\",\"id\":11,\"tag\":\"text\",\"ns\":\"svg\",\"attrs\":["
        "[\"x\",\"20\"],[\"y\",\"70\"],[\"fill\",\"#ffffff\"],[\"font-size\",\"60\"],"
        "[\"font-weight\",\"bold\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":10,\"id\":11,\"before\":null},"
        "{\"op\":\"text\",\"id\":12,\"data\":\"Hi\"},"
        "{\"op\":\"ins\",\"parent\":11,\"id\":12,\"before\":null}"
        "]}");
    if (!send(r, b)) return 1;
    /* Scan the band the glyphs occupy (baseline y=70, ascends ~48px -> 22..70). */
    int has_stroke = scan_for_red(r, dpr, 20, 22, 120, 70, 160);
    printf("svg <text> stroke pixel found: %s\n", has_stroke ? "yes" : "NO");
    if (!has_stroke) { fprintf(stderr, "FAIL: svg <text> drew no bright pixels\n"); fails++; }
    cuiw_destroy(r);

    /* 2. canvas drawImage: red PNG into rect (10,10,60,60) on a 100x100 canvas. */
    cuiw_renderer_t r2 = cuiw_create("dark", 200, dpr);
    if (!r2) { fprintf(stderr, "create failed\n"); return 1; }
    char c[1600];
    snprintf(c, sizeof c,
        "{\"type\":\"ops\",\"ops\":["
        "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
        "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:0;background:#000000}"
        "canvas{width:100px;height:100px;background:#000000}\"},"
        "{\"op\":\"el\",\"id\":10,\"tag\":\"canvas\",\"attrs\":[],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null},"
        "{\"op\":\"canvas\",\"id\":10,\"cmds\":["
        "[\"drawImage\",\"%s\",10,10,60,60]]}"
        "]}", kRedPng);
    if (!send(r2, c)) return 1;
    int R, G, B;
    /* Canvas origin CSS (0,0); rect (10,10,60,60) centre = (40,40). */
    sample(r2, dpr, 40, 40, &R, &G, &B);
    printf("canvas drawImage centre rgb(%3d,%3d,%3d)  want ~red (255,0,0)\n", R, G, B);
    if (!near(R, 255, 12) || !near(G, 0, 20) || !near(B, 0, 20)) {
        fprintf(stderr, "FAIL: drawImage centre is not the red picture\n"); fails++;
    }
    cuiw_destroy(r2);

    if (fails) { fprintf(stderr, "svgtext: %d FAIL\n", fails); return 1; }
    printf("svgtext: PASS\n");
    return 0;
}
