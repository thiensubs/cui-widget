/* Image self-check: render an <img> whose src is a tiny embedded PNG (a solid
 * red 8x8, as a data: URL) and confirm the picture is drawn inside the element
 * box — centre is red, the page outside stays white. Exit 0 = pass.
 *   cuiw-images
 */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[1024];

/* A solid red 8x8 PNG, base64-encoded, embedded below as a data: URL (the only
 * image form the host contract allows — no network, no file paths). */

static const char *kBuild =
    "{\"type\":\"ops\",\"ops\":["
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff}"
    "img{width:60px;height:60px}\"},"
    "{\"op\":\"el\",\"id\":10,\"tag\":\"img\",\"attrs\":[[\"src\",\""
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAIAAABLbSncAAAACXBIWXMAAAABAAAAAQBPJcTW"
    "AAAAEklEQVR4nGP8y4AdsOAQH6QSANErARssm0F5AAAAAElFTkSuQmCC\"]],\"adopt\":\"body\"},"
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
    /* The box spans CSS (16,16)-(76,76); centre (46,46) should be the red image. */
    sample(r, dpr, 46, 46, &R, &G, &B);
    printf("img centre      rgb(%3d,%3d,%3d)  want ~red (255,0,0)\n", R, G, B);
    if (!near(R, 255, 12) || !near(G, 0, 20) || !near(B, 0, 20)) {
        fprintf(stderr, "FAIL: <img> centre is not the red picture\n"); fails++;
    }
    /* Just outside the box (x 90) should be the white page, not the image. */
    sample(r, dpr, 90, 46, &R, &G, &B);
    printf("outside box     rgb(%3d,%3d,%3d)  want white (255,255,255)\n", R, G, B);
    if (!near(R, 255, 8) || !near(G, 255, 8) || !near(B, 255, 8)) {
        fprintf(stderr, "FAIL: image leaked outside its box\n"); fails++;
    }
    cuiw_destroy(r);

    if (fails) { fprintf(stderr, "images: %d FAIL\n", fails); return 1; }
    printf("images: PASS\n");
    return 0;
}
