// cuiw — the CUI widget-renderer library (public C ABI).
//
// A standalone, dependency-light renderer that turns a stream of RenderOps
// (the document contract shared with the Telegram widget host) into pixels,
// painting through CUI's software rasterizer. No WebView, no script engine,
// no network, no sandbox: the caller is trusted and drives everything.
//
// This is the "extra handle" layered on top of CUI: CUI supplies the drawing
// primitives (rects, text, corners, the rasterizer); cuiw supplies the widget
// layer above it (DOM tree, CSS cascade + layout, hit-testing, events).
//
// Isolation: libcui-widget depends only on libcui and the C runtime. It does
// NOT link the widget sandbox (no QuickJS, no validator, no CPU/memory caps) —
// an application that starts with CUI can embed this lib and render widgets
// itself, with no ceiling but the host's own resources.
//
// Threading: every call on a handle serializes against an internal mutex, so
// calls may come from any thread, but the host should paint from one thread.
// The internal frame clock (see cuiw_set_frame_callback) runs on its own
// thread and invokes callbacks there.
//
// Memory: cuiw_paint returns a cuiw_frame the caller owns; release it with
// cuiw_frame_free. Strings passed IN are borrowed; strings passed OUT are
// borrowed and valid until the next call on the handle.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque renderer handle. Create with cuiw_create, release with cuiw_destroy.
typedef struct cuiw_renderer *cuiw_renderer_t;

/// Library version, semver, e.g. "0.1.0".
const char *cuiw_version(void);

/// The CUI version this lib was built against, e.g. "1.0". Informational.
const char *cuiw_cui_version(void);

// ───────────── lifecycle ─────────────

/// Create a renderer. `theme` is "light" or "dark"; `width_css` is the bubble
/// width in CSS logical px; `dpr` is the device pixel ratio (physical px =
/// CSS px × dpr). Returns NULL on failure (bad args or out of memory).
cuiw_renderer_t cuiw_create(const char *theme, double width_css, double dpr);

/// Destroy a renderer and free its resources. NULL is a no-op. Joins the
/// internal frame-clock thread.
void cuiw_destroy(cuiw_renderer_t r);

// ───────────── configuration ─────────────

/// Change the theme ("light" | "dark"). Relayouts and schedules a frame.
void cuiw_set_theme(cuiw_renderer_t r, const char *theme);

/// Change the bubble width (CSS px). Relayouts and schedules a frame.
void cuiw_set_width(cuiw_renderer_t r, double width_css);

// ───────────── document ─────────────

/// Feed one host→renderer message: either a single JSON object
/// ({type:"ops"|"query"|"theme"|…}) or a JSON array of them. This is the same
/// message contract the widget host speaks (docs/protocol.md). The most common
/// message is {type:"ops", ops:[…]} — a batch of RenderOps describing the
/// document (el/text/attr/style/css/ins/rm).
///
/// Replies (query results, events, the ready announcement, the reported
/// document size) come back through the message callback.
///
/// `json` is borrowed and only read during the call. Returns false if `r` or
/// `json` is NULL, or the JSON does not parse.
bool cuiw_send(cuiw_renderer_t r, const char *json);

/// A pointer event in CSS px. action: 0 = down, 1 = move, 2 = up, 3 = cancel.
/// `pointer` is the pointer id, `primary` whether it is the primary pointer.
/// Emits click/pointerdown/… messages through the message callback.
void cuiw_touch(cuiw_renderer_t r, int action, double x, double y,
                int pointer, bool primary);

/// Convenience: a tap (down + up) at one point.
void cuiw_click(cuiw_renderer_t r, double x, double y);

// ───────────── painting ─────────────

/// A rasterized frame: premultiplied RGBA8, row-major, `width × height`
/// physical px (`stride` = width × 4). The caller owns it and must release it
/// with cuiw_frame_free.
typedef struct cuiw_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride;      // bytes per row (width × 4)
    uint8_t *rgba;        // premultiplied RGBA8; NULL if the paint failed
    double paint_ms;      // how long the raster took
} cuiw_frame;

/// Paint the current document now and return the frame. The caller owns it
/// (cuiw_frame_free). On failure `rgba` is NULL. This is synchronous and does
/// not touch the frame clock.
cuiw_frame cuiw_paint(cuiw_renderer_t r);

/// Release a frame returned by cuiw_paint. NULL / NULL->rgba is a no-op.
void cuiw_frame_free(cuiw_frame *f);

