/* Gradient + box-shadow self-check: render a box with a vertical linear-gradient
 * and one with a box-shadow, then sample pixels to confirm the gradient blends
 * top->bottom and the shadow darkens the area behind/below the box. Exit 0 = pass.
 *   cuiw-gradients
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[2048];

/* A 100x100 box at CSS (16,16): body padding 16. Red->blue gradient. */
static const char *kBuild =
    "{\"type\":\"ops\",\"ops\":["
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff}"
    ".g{width:100px;height:100px;background-image:linear-gradient(to bottom,#ff0000,#0000ff)}\"},"
    "{\"op\":\"el\",\"id\":10,\"tag\":\"div\",\"attrs\":[[\"class\",\"g\"]],\"adopt\":\"body\"},"
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

int main(void) {
    double dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("light", 200, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }
    if (!send(r, kBuild)) return 1;

    int R, G, B;
    int fails = 0;
    /* Gradient box spans CSS y 16..116. Top ~ red, bottom ~ blue, middle ~ purple. */
    sample(r, dpr, 66, 20, &R, &G, &B);
    printf("gradient top    rgb(%3d,%3d,%3d)  want ~red   (255,0,0)\n", R, G, B);
    if (!near(R, 255, 20) || !near(G, 0, 20) || !near(B, 0, 20)) {
        fprintf(stderr, "FAIL: gradient top is not red\n"); fails++;
    }
    sample(r, dpr, 66, 112, &R, &G, &B);
    printf("gradient bottom rgb(%3d,%3d,%3d)  want ~blue  (0,0,255)\n", R, G, B);
    if (!near(R, 0, 20) || !near(G, 0, 20) || !near(B, 255, 20)) {
        fprintf(stderr, "FAIL: gradient bottom is not blue\n"); fails++;
    }
    sample(r, dpr, 66, 66, &R, &G, &B);
    printf("gradient middle rgb(%3d,%3d,%3d)  want ~purple(128,0,128)\n", R, G, B);
    if (!near(G, 0, 20) || !near(R, 128, 30) || !near(B, 128, 30)) {
        fprintf(stderr, "FAIL: gradient middle is not a red/blue blend\n"); fails++;
    }
    cuiw_destroy(r);

    /* box-shadow: a box on a white page with a black shadow offset down-right.
     * A pixel just below the box should be darker than the white page. */
    cuiw_renderer_t r2 = cuiw_create("light", 200, dpr);
    if (!r2) { fprintf(stderr, "create failed\n"); return 1; }
    const char *sh =
        "{\"type\":\"ops\",\"ops\":["
        "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
        "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff}"
        ".s{width:60px;height:60px;background:#ffffff;"
        "box-shadow:6px 6px 4px #000000}\"},"
        "{\"op\":\"el\",\"id\":10,\"tag\":\"div\",\"attrs\":[[\"class\",\"s\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null}"
        "]}";
    if (!send(r2, sh)) return 1;
    /* Box spans CSS (16,16)-(76,76). Shadow offset +6,+6 with blur 4 -> dark band
     * just below the box bottom edge, around CSS y ~80, x ~40 (inside box x-range). */
    int R2, G2, B2;
    sample(r2, dpr, 40, 82, &R2, &G2, &B2);
    printf("shadow below    rgb(%3d,%3d,%3d)  want darker than white\n", R2, G2, B2);
    if (R2 > 200 || G2 > 200 || B2 > 200) {
        fprintf(stderr, "FAIL: no shadow below the box (pixel is near white)\n"); fails++;
    }
    /* A pixel far from the box and its shadow should stay white. The frame is
     * only ~92 CSS px tall (padding + box), so sample far right, mid height. */
    sample(r2, dpr, 185, 45, &R2, &G2, &B2);
    printf("far from box    rgb(%3d,%3d,%3d)  want white (255,255,255)\n", R2, G2, B2);
    if (!near(R2, 255, 8) || !near(G2, 255, 8) || !near(B2, 255, 8)) {
        fprintf(stderr, "FAIL: shadow leaked far from the box\n"); fails++;
    }
    cuiw_destroy(r2);

    /* Horizontal gradient (to right): left edge red, right edge blue. */
    cuiw_renderer_t r3 = cuiw_create("light", 200, dpr);
    if (!r3) { fprintf(stderr, "create failed\n"); return 1; }
    if (!send(r3,
        "{\"type\":\"ops\",\"ops\":["
        "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
        "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff}"
        ".h{width:100px;height:40px;background:linear-gradient(to right,#ff0000,#0000ff)}\"},"
        "{\"op\":\"el\",\"id\":20,\"tag\":\"div\",\"attrs\":[[\"class\",\"h\"]],\"adopt\":\"body\"},"
        "{\"op\":\"ins\",\"parent\":2,\"id\":20,\"before\":null}"
        "]}")) return 1;
    int R3, G3, B3;
    /* Box spans CSS x 16..116. Left ~ red, right ~ blue. */
    sample(r3, dpr, 26, 36, &R3, &G3, &B3);
    printf("horizontal left  rgb(%3d,%3d,%3d)  want reddish (R>B)\n", R3, G3, B3);
    if (!(R3 > B3)) {
        fprintf(stderr, "FAIL: horizontal left is not reddish\n"); fails++;
    }
    sample(r3, dpr, 106, 36, &R3, &G3, &B3);
    printf("horizontal right rgb(%3d,%3d,%3d)  want bluish (B>R)\n", R3, G3, B3);
    if (!(B3 > R3)) {
        fprintf(stderr, "FAIL: horizontal right is not bluish\n"); fails++;
    }
    cuiw_destroy(r3);

    if (fails) { fprintf(stderr, "gradients: %d FAIL\n", fails); return 1; }
    printf("gradients: PASS\n");
    return 0;
}
