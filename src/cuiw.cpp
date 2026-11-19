// cuiw — C ABI implementation. A thin, exception-safe bridge from the C API to
// the C++ CuiRenderer. Owns one CuiRenderer per handle and forwards calls;
// converts C++ frames/messages into the C-owned shapes the ABI promises.
#include "cuiw.h"

#include "twidget/cui_renderer.h"
#include "twidget/json.h"

#include <cui/cui.h>   /* CUI_VERSION_STRING for cuiw_cui_version() */

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

using twidget::CuiRenderer;
using twidget::Json;

/// The per-handle state. The C ABI sees an opaque pointer to this. It must be
/// the global-scope `struct cuiw_renderer` the header's typedef points at, so
/// it lives outside any namespace (an anonymous-namespace struct here would be
/// a distinct type and the handle would be incomplete to `delete`).
struct cuiw_renderer {
    std::unique_ptr<CuiRenderer> r;

    cuiw_message_fn msg_fn = nullptr;
    void *msg_user = nullptr;
    cuiw_diag_fn diag_fn = nullptr;
    void *diag_user = nullptr;
    cuiw_frame_fn frame_fn = nullptr;
    void *frame_user = nullptr;
};

namespace {

/// Copy a C++ frame into a C-owned cuiw_frame (malloc'd rgba the consumer
/// releases with cuiw_frame_free). Returns a frame with rgba == NULL on OOM.
cuiw_frame frame_copy(const CuiRenderer::Frame &f) {
    cuiw_frame cf{};
    cf.width = f.width;
    cf.height = f.height;
    cf.stride = f.width * 4;
    cf.paint_ms = f.paintMs;
    if (!f.rgba.empty()) {
        cf.rgba = static_cast<uint8_t *>(std::malloc(f.rgba.size()));
        if (cf.rgba)
            std::memcpy(cf.rgba, f.rgba.data(), f.rgba.size());
        else
            cf.width = cf.height = cf.stride = 0;
    }
    return cf;
}

} // namespace

