// Internal SVG geometry — see cui_svg.h for the subset and the CUI constraints.
#include "cui_svg.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace cuiw_svg {
namespace {

// Cross product of (b-a) x (c-a). Positive = counter-clockwise turn.
double cross(Pt a, Pt b, Pt c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// True when the triangle (a,b,c) contains p. Points exactly on an edge count as
// inside, so collinear ears still clip cleanly.
bool pointInTri(Pt p, Pt a, Pt b, Pt c) {
    const double e = 1e-9;
    double d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
    bool has_neg = (d1 < -e) || (d2 < -e) || (d3 < -e);
    bool has_pos = (d1 > e) || (d2 > e) || (d3 > e);
    return !(has_neg && has_pos);
}

// Drops consecutive duplicate points (a closed path repeats its start).
std::vector<Pt> dedupe(const std::vector<Pt>& in) {
    std::vector<Pt> out;
    out.reserve(in.size());
    for (const Pt& p : in) {
        if (!out.empty()) {
            const Pt& q = out.back();
            if (std::fabs(p.x - q.x) < 1e-9 && std::fabs(p.y - q.y) < 1e-9) continue;
        }
        out.push_back(p);
    }
    // A closed shape whose last point equals its first.
    while (out.size() > 1) {
        const Pt& f = out.front();
        const Pt& l = out.back();
        if (std::fabs(f.x - l.x) < 1e-9 && std::fabs(f.y - l.y) < 1e-9) out.pop_back();
        else break;
    }
    return out;
}

double polygonArea(const std::vector<Pt>& v) {
    double a = 0;
    for (size_t i = 0; i < v.size(); i++) {
        const Pt& p = v[i];
        const Pt& q = v[(i + 1) % v.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

// True when every consecutive turn has the same sign — the polygon is convex and
// can be handed to cui_draw_polygon as one shape. That matters for fill quality:
// a circle/ellipse is a dense outline, and triangulating it into a fan means each
// triangle is antialiased separately, so the shared interior edges accumulate
// partial coverage from both sides and show up as faint radial seams. One convex
// polygon has no interior edges, so it fills clean.
bool isConvex(const std::vector<Pt>& v) {
    if (v.size() < 4) return true;
    int sign = 0;
    const size_t n = v.size();
    for (size_t i = 0; i < n; i++) {
        const Pt& a = v[i];
        const Pt& b = v[(i + 1) % n];
        const Pt& c = v[(i + 2) % n];
        double cr = cross(a, b, c);
        if (std::fabs(cr) < 1e-12) continue;   // collinear — no turn, ignore
        int s = cr > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

}  // namespace

bool makeXform(const char* viewBox, double dx, double dy, double dw, double dh,
               Xform& out) {
    out = Xform{};
    if (!viewBox || dw <= 0 || dh <= 0) return false;
    double minx = 0, miny = 0, vw = 0, vh = 0;
    // viewBox="minx miny width height" — parse four numbers, comma/space separated.
    int n = std::sscanf(viewBox, "%lf %lf %lf %lf", &minx, &miny, &vw, &vh);
    if (n != 4 || vw <= 0 || vh <= 0) return false;
    // "meet": scale uniformly to fit, centre in the box (xMidYMid).
    double s = std::min(dw / vw, dh / vh);
    out.scale = s;
    out.tx = dx + (dw - vw * s) * 0.5 - minx * s;
    out.ty = dy + (dh - vh * s) * 0.5 - miny * s;
    return true;
}

Pt apply(const Xform& x, Pt p) {
    return Pt{x.tx + p.x * x.scale, x.ty + p.y * x.scale};
}

std::vector<Pt> triangulate(const std::vector<Pt>& polyIn) {
    std::vector<Pt> tris;
    std::vector<Pt> v = dedupe(polyIn);
    if (v.size() < 3) return tris;
    if (v.size() == 3) {
        tris.insert(tris.end(), v.begin(), v.end());
        return tris;
    }
    // Work in the polygon's own winding so the ear test is orientation-agnostic.
    if (polygonArea(v) < 0) std::reverse(v.begin(), v.end());
    // Guard against pathological input blowing up the loop.
    const size_t max_iter = v.size() * v.size() + 8;
    size_t iter = 0;
    std::vector<size_t> idx(v.size());
    for (size_t i = 0; i < v.size(); i++) idx[i] = i;
    while (idx.size() > 3 && iter++ < max_iter) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); i++) {
            size_t ip = idx[(i + idx.size() - 1) % idx.size()];
            size_t ic = idx[i];
            size_t in = idx[(i + 1) % idx.size()];
            Pt a = v[ip], b = v[ic], c = v[in];
            if (cross(a, b, c) <= 1e-12) continue;   // reflex or collinear — not an ear
            bool ok = true;
            for (size_t j = 0; j < idx.size() && ok; j++) {
                size_t ij = idx[j];
                if (ij == ip || ij == ic || ij == in) continue;
                if (pointInTri(v[ij], a, b, c)) ok = false;
            }
            if (!ok) continue;
            tris.push_back(a);
            tris.push_back(b);
            tris.push_back(c);
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;   // no ear found (self-intersecting input) — emit rest as fan
    }
    if (idx.size() == 3) {
        tris.push_back(v[idx[0]]);
        tris.push_back(v[idx[1]]);
        tris.push_back(v[idx[2]]);
    } else if (idx.size() > 3) {
        // Fallback fan from vertex 0 — wrong for concave input but never crashes.
        for (size_t i = 1; i + 1 < idx.size(); i++) {
            tris.push_back(v[idx[0]]);
            tris.push_back(v[idx[i]]);
            tris.push_back(v[idx[i + 1]]);
        }
    }
    return tris;
}

void paintPaths(cui_ctx* ui, const std::vector<Path>& paths, const Xform& x,
                cui_rect clip) {
    cui_push_clip(ui, clip);
    for (const Path& p : paths) {
        if (p.fill.a > 0 && p.pts.size() >= 3) {
            cui_color fc{static_cast<uint8_t>(std::clamp(p.fill.r * 255.f, 0.f, 255.f)),
                         static_cast<uint8_t>(std::clamp(p.fill.g * 255.f, 0.f, 255.f)),
                         static_cast<uint8_t>(std::clamp(p.fill.b * 255.f, 0.f, 255.f)),
                         static_cast<uint8_t>(std::clamp(p.fill.a * 255.f, 0.f, 255.f))};
            std::vector<Pt> dv;
            dv.reserve(p.pts.size());
            for (const Pt& t : p.pts) dv.push_back(apply(x, t));
            if (isConvex(dv)) {
                // One convex polygon — no interior edges, so no radial AA seams
                // (the fan-triangulation seams a circle/ellipse otherwise shows).
                std::vector<cui_vec2> poly(dv.size());
                for (size_t k = 0; k < dv.size(); k++)
                    poly[k] = cui_vec2{(float)dv[k].x, (float)dv[k].y};
                cui_draw_polygon(ui, poly.data(), (int)poly.size(), fc);
            } else {
                // Concave: ear-clip, then each triangle is its own (convex)
                // polygon — cui_draw_polygon tessellates a single polygon's
                // vertices, so concatenating every triangle would build one
                // bogus overlapping shape.
                std::vector<Pt> tris = triangulate(p.pts);
                std::vector<cui_vec2> tri(3);
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    for (int k = 0; k < 3; k++) {
                        Pt t = apply(x, tris[i + k]);
                        tri[k] = cui_vec2{(float)t.x, (float)t.y};
                    }
                    cui_draw_polygon(ui, tri.data(), 3, fc);
                }
            }
        }
        if (p.stroke.a > 0 && p.pts.size() >= 2) {
            cui_color sc{static_cast<uint8_t>(std::clamp(p.stroke.r * 255.f, 0.f, 255.f)),
                         static_cast<uint8_t>(std::clamp(p.stroke.g * 255.f, 0.f, 255.f)),
                         static_cast<uint8_t>(std::clamp(p.stroke.b * 255.f, 0.f, 255.f)),
                         static_cast<uint8_t>(std::clamp(p.stroke.a * 255.f, 0.f, 255.f))};
            std::vector<cui_vec2> pts;
            pts.reserve(p.pts.size());
            for (const Pt& t : p.pts) pts.push_back(cui_vec2{(float)apply(x, t).x,
                                                             (float)apply(x, t).y});
            cui_draw_polyline(ui, pts.data(), (int)pts.size(),
                              (float)p.stroke_width, sc, p.closed);
        }
    }
    cui_pop_clip(ui);
}

}  // namespace cuiw_svg
