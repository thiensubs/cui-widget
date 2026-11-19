/* cuiw-render — the isolation proof for libcui-widget.
 *
 * Renders a widget (a stream of RenderOps) to a PNG using ONLY libcui-widget
 * over its C ABI. It includes cuiw.h and links nothing else from the widget
 * sandbox — no QuickJS, no validator, no CPU/memory caps. If this builds and
 * produces a correct PNG, the library is genuinely standalone: an application
 * that starts with CUI can embed libcui-widget and render widgets itself.
 *
 * Usage: cuiw-render <ops.json> <out.png> [width] [dpr] [theme]
 *   <ops.json>  a file holding a JSON array of RenderOps (one validated batch,
 *               as dumped by the widget host). Optional; a built-in sample is
 *               used if omitted.
 *   <out.png>   output PNG path (default: cuiw-render.png)
 *   width       bubble width in CSS px (default 400)
 *   dpr         device pixel ratio (default 2)
 *   theme       "light" | "dark" (default light)
 */
#include "cuiw.h"

/* In the CMake/tree build CUI's headers live under cui/. In the drop-in build
 * (CUI's flat cui.h beside this file) define CUI_DROPIN to include it flat. */
#ifdef CUI_DROPIN
#include "cui.h"              /* cui_write_png — flat single-file CUI */
#else
#include <cui/cui_render.h>   /* cui_write_png — from libcui, linked transitively */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read a whole file into a NUL-terminated malloc'd buffer, or NULL. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = '\0';
    fclose(f);
    if (len_out) *len_out = (size_t)n;
    return buf;
}

/* A tiny built-in sample document (RenderOps): a card with a title and two
 * buttons, so the example renders something even without an ops file. This is
 * the same op contract the widget host speaks (el/text/ins/style/css). */
static const char *kSampleOps =
    "["                                              /* one batch of RenderOps */
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"          /* the body host */
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

/* Renderer -> host message callback: shows the ready announcement, the reported
 * document size, and any click events the document emits. */
static void on_message(const char *json, void *user) {
    (void)user;
    /* Keep the console tidy: show the message type and a short excerpt. */
    const char *type = strstr(json, "\"type\"");
    printf("[msg] %.120s%s\n", type ? type : json, (type && strlen(type) > 120) ? "…" : "");
}

static void on_diag(const char *kind, const char *detail, void *user) {
    (void)user;
    printf("[diag] %s: %s\n", kind, detail);
}

int main(int argc, char **argv) {
    const char *ops_path = argc > 1 ? argv[1] : NULL;
    if (ops_path && ops_path[0] == '\0') ops_path = NULL;   /* "" = use the built-in sample */
    const char *out_path = argc > 2 ? argv[2] : "cuiw-render.png";
    double width = argc > 3 ? atof(argv[3]) : 400.0;
    double dpr = argc > 4 ? atof(argv[4]) : 2.0;
    const char *theme = argc > 5 ? argv[5] : "light";
    if (width <= 0) width = 400.0;
    if (dpr <= 0) dpr = 2.0;

    printf("libcui-widget %s (CUI %s)\n", cuiw_version(), cuiw_cui_version());

    /* Report the honest capability matrix, then prove the query round-trips. */
    uint8_t matrix[CUIW_FEATURE__COUNT];
    size_t n = cuiw_feature_matrix(matrix, CUIW_FEATURE__COUNT);
    printf("capabilities (%zu):", n);
    for (size_t i = 0; i < n; i++)
        if (matrix[i]) printf(" %s", cuiw_feature_name((cuiw_feature)i));
    printf("\n");
    printf("  unsupported:");
    for (size_t i = 0; i < n; i++)
        if (!matrix[i]) printf(" %s", cuiw_feature_name((cuiw_feature)i));
    printf("\n");

    cuiw_renderer_t r = cuiw_create(theme, width, dpr);
    if (!r) { fprintf(stderr, "cuiw_create failed\n"); return 1; }
    cuiw_set_message_callback(r, on_message, NULL);
    cuiw_set_diag_callback(r, on_diag, NULL);

    /* The document: an ops file if given, else the built-in sample. Either way
     * the batch must arrive as a message: {"type":"ops","ops":[ …render ops… ]},
     * exactly the envelope the widget host sends. */
    char *ops = NULL;
    char *wrapped = NULL;
    const char *batch;
    const char *src = NULL;
    if (ops_path) {
        size_t len = 0;
        ops = read_file(ops_path, &len);
        if (!ops) { fprintf(stderr, "cannot read ops file %s\n", ops_path); cuiw_destroy(r); return 1; }
        src = ops;
    } else {
        src = kSampleOps;
    }
    size_t src_len = strlen(src);
    wrapped = (char *)malloc(src_len + 32);
    if (!wrapped) { free(ops); cuiw_destroy(r); return 1; }
    snprintf(wrapped, src_len + 32, "{\"type\":\"ops\",\"ops\":%s}", src);
    batch = wrapped;
    free(ops);

    if (!cuiw_send(r, batch)) {
        fprintf(stderr, "cuiw_send rejected the batch (bad JSON?)\n");
        free(wrapped);
        cuiw_destroy(r);
        return 1;
    }
    free(wrapped);

    /* Paint synchronously (host-driven; the clock is not required here). */
    cuiw_frame frame = cuiw_paint(r);
    if (!frame.rgba) {
        fprintf(stderr, "cuiw_paint produced no frame\n");
        cuiw_destroy(r);
        return 1;
    }
    printf("painted %u x %u px (stride %u) in %.2f ms\n",
           frame.width, frame.height, frame.stride, frame.paint_ms);

    /* Write the PNG with CUI's own writer (libcui, linked transitively). */
    if (!cui_write_png(out_path, frame.rgba, (int)frame.width, (int)frame.height,
                       (int)frame.stride)) {
        fprintf(stderr, "cui_write_png failed for %s\n", out_path);
        cuiw_frame_free(&frame);
        cuiw_destroy(r);
        return 1;
    }
    printf("wrote %s\n", out_path);

    cuiw_frame_free(&frame);
    cuiw_destroy(r);
    return 0;
}