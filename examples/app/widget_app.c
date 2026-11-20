/* widget_app — the reference application for libcui-widget + CUI.
 *
 * This is the app you build on every CI run and every release. It lays out a
 * representative widget document through the libcui-widget C ABI, paints it with
 * CUI's software rasterizer, and writes a PNG per theme. CI then compares those
 * PNGs pixel-for-pixel against committed golden images, so a layout or paint
 * regression fails the build instead of shipping.
 *
 * It is headless on purpose: no window, no event loop, no GPU. It renders through
 * CUI's software rasterizer and bundled fonts (Inter/Geist, rasterized by the
 * vendored stb_truetype), which makes the output byte-identical on Linux, macOS
 * and Windows — the property that lets one set of golden images serve every
 * platform. See docs/CI.md for why that holds.
 *
 * Usage: widget_app <out-dir>
 *   writes <out-dir>/widget-light.png and <out-dir>/widget-dark.png
 *
 * The document deliberately exercises every capability the engine supports today
 * (see docs/CAPABILITIES.md): block + flex layout, id/class/tag selectors,
 * border-radius, text-align, font-weight and min-height. It avoids the
 * unsupported ones so the golden images reflect real, intended output rather than
 * documented degradation.
 */
#include "cuiw.h"
#include <cui/cui_render.h>   /* cui_write_png — libcui, linked transitively */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A representative widget document as one batch of RenderOps — the same op
 * contract the widget host speaks (el/text/ins/style/css). Built to be stable:
 * no animation, no clock, no external input, so the painted pixels depend only
 * on the document and the engine version. */