// ───────────── callbacks ─────────────

/// Renderer → host message (JSON, UTF-8, NUL-terminated). Borrowed for the
/// duration of the callback only. Delivered on the calling thread for send/
/// touch, and on the frame-clock thread for clock-driven messages.
typedef void (*cuiw_message_fn)(const char *json, void *user);

/// A diagnostic: `kind` (e.g. "cui-init", "renderer-violation") and `detail`.
typedef void (*cuiw_diag_fn)(const char *kind, const char *detail, void *user);

/// A frame the clock produced. The lib owns it and frees it as soon as the
/// callback returns, so COPY what you need inside the callback — do not free
/// it and do not retain `frame->rgba` past the call.
typedef void (*cuiw_frame_fn)(const cuiw_frame *frame, void *user);

/// Register the message callback (pass NULL to clear). `user` is passed back.
void cuiw_set_message_callback(cuiw_renderer_t r, cuiw_message_fn fn, void *user);

/// Register the diagnostic callback (pass NULL to clear).
void cuiw_set_diag_callback(cuiw_renderer_t r, cuiw_diag_fn fn, void *user);

/// Register the frame-clock callback (pass NULL to clear). The lib owns each
/// delivered frame and frees it when the callback returns; copy inside.
void cuiw_set_frame_callback(cuiw_renderer_t r, cuiw_frame_fn fn, void *user);

/// Start the internal frame clock (default: running). The clock delivers
/// clock-driven messages and frames to the callbacks on its own thread.
void cuiw_start_clock(cuiw_renderer_t r);

/// Stop the internal frame clock. The host can then drive painting purely with
/// cuiw_paint. Safe to call from any thread; joins the clock thread.
void cuiw_stop_clock(cuiw_renderer_t r);

// ───────────── capabilities (the honest CSS matrix) ─────────────

/// A capability the layout engine either has or does not. Query with
/// cuiw_has_feature rather than assuming: an unsupported feature degrades
/// (documented in docs/cui-renderer.md), it does not silently mis-render.
typedef enum cuiw_feature {
    CUIW_FEATURE_BLOCK = 0,
    CUIW_FEATURE_FLEX,
    CUIW_FEATURE_FLEX_WRAP,
    CUIW_FEATURE_ID_SELECTOR,
    CUIW_FEATURE_CLASS_SELECTOR,
    CUIW_FEATURE_TAG_SELECTOR,
    CUIW_FEATURE_BORDER_RADIUS,
    CUIW_FEATURE_TEXT_ALIGN,
    CUIW_FEATURE_FONT_WEIGHT,
    CUIW_FEATURE_MIN_HEIGHT,
    CUIW_FEATURE_VERTICAL_ALIGN,
    CUIW_FEATURE_CSS_VARIABLES,
    CUIW_FEATURE_OPACITY_COMPOSITING,
    CUIW_FEATURE_PSEUDO_CLASSES,   ///< :disabled + :first-child/:last-child/:nth-child
    CUIW_FEATURE_MEDIA_QUERIES,    ///< @media min-width / max-width against doc width
    CUIW_FEATURE_FLOATS,           ///< float:left/right, inline + block wrap around
    /// Not yet: the list below is what the engine still lacks.
    CUIW_FEATURE_ABSOLUTE_POSITION,
    CUIW_FEATURE_PERCENT_HEIGHT,
    CUIW_FEATURE_INLINE_LAYOUT,
    CUIW_FEATURE_TABLES,
    CUIW_FEATURE_GRID,
    CUIW_FEATURE_TRANSITIONS,
    CUIW_FEATURE_KEYFRAMES,
    CUIW_FEATURE_OVERFLOW_SCROLL,
    CUIW_FEATURE_TEXT_DECORATION,
    CUIW_FEATURE_CANVAS,
    CUIW_FEATURE_INLINE_SVG,
    CUIW_FEATURE__COUNT
} cuiw_feature;

/// Returns true if the layout engine supports `feature`. Out-of-range → false.
bool cuiw_has_feature(cuiw_feature f);

/// Human-readable name of a feature, or NULL if out of range.
const char *cuiw_feature_name(cuiw_feature f);

/// Fill `out` (length `n`, at least CUIW_FEATURE__COUNT) with 0/1 per feature.
/// Returns the number of features written.
size_t cuiw_feature_matrix(uint8_t *out, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif