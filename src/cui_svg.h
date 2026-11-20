// Internal SVG geometry for libcui-widget (not part of the public C ABI).
//
// Flattens a bounded SVG subset into triangles and polylines that paint through
// CUI's software rasterizer. Nothing here parses a document — the renderer walks
// the SVG subtree, resolves fill/stroke through the normal CSS cascade, and hands
// us finished geometry; this file owns the shape math.
//
// Subset (v1): <rect> <circle> <ellipse> <line> <polyline> <polygon> and
// <path d> with M/L/H/V/C/Q/Z. Not modelled: <text> inside SVG (labels use the
// host's HTML text), gradients, transforms, filters, nested <svg>, and the arc
// (A) path command.
//
// Two constraints from CUI shape this file:
//  * cui_draw_polygon is a fan triangulation, so it only fills convex shapes.
//    Icons are often concave (stars, hearts), so we ear-clip every fill into
//    triangles and draw those.
//  * CUI has no ellipse primitive, so circles/ellipses flatten to N-gons.
#pragma once

#include <cui/cui.h>

#include <cstdint>
#include <vector>

namespace cuiw_svg {

struct Pt {
    double x = 0, y = 0;
};

struct Rgba {
    float r = 0, g = 0, b = 0, a = 0;
};

// One flattened shape in user (viewBox) coordinates. A fill carries a closed
// outline to triangulate; a stroke carries an open/closed polyline. A shape may
// hold both (fill + stroke). alpha 0 = "none" — skipped at paint.
struct Path {
    std::vector<Pt> pts;
    bool closed = false;   // fill outline is closed; stroke joins last->first
    Rgba fill{0, 0, 0, 0};
    Rgba stroke{0, 0, 0, 0};
    double stroke_width = 1.0;
};

// Maps user coords -> the element box [dx, dx+dw) x [dy, dy+dh) honouring
// preserveAspectRatio="xMidYMid meet" (the default). Returns false when the
// viewBox is degenerate (zero/negative width or height), in which case nothing
// should be painted.
struct Xform {
    double scale = 1, tx = 0, ty = 0;
};
bool makeXform(const char* viewBox, double dx, double dy, double dw, double dh,
               Xform& out);

Pt apply(const Xform& x, Pt p);

// Triangulates a simple (possibly concave) polygon by ear clipping. Degenerate
// and self-intersecting input yields a best-effort result, never a crash.
std::vector<Pt> triangulate(const std::vector<Pt>& poly);

// Paints the shapes through CUI. Coordinates go in as user-space and are
// transformed by `x`. Each fill is clipped to the viewport rect before the
// fan-triangulated triangles are drawn.
void paintPaths(cui_ctx* ui, const std::vector<Path>& paths, const Xform& x,
                cui_rect clip);

}  // namespace cuiw_svg