static const char *kAppOps =
    "["
    /* the body host */
    "{\"op\":\"el\",\"id\":2,\"tag\":\"body\",\"attrs\":[],\"adopt\":\"body\"},"

    /* Styles exercise every supported capability. */
    "{\"op\":\"css\",\"id\":0,\"text\":"
      "\"body{margin:0;padding:20px;background:#f4f5f7;font-size:15px;color:#1a1a1a;font-family:sans-serif}"
      ".profile{background:#ffffff;border-radius:14px;padding:18px;margin-bottom:14px;min-height:120px}"
      ".name{font-size:20px;font-weight:700;margin:0 0 4px 0}"
      ".handle{font-size:13px;font-weight:400;color:#6b7280;margin:0 0 12px 0}"
      ".bio{font-size:14px;font-weight:400;margin:0 0 14px 0;line-height:1.45}"
      ".row{display:flex;gap:10px;align-items:center}"
      "button{padding:9px 16px;border-radius:9px;border:0;font-weight:500;font-size:14px}"
      ".primary{background:#2563eb;color:#ffffff}"
      ".ghost{background:#e5e7eb;color:#1f2937}"
      ".footer{font-size:12px;color:#9ca3af;text-align:center;margin-top:16px}"
      ".actions{display:flex;justify-content:space-between;align-items:center;margin:6px 4px 4px 4px}"
      ".action{display:flex;gap:6px;align-items:center;font-size:13px;color:#6b7280}"
      "#title{text-align:center}\"},"

    /* A profile card: block layout, min-height, border-radius. */
    "{\"op\":\"el\",\"id\":10,\"tag\":\"div\",\"attrs\":[[\"class\",\"profile\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":10,\"before\":null},"

    /* Name — centered via an id selector, bold via font-weight. */
    "{\"op\":\"el\",\"id\":11,\"tag\":\"h1\",\"attrs\":[[\"id\",\"title\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":11,\"before\":null},"
    "{\"op\":\"text\",\"id\":12,\"data\":\"Maya Okafor\"},"
    "{\"op\":\"ins\",\"parent\":11,\"id\":12,\"before\":null},"

    /* Handle — a class selector, lighter weight. */
    "{\"op\":\"el\",\"id\":13,\"tag\":\"p\",\"attrs\":[[\"class\",\"handle\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":13,\"before\":null},"
    "{\"op\":\"text\",\"id\":14,\"data\":\"@maya — widgets, no webview\"},"
    "{\"op\":\"ins\",\"parent\":13,\"id\":14,\"before\":null},"

    /* Bio — wraps across lines at this width, exercising line layout. */
    "{\"op\":\"el\",\"id\":15,\"tag\":\"p\",\"attrs\":[[\"class\",\"bio\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":15,\"before\":null},"
    "{\"op\":\"text\",\"id\":16,\"data\":\"Building interfaces that render without a browser. This paragraph is long enough to wrap onto a second line at the reference width.\"},"
    "{\"op\":\"ins\",\"parent\":15,\"id\":16,\"before\":null},"

    /* A flex row of two buttons — exercises flex + gap + the button fixes. */
    "{\"op\":\"el\",\"id\":17,\"tag\":\"div\",\"attrs\":[[\"class\",\"row\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":17,\"before\":null},"
    "{\"op\":\"el\",\"id\":18,\"tag\":\"button\",\"attrs\":[[\"class\",\"primary\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":17,\"id\":18,\"before\":null},"
    "{\"op\":\"text\",\"id\":19,\"data\":\"Follow\"},"
    "{\"op\":\"ins\",\"parent\":18,\"id\":19,\"before\":null},"
    "{\"op\":\"el\",\"id\":20,\"tag\":\"button\",\"attrs\":[[\"class\",\"ghost\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":17,\"id\":20,\"before\":null},"
    "{\"op\":\"text\",\"id\":21,\"data\":\"Message\"},"
    "{\"op\":\"ins\",\"parent\":20,\"id\":21,\"before\":null},"

    /* A post action bar — the everyday app use case for inline SVG: UI icons
     * drawn as vectors, no icon font, no image assets. Each icon is a small
     * <svg> exercising a different shape feature (curved path, polyline, arcs
     * flattened, stroked-only path). */
    "{\"op\":\"el\",\"id\":30,\"tag\":\"div\",\"attrs\":[[\"class\",\"actions\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":10,\"id\":30,\"before\":null},"

    /* Like — a heart (fill + stroke), curved cubic path. */
    "{\"op\":\"el\",\"id\":31,\"tag\":\"div\",\"attrs\":[[\"class\",\"action\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":30,\"id\":31,\"before\":null},"
    "{\"op\":\"el\",\"id\":32,\"tag\":\"svg\",\"ns\":\"svg\",\"attrs\":[[\"viewBox\",\"0 0 24 24\"],[\"width\",\"18\"],[\"height\",\"18\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":31,\"id\":32,\"before\":null},"
    "{\"op\":\"el\",\"id\":33,\"tag\":\"path\",\"ns\":\"svg\",\"attrs\":[[\"d\",\"M12 20 C 4 14 3 9 6 6.5 C 8.5 4.5 11 6 12 8 C 13 6 15.5 4.5 18 6.5 C 21 9 20 14 12 20 Z\"],[\"fill\",\"#ef4444\"],[\"stroke\",\"#b91c1c\"],[\"stroke-width\",\"1.2\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":32,\"id\":33,\"before\":null},"
    "{\"op\":\"el\",\"id\":47,\"tag\":\"span\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":31,\"id\":47,\"before\":null},"
    "{\"op\":\"text\",\"id\":34,\"data\":\"128\"},"
    "{\"op\":\"ins\",\"parent\":47,\"id\":34,\"before\":null},"

    /* Comment — a speech bubble (stroked only, no fill). */
    "{\"op\":\"el\",\"id\":35,\"tag\":\"div\",\"attrs\":[[\"class\",\"action\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":30,\"id\":35,\"before\":null},"
    "{\"op\":\"el\",\"id\":36,\"tag\":\"svg\",\"ns\":\"svg\",\"attrs\":[[\"viewBox\",\"0 0 24 24\"],[\"width\",\"18\"],[\"height\",\"18\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":35,\"id\":36,\"before\":null},"
    "{\"op\":\"el\",\"id\":37,\"tag\":\"path\",\"ns\":\"svg\",\"attrs\":[[\"d\",\"M4 5 L20 5 L20 15 L11 15 L7 19 L7 15 L4 15 Z\"],[\"fill\",\"none\"],[\"stroke\",\"#6b7280\"],[\"stroke-width\",\"1.6\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":36,\"id\":37,\"before\":null},"
    "{\"op\":\"el\",\"id\":48,\"tag\":\"span\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":35,\"id\":48,\"before\":null},"
    "{\"op\":\"text\",\"id\":38,\"data\":\"42\"},"
    "{\"op\":\"ins\",\"parent\":48,\"id\":38,\"before\":null},"

    /* Share — a paper plane (filled), a concave polygon. */
    "{\"op\":\"el\",\"id\":39,\"tag\":\"div\",\"attrs\":[[\"class\",\"action\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":30,\"id\":39,\"before\":null},"
    "{\"op\":\"el\",\"id\":40,\"tag\":\"svg\",\"ns\":\"svg\",\"attrs\":[[\"viewBox\",\"0 0 24 24\"],[\"width\",\"18\"],[\"height\",\"18\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":39,\"id\":40,\"before\":null},"
    "{\"op\":\"el\",\"id\":41,\"tag\":\"polygon\",\"ns\":\"svg\",\"attrs\":[[\"points\",\"3 12 21 4 14 21 11 13\"],[\"fill\",\"#6b7280\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":40,\"id\":41,\"before\":null},"
    "{\"op\":\"el\",\"id\":49,\"tag\":\"span\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":39,\"id\":49,\"before\":null},"
    "{\"op\":\"text\",\"id\":42,\"data\":\"19\"},"
    "{\"op\":\"ins\",\"parent\":49,\"id\":42,\"before\":null},"

    /* Save — a bookmark (stroked), exercising a notch via a path. */
    "{\"op\":\"el\",\"id\":43,\"tag\":\"div\",\"attrs\":[[\"class\",\"action\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":30,\"id\":43,\"before\":null},"
    "{\"op\":\"el\",\"id\":44,\"tag\":\"svg\",\"ns\":\"svg\",\"attrs\":[[\"viewBox\",\"0 0 24 24\"],[\"width\",\"18\"],[\"height\",\"18\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":43,\"id\":44,\"before\":null},"
    "{\"op\":\"el\",\"id\":45,\"tag\":\"path\",\"ns\":\"svg\",\"attrs\":[[\"d\",\"M6 3 L18 3 L18 21 L12 16 L6 21 Z\"],[\"fill\",\"none\"],[\"stroke\",\"#6b7280\"],[\"stroke-width\",\"1.6\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":44,\"id\":45,\"before\":null},"
    "{\"op\":\"el\",\"id\":50,\"tag\":\"span\",\"attrs\":[],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":43,\"id\":50,\"before\":null},"
    "{\"op\":\"text\",\"id\":46,\"data\":\"Save\"},"
    "{\"op\":\"ins\",\"parent\":50,\"id\":46,\"before\":null},"
    /* A centered footer via text-align. */
    "{\"op\":\"el\",\"id\":22,\"tag\":\"p\",\"attrs\":[[\"class\",\"footer\"]],\"adopt\":\"body\"},"
    "{\"op\":\"ins\",\"parent\":2,\"id\":22,\"before\":null},"
    "{\"op\":\"text\",\"id\":23,\"data\":\"Rendered by libcui-widget + CUI\"},"
    "{\"op\":\"ins\",\"parent\":22,\"id\":23,\"before\":null}"
    "]";