extern "C" {

const char *cuiw_version(void) { return "0.1.0"; }

const char *cuiw_cui_version(void) {
    // Informational: the CUI this lib was built against, straight from CUI's
    // own version macro (cui.h), so it can never drift from the linked lib.
    return CUI_VERSION_STRING;
}

cuiw_renderer_t cuiw_create(const char *theme, double width_css, double dpr) {
    if (!theme) return nullptr;
    if (width_css <= 0) width_css = 320;
    if (dpr <= 0) dpr = 1;
    cuiw_renderer *h = new (std::nothrow) cuiw_renderer();
    if (!h) return nullptr;
    h->r = std::make_unique<CuiRenderer>(std::string(theme), width_css, dpr);
    // Wire the C++ callbacks through to the C function pointers. Safe to set
    // after construction: no message or frame is emitted until load()/send().
    h->r->onMessage = [h](const std::string &json) {
        if (h->msg_fn) h->msg_fn(json.c_str(), h->msg_user);
    };
    h->r->onDiag = [h](const std::string &kind, const std::string &detail) {
        if (h->diag_fn) h->diag_fn(kind.c_str(), detail.c_str(), h->diag_user);
    };
    h->r->onPaint = [h](const CuiRenderer::Frame &f) {
        if (!h->frame_fn) return;
        cuiw_frame cf = frame_copy(f);
        if (cf.rgba) h->frame_fn(&cf, h->frame_user);
        cuiw_frame_free(&cf);
    };
    return h;
}

void cuiw_destroy(cuiw_renderer_t r) { delete r; }

void cuiw_set_theme(cuiw_renderer_t r, const char *theme) {
    if (!r || !r->r || !theme) return;
    r->r->setTheme(std::string(theme));
}

void cuiw_set_width(cuiw_renderer_t r, double width_css) {
    if (!r || !r->r) return;
    r->r->setWidth(width_css);
}

bool cuiw_send(cuiw_renderer_t r, const char *json) {
    if (!r || !r->r || !json) return false;
    // Reject unparseable input up front so the bool means something: the
    // renderer would silently ignore it, the host would rather know now.
    if (!Json::parse(std::string(json))) return false;
    r->r->send(std::string(json));
    return true;
}

void cuiw_touch(cuiw_renderer_t r, int action, double x, double y,
                int pointer, bool primary) {
    if (!r || !r->r) return;
    r->r->touch(action, x, y, pointer, primary);
}

void cuiw_click(cuiw_renderer_t r, double x, double y) {
    if (!r || !r->r) return;
    r->r->click(x, y);
}

cuiw_frame cuiw_paint(cuiw_renderer_t r) {
    cuiw_frame cf{};
    if (!r || !r->r) return cf;
    return frame_copy(r->r->paint());
}

void cuiw_frame_free(cuiw_frame *f) {
    if (!f) return;
    std::free(f->rgba);
    f->rgba = nullptr;
    f->width = f->height = f->stride = 0;
    f->paint_ms = 0;
}

void cuiw_set_message_callback(cuiw_renderer_t r, cuiw_message_fn fn, void *user) {
    if (!r) return;
    r->msg_fn = fn;
    r->msg_user = user;
}

void cuiw_set_diag_callback(cuiw_renderer_t r, cuiw_diag_fn fn, void *user) {
    if (!r) return;
    r->diag_fn = fn;
    r->diag_user = user;
}

// The frame callback receives a lib-allocated frame; the callback owns it and
// must release it with cuiw_frame_free. Wire that contract here.
void cuiw_set_frame_callback(cuiw_renderer_t r, cuiw_frame_fn fn, void *user) {
    if (!r) return;
    r->frame_fn = fn;
    r->frame_user = user;
}

void cuiw_start_clock(cuiw_renderer_t r) {
    if (!r || !r->r) return;
    r->r->startClock();
}

void cuiw_stop_clock(cuiw_renderer_t r) {
    if (!r || !r->r) return;
    r->r->stopClock();
}

bool cuiw_has_feature(cuiw_feature f) {
    switch (f) {
        // The layout engine's supported set (see docs/cui-renderer.md).
        case CUIW_FEATURE_BLOCK:
        case CUIW_FEATURE_FLEX:
        case CUIW_FEATURE_FLEX_WRAP:
        case CUIW_FEATURE_ID_SELECTOR:
        case CUIW_FEATURE_CLASS_SELECTOR:
        case CUIW_FEATURE_TAG_SELECTOR:
        case CUIW_FEATURE_BORDER_RADIUS:
        case CUIW_FEATURE_TEXT_ALIGN:
        case CUIW_FEATURE_FONT_WEIGHT:
        case CUIW_FEATURE_MIN_HEIGHT:
        case CUIW_FEATURE_VERTICAL_ALIGN:
        case CUIW_FEATURE_CSS_VARIABLES:
        case CUIW_FEATURE_OPACITY_COMPOSITING:
        case CUIW_FEATURE_PSEUDO_CLASSES:
        case CUIW_FEATURE_MEDIA_QUERIES:
        case CUIW_FEATURE_FLOATS:
        case CUIW_FEATURE_GRID:
        case CUIW_FEATURE_PERCENT_HEIGHT:
        case CUIW_FEATURE_INLINE_LAYOUT:
        case CUIW_FEATURE_TABLES:
        case CUIW_FEATURE_OVERFLOW_SCROLL:
        case CUIW_FEATURE_TEXT_DECORATION:
        case CUIW_FEATURE_ABSOLUTE_POSITION:
            return true;
        default:
            return false;
    }
}

const char *cuiw_feature_name(cuiw_feature f) {
    switch (f) {
        case CUIW_FEATURE_BLOCK: return "block";
        case CUIW_FEATURE_FLEX: return "flex";
        case CUIW_FEATURE_FLEX_WRAP: return "flex-wrap";
        case CUIW_FEATURE_ID_SELECTOR: return "id-selector";
        case CUIW_FEATURE_CLASS_SELECTOR: return "class-selector";
        case CUIW_FEATURE_TAG_SELECTOR: return "tag-selector";
        case CUIW_FEATURE_BORDER_RADIUS: return "border-radius";
        case CUIW_FEATURE_TEXT_ALIGN: return "text-align";
        case CUIW_FEATURE_FONT_WEIGHT: return "font-weight";
        case CUIW_FEATURE_MIN_HEIGHT: return "min-height";
        case CUIW_FEATURE_VERTICAL_ALIGN: return "vertical-align";
        case CUIW_FEATURE_ABSOLUTE_POSITION: return "position:absolute";
        case CUIW_FEATURE_PERCENT_HEIGHT: return "percent-height";
        case CUIW_FEATURE_INLINE_LAYOUT: return "inline-layout";
        case CUIW_FEATURE_TABLES: return "tables";
        case CUIW_FEATURE_GRID: return "grid";
        case CUIW_FEATURE_FLOATS: return "floats";
        case CUIW_FEATURE_CSS_VARIABLES: return "css-variables";
        case CUIW_FEATURE_MEDIA_QUERIES: return "media-queries";
        case CUIW_FEATURE_TRANSITIONS: return "transitions";
        case CUIW_FEATURE_KEYFRAMES: return "keyframes";
        case CUIW_FEATURE_OVERFLOW_SCROLL: return "overflow-scroll";
        case CUIW_FEATURE_TEXT_DECORATION: return "text-decoration";
        case CUIW_FEATURE_OPACITY_COMPOSITING: return "opacity-compositing";
        case CUIW_FEATURE_PSEUDO_CLASSES: return "pseudo-classes";
        case CUIW_FEATURE_CANVAS: return "canvas";
        case CUIW_FEATURE_INLINE_SVG: return "inline-svg";
        default: return nullptr;
    }
}

size_t cuiw_feature_matrix(uint8_t *out, size_t n) {
    if (!out) return 0;
    size_t count = n < static_cast<size_t>(CUIW_FEATURE__COUNT)
                       ? n
                       : static_cast<size_t>(CUIW_FEATURE__COUNT);
    for (size_t i = 0; i < count; i++)
        out[i] = cuiw_has_feature(static_cast<cuiw_feature>(i)) ? 1 : 0;
    return count;
}

} // extern "C"