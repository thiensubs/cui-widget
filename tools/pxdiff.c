/* pxdiff — pixel-by-pixel comparison of two PNGs.
 *
 *   pxdiff <golden.png> <actual.png> [tol] [max_pct] [--diff out.png]
 *
 * Decodes both images to RGBA and compares them pixel by pixel. This is the
 * regression gate for libcui-widget's example app: the rendered pixels are the
 * contract, so a layout or paint change fails here rather than shipping.
 *
 * Why decode instead of comparing PNG bytes: the pixels are what matters. If CUI
 * ever changes its PNG encoder, the file bytes change but the image does not — a
 * byte comparison would raise a false alarm. Decoding first means only a real
 * pixel difference fails the build.
 *
 *   tol       max per-channel difference (0-255) still considered equal. Default 0
 *             (byte-exact). Raise it only if a platform shows a documented,
 *             sub-visible antialiasing difference.
 *   max_pct   max percentage of pixels allowed to differ. Default 0 (no pixel may
 *             differ). A pixel "differs" when any channel exceeds tol.
 *
 * Exit code: 0 if dimensions match and differing pixels are within max_pct,
 * otherwise 1. With --diff, writes a visualization PNG: identical pixels are
 * dimmed, differing pixels are highlighted red so a human can see what moved.
 */
#include "cui/cui_render.h"   /* cui_write_png — libcui, linked transitively */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
/* stb_image is vendored by CUI (public domain); decode-only here. */
#include "stb_image.h"

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s <golden.png> <actual.png> [tol] [max_pct] [--diff out.png]\n",
            prog);
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 2; }

    const char *golden_path = argv[1];
    const char *actual_path = argv[2];
    int tol = 0;
    double max_pct = 0.0;
    const char *diff_out = NULL;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--diff") == 0 && i + 1 < argc) {
            diff_out = argv[++i];
        } else if (i == 3) {
            tol = atoi(argv[i]);
        } else if (i == 4) {
            max_pct = atof(argv[i]);
        } else {
            usage(argv[0]); return 2;
        }
    }
    if (tol < 0 || tol > 255) { fprintf(stderr, "tol must be 0-255\n"); return 2; }
    if (max_pct < 0.0 || max_pct > 100.0) { fprintf(stderr, "max_pct must be 0-100\n"); return 2; }

    int gw = 0, gh = 0, gcomp = 0;
    unsigned char *g = stbi_load(golden_path, &gw, &gh, &gcomp, 4);
    if (!g) { fprintf(stderr, "cannot decode %s: %s\n", golden_path, stbi_failure_reason()); return 2; }

    int aw = 0, ah = 0, acomp = 0;
    unsigned char *a = stbi_load(actual_path, &aw, &ah, &acomp, 4);
    if (!a) {
        fprintf(stderr, "cannot decode %s: %s\n", actual_path, stbi_failure_reason());
        stbi_image_free(g); return 2;
    }

    if (gw != aw || gh != ah) {
        fprintf(stderr,
                "FAIL size mismatch: golden %dx%d vs actual %dx%d\n",
                gw, gh, aw, ah);
        stbi_image_free(g); stbi_image_free(a);
        return 1;
    }

    const size_t npx = (size_t)gw * (size_t)gh;
    unsigned char *diff = NULL;
    if (diff_out) {
        /* Start from the actual image, dimmed; differing pixels get repainted red. */
        diff = (unsigned char *)malloc(npx * 4);
        if (diff) {
            for (size_t i = 0; i < npx; i++) {
                diff[i * 4 + 0] = (unsigned char)(a[i * 4 + 0] / 4);
                diff[i * 4 + 1] = (unsigned char)(a[i * 4 + 1] / 4);
                diff[i * 4 + 2] = (unsigned char)(a[i * 4 + 2] / 4);
                diff[i * 4 + 3] = 255;
            }
        }
    }

    size_t ndiff = 0;
    int max_delta = 0;
    for (size_t i = 0; i < npx; i++) {
        int dmax = 0;
        for (int c = 0; c < 3; c++) {           /* compare RGB; alpha is opaque here */
            int d = (int)a[i * 4 + c] - (int)g[i * 4 + c];
            if (d < 0) d = -d;
            if (d > dmax) dmax = d;
        }
        if (dmax > max_delta) max_delta = dmax;
        if (dmax > tol) {
            ndiff++;
            if (diff) {
                diff[i * 4 + 0] = 255;
                diff[i * 4 + 1] = 0;
                diff[i * 4 + 2] = 0;
                diff[i * 4 + 3] = 255;
            }
        }
    }

    const double pct = npx ? (100.0 * (double)ndiff / (double)npx) : 0.0;
    printf("golden   %s (%dx%d)\n", golden_path, gw, gh);
    printf("actual   %s (%dx%d)\n", actual_path, aw, ah);
    printf("pixels   %zu   differing %zu (%.4f%%)   max channel delta %d   tol %d\n",
           npx, ndiff, pct, max_delta, tol);

    if (diff_out) {
        if (diff && cui_write_png(diff_out, diff, gw, gh, gw * 4))
            printf("diff     %s\n", diff_out);
        else
            fprintf(stderr, "warning: could not write diff image %s\n", diff_out);
        free(diff);
    }

    stbi_image_free(g);
    stbi_image_free(a);

    if (pct > max_pct) {
        fprintf(stderr,
                "FAIL %zu/%zu pixels differ (%.4f%% > %.4f%%), max delta %d\n",
                ndiff, npx, pct, max_pct, max_delta);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
