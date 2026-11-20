/* Scroll self-check: a box with overflow:scroll holds two stacked coloured
 * bands, each half the box tall (so only the first is visible at scrollTop=0).
 * Scrolling to the bottom (scrollTop = content - box) reveals the second band.
 * Exit 0 = pass.
 *   cuiw-scroll
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>

static char buf[2048];

static int send(cuiw_renderer_t r, const char *json) {
    if (!cuiw_send(r, json)) { fprintf(stderr, "send rejected\n"); return 0; }
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

int main(void) {
    double dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("dark", 200, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }

    /* Box at CSS (16,16), 100x100, overflow:scroll. Inside: a red band 0-100
     * and a blue band 100-200 (content is 200 tall, box is 100). */
    if (!send(r,
        "{\"type\":\"ops\",\"ops\":["
        "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
        "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#000000}"
        ".box{width:100px;height:100px;overflow:scroll;background:#000000}"
        ".red{height:100px;background:#ff0000}.blue{height:100px;background:#0000ff}\"},"
        "{\"op\":\"el\",\"id\":10,\"tag\":\"div\",\"attrs\":[[\"class\",\"box\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null},"
        "{\"op\":\"el\",\"id\":11,\"tag\":\"div\",\"attrs\":[[\"class\",\"red\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":10,\"id\":11,\"before\":null},"
        "{\"op\":\"el\",\"id\":12,\"tag\":\"div\",\"attrs\":[[\"class\",\"blue\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":10,\"id\":12,\"before\":null}"
        "]}")) return 1;

    int R, G, B;
    int fails = 0;
    /* scrollTop=0: the box shows the red band. Sample the box centre (66,66). */
    sample(r, dpr, 66, 66, &R, &G, &B);
    printf("scrollTop=0   box centre rgb(%3d,%3d,%3d)  want ~red (255,0,0)\n", R, G, B);
    if (!near(R, 255, 12) || !near(G, 0, 20) || !near(B, 0, 20)) {
        fprintf(stderr, "FAIL: at scrollTop=0 the red band is not visible\n"); fails++;
    }

    /* Scroll to the bottom: content 200 - box 100 = 100. The blue band shows. */
    if (!send(r, "{\"type\":\"ops\",\"ops\":[{\"op\":\"prop\",\"id\":10,"
                 "\"name\":\"scrollTop\",\"value\":100}]}")) return 1;
    sample(r, dpr, 66, 66, &R, &G, &B);
    printf("scrollTop=100 box centre rgb(%3d,%3d,%3d)  want ~blue (0,0,255)\n", R, G, B);
    if (!near(R, 0, 20) || !near(G, 0, 20) || !near(B, 255, 12)) {
        fprintf(stderr, "FAIL: at scrollTop=100 the blue band is not visible\n"); fails++;
    }

    /* Overscroll past the end clamps: the view stays on the blue band. */
    if (!send(r, "{\"type\":\"ops\",\"ops\":[{\"op\":\"prop\",\"id\":10,"
                 "\"name\":\"scrollTop\",\"value\":9999}]}")) return 1;
    sample(r, dpr, 66, 66, &R, &G, &B);
    printf("scrollTop=9999 box centre rgb(%3d,%3d,%3d)  want ~blue (clamped)\n", R, G, B);
    if (!near(R, 0, 20) || !near(G, 0, 20) || !near(B, 255, 12)) {
        fprintf(stderr, "FAIL: overscroll did not clamp to the bottom\n"); fails++;
    }

    cuiw_destroy(r);
    if (fails) { fprintf(stderr, "scroll: %d FAIL\n", fails); return 1; }
    printf("scroll: PASS\n");
    return 0;
}