/* Renderer -> host messages: kept quiet except for diagnostics, so CI logs stay
 * readable. A real app would route events/query replies here. */
static void on_message(const char *json, void *user) { (void)json; (void)user; }
static void on_diag(const char *kind, const char *detail, void *user) {
    (void)user;
    fprintf(stderr, "[diag] %s: %s\n", kind, detail);
}

/* Build the ops envelope, send it, paint once, and write <out>/<name>. */
static int render_theme(const char *out_dir, const char *name,
                        const char *theme, double width, double dpr) {
    cuiw_renderer_t r = cuiw_create(theme, width, dpr);
    if (!r) { fprintf(stderr, "cuiw_create failed (%s)\n", theme); return 1; }
    cuiw_set_message_callback(r, on_message, NULL);
    cuiw_set_diag_callback(r, on_diag, NULL);

    const char *envelope = "{\"type\":\"ops\",\"ops\":";
    size_t elen = strlen(envelope), olen = strlen(kAppOps);
    char *batch = (char *)malloc(elen + olen + 2);
    if (!batch) { cuiw_destroy(r); return 1; }
    memcpy(batch, envelope, elen);
    memcpy(batch + elen, kAppOps, olen);
    batch[elen + olen] = '}';
    batch[elen + olen + 1] = '\0';

    if (!cuiw_send(r, batch)) {
        fprintf(stderr, "cuiw_send rejected the document\n");
        free(batch); cuiw_destroy(r); return 1;
    }
    free(batch);

    cuiw_frame frame = cuiw_paint(r);
    if (!frame.rgba) { fprintf(stderr, "cuiw_paint produced no frame\n"); cuiw_destroy(r); return 1; }

    char path[1024];
    snprintf(path, sizeof path, "%s/%s", out_dir, name);
    if (!cui_write_png(path, frame.rgba, (int)frame.width, (int)frame.height,
                       (int)frame.stride)) {
        fprintf(stderr, "cui_write_png failed for %s\n", path);
        cuiw_frame_free(&frame); cuiw_destroy(r); return 1;
    }
    printf("wrote %s  (%u x %u px)\n", path, frame.width, frame.height);

    cuiw_frame_free(&frame);
    cuiw_destroy(r);
    return 0;
}

int main(int argc, char **argv) {
    const char *out_dir = argc > 1 ? argv[1] : ".";
    /* Reference geometry. Fixed on purpose: the golden images are tied to this
     * width and pixel ratio. Changing either is a deliberate golden update. */
    const double kWidth = 360.0;   /* a phone-message bubble width, in CSS px */
    const double kDpr   = 2.0;     /* physical px = CSS px * 2 */

    if (render_theme(out_dir, "widget-light.png", "light", kWidth, kDpr)) return 1;
    if (render_theme(out_dir, "widget-dark.png",  "dark",  kWidth, kDpr)) return 1;
    return 0;
}
