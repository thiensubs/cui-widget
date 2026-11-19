/* Pixel probe: render the built-in sample and print the actual RGBA values at
 * key points, so we can see what the rasterizer really produced (no image
 * viewer in the loop). Usage: cuiw-probe */
#include "cuiw.h"
#include <stdio.h>
#include <stdlib.h>

static const char *kOps =
    "["
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"css\",\"id\":0,\"text\":\"body{margin:0;padding:16px;background:#ffffff;font-size:15px;color:#111111}"
    ".card{background:#eef2ff;border-radius:12px;padding:16px;margin-bottom:12px}"
    "h1{font-size:20px;margin:0 0 8px 0;font-weight:600}"
    "p{margin:0 0 12px 0}"
    ".row{display:flex;gap:8px}"
    "button{padding:8px 14px;border-radius:8px;background:#2563eb;color:#ffffff;border:0;font-weight:500}}\"},"
    "{\"op\":\"el\",\"id\":10,\"tag\":\"div\",\"attrs\":[[\"class\",\"card\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null},"
    "{\"op\":\"el\",\"id\":11,\"tag\":\"h1\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":11,\"before\":null},"
    "{\"op\":\"text\",\"id\":12,\"data\":\"Hello from libcui-widget\"},"
    "{\"op\":\"ins\",\"parent\":11,\"id\":12,\"before\":null},"
    "{\"op\":\"el\",\"id\":13,\"tag\":\"p\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":13,\"before\":null},"
    "{\"op\":\"text\",\"id\":14,\"data\":\"Rendered by CUI, no WebView, no sandbox, no limits.\"},"
    "{\"op\":\"ins\",\"parent\":13,\"id\":14,\"before\":null},"
    "{\"op\":\"el\",\"id\":15,\"tag\":\"div\",\"attrs\":[[\"class\",\"row\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":15,\"before\":null},"
    "{\"op\":\"el\",\"id\":16,\"tag\":\"button\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":15,\"id\":16,\"before\":null},"
    "{\"op\":\"text\",\"id\":17,\"data\":\"OK\"},"
    "{\"op\":\"ins\",\"parent\":16,\"id\":17,\"before\":null},"
    "{\"op\":\"el\",\"id\":18,\"tag\":\"button\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":15,\"id\":18,\"before\":null},"
    "{\"op\":\"text\",\"id\":19,\"data\":\"Cancel\"},"
    "{\"op\":\"ins\",\"parent\":18,\"id\":19,\"before\":null}"
    "]";

static char wrapped[4096];

int main(int argc, char **argv) {
    double dpr = argc > 1 ? atof(argv[1]) : 2.0;
    if (dpr <= 0) dpr = 2.0;
    cuiw_renderer_t r = cuiw_create("light", 400, dpr);
    if (!r) { fprintf(stderr, "create failed\n"); return 1; }
    snprintf(wrapped, sizeof wrapped, "{\"type\":\"ops\",\"ops\":%s}", kOps);
    if (!cuiw_send(r, wrapped)) { fprintf(stderr, "send rejected\n"); return 1; }
    cuiw_frame f = cuiw_paint(r);
    if (!f.rgba) { fprintf(stderr, "no frame\n"); return 1; }
    printf("frame %ux%u stride %u\n", f.width, f.height, f.stride);
    /* Fine scan at the card's left edge (dump says x=32, pixels said ~88). */
    int y = 10;
    printf("left-edge fine scan y=%d:\n", y);
    for (int x = 24; x <= 104; x += 4) {
        const unsigned char *p = f.rgba + (size_t)y * f.stride + (size_t)x * 4;
        printf("  x=%3d rgb(%3u,%3u,%3u)%s\n", x, p[0], p[1], p[2],
               (p[0] == 238 && p[1] == 242) ? "  <- card" : "");
    }
    /* Right edge: where does card color end? */
    printf("right-edge fine scan y=%d:\n", y);
    for (int x = 720; x <= 799; x += 4) {
        const unsigned char *p = f.rgba + (size_t)y * f.stride + (size_t)x * 4;
        printf("  x=%3d rgb(%3u,%3u,%3u)%s\n", x, p[0], p[1], p[2],
               (p[0] == 238 && p[1] == 242) ? "  <- card" : "");
    }
    /* Exact card edges: scan mid-card rows for the first/last card-colored px. */
    for (int yy = 60; yy <= 300; yy += 70) {
        int L = -1, R = -1;
        for (int x = 0; x < (int)f.width; x++) {
            const unsigned char *p = f.rgba + (size_t)yy * f.stride + (size_t)x * 4;
            if (abs(p[0] - 238) <= 6 && abs(p[1] - 242) <= 6 && abs(p[2] - 255) <= 6) { L = x; break; }
        }
        for (int x = f.width - 1; x >= 0; x--) {
            const unsigned char *p = f.rgba + (size_t)yy * f.stride + (size_t)x * 4;
            if (abs(p[0] - 238) <= 6 && abs(p[1] - 242) <= 6 && abs(p[2] - 255) <= 6) { R = x; break; }
        }
        if (L >= 0)
            printf("[edges] y=%d card x=%d..%d width=%d left_margin=%d right_margin=%d\n",
                   yy, L, R, R - L + 1, L, f.width - 1 - R);
        else
            printf("[edges] y=%d no card color\n", yy);
    }
    /* Clean card-background rows (no text/buttons): h1 y=32..54, p y=62..105,
     * buttons y=234..310 device. y=110 and y=220 are empty card background. */
    int clean_rows[2] = {110, 220};
    for (int ri = 0; ri < 2; ri++) {
        int yy = clean_rows[ri];
        int L = -1, R = -1;
        for (int x = 0; x < (int)f.width; x++) {
            const unsigned char *p = f.rgba + (size_t)yy * f.stride + (size_t)x * 4;
            if (abs(p[0] - 238) <= 6 && abs(p[1] - 242) <= 6 && abs(p[2] - 255) <= 6) { L = x; break; }
        }
        for (int x = f.width - 1; x >= 0; x--) {
            const unsigned char *p = f.rgba + (size_t)yy * f.stride + (size_t)x * 4;
            if (abs(p[0] - 238) <= 6 && abs(p[1] - 242) <= 6 && abs(p[2] - 255) <= 6) { R = x; break; }
        }
        printf("[clean] y=%d card x=%d..%d width=%d left_mgn=%d right_mgn=%d\n",
               yy, L, R, R - L + 1, L, f.width - 1 - R);
    }
    printf("expected card device x=32..768 width=736 (CSS 16..384 w=368)\n");
    cuiw_frame_free(&f);
    cuiw_destroy(r);
    return 0;
}
