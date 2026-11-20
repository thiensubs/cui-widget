#include "twidget/cui_renderer.h"

#include "cui_svg.h"
#include "cui_img.h"

#include "twidget/json.h"

#include <cui/cui.h>
#include <cui/cui_render.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace twidget {

namespace {

constexpr double kTickSeconds = 1.0 / 60;
// Every property the transition engine animates. "all" fans out to this list;
// readAnim() returns set=false for any a node doesn't actually have a value for,
// so a fan-out only acts on properties present on the node.
const char* const kAnimatableAll[] = {
    "opacity", "color", "background-color", "border-color", "width", "height",
    "min-height", "border-radius", "border-width", "font-size", "gap",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "margin-top", "margin-right", "margin-bottom", "margin-left",
    "top", "right", "bottom", "left",
};
constexpr double kDefaultFontSize = 15.0;
constexpr double kDefaultLineHeight = 1.35;
constexpr double kRootPaddingX = 12.0, kRootPaddingY = 8.0;

// ───────────── small CSS value helpers ─────────────

struct Rgba { float r = 0, g = 0, b = 0, a = 1; };

bool parseColor(const std::string& in, Rgba& out) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) if (c != ' ' && c != '\t') s.push_back((char)std::tolower((unsigned char)c));
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    if (!s.empty() && s[0] == '#') {
        std::string h = s.substr(1);
        if (h.size() == 3 || h.size() == 4) {
            std::string e;
            for (char c : h) { e.push_back(c); e.push_back(c); }
            h = e;
        }
        if (h.size() != 6 && h.size() != 8) return false;
        int v[8];
        for (size_t i = 0; i < h.size(); i++) { v[i] = hexVal(h[i]); if (v[i] < 0) return false; }
        out.r = (v[0] * 16 + v[1]) / 255.0f;
        out.g = (v[2] * 16 + v[3]) / 255.0f;
        out.b = (v[4] * 16 + v[5]) / 255.0f;
        out.a = h.size() == 8 ? (v[6] * 16 + v[7]) / 255.0f : 1.0f;
        return true;
    }
    if (s.compare(0, 4, "rgba") == 0 || s.compare(0, 3, "rgb") == 0) {
        size_t p = s.find('(');
        size_t q = s.find(')');
        if (p == std::string::npos || q == std::string::npos) return false;
        std::string body = s.substr(p + 1, q - p - 1);
        double v[4] = {0, 0, 0, 1};
        int n = 0;
        size_t i = 0;
        while (i < body.size() && n < 4) {
            while (i < body.size() && (body[i] == ',' || body[i] == ' ')) i++;
            size_t st = i;
            while (i < body.size() && body[i] != ',' && body[i] != ' ') i++;
            if (i > st) v[n++] = std::atof(body.substr(st, i - st).c_str());
        }
        if (n < 3) return false;
        // rgb() / rgba() may be 0-255 or 0%-100%.
        auto ch = [](double x) { return std::clamp(x, 0.0, 255.0) / 255.0f; };
        out.r = ch(v[0]); out.g = ch(v[1]); out.b = ch(v[2]);
        out.a = n >= 4 ? (float)std::clamp(v[3], 0.0, 1.0) : 1.0f;
        return true;
    }
    struct Named { const char* n; Rgba c; };
    static const Named kNamed[] = {
        {"transparent", {0, 0, 0, 0}},   {"white", {1, 1, 1, 1}},
        {"black", {0, 0, 0, 1}},         {"red", {1, 0, 0, 1}},
        {"green", {0, 0.5f, 0, 1}},      {"blue", {0, 0, 1, 1}},
        {"gray", {0.5f, 0.5f, 0.5f, 1}}, {"grey", {0.5f, 0.5f, 0.5f, 1}},
        {"silver", {0.75f, 0.75f, 0.75f, 1}},
    };
    for (const auto& k : kNamed) if (s == k.n) { out = k.c; return true; }
    return false;
}

bool parseLenPx(const std::string& v, double& out, bool& is_pct) {
    std::string s;
    for (char c : v) if (!std::isspace((unsigned char)c)) s.push_back(c);
    if (s.empty()) return false;
    is_pct = false;
    double mult = 1.0;
    if (s.size() >= 1 && s.back() == '%') {
        is_pct = true;
        s.pop_back();
    }
    // A length: a number optionally followed by px. em/rem resolve against the
    // element's own font-size by the caller; here we treat em as px * 16.
    else if (s.size() >= 2 && s.substr(s.size() - 2) == "px") s = s.substr(0, s.size() - 2);
    else if (s.size() >= 2 && s.substr(s.size() - 2) == "em") { s = s.substr(0, s.size() - 2); mult = kDefaultFontSize; }
    else if (s.size() >= 3 && s.substr(s.size() - 3) == "rem") { s = s.substr(0, s.size() - 3); mult = kDefaultFontSize; }
    char* end = nullptr;
    double x = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    out = x * mult;
    return true;
}
// Convenience wrapper for callers that do not care about percent.
bool parseLenPx(const std::string& v, double& out) {
    bool pct = false;
    return parseLenPx(v, out, pct);
}

// Splits a CSS value into whitespace-separated tokens, keeping the inside of
// parentheses together — so "rgba(0, 0, 0, 0.2)" is one token even though it
// holds spaces. Used by box-shadow, whose color may be a function.
std::vector<std::string> splitParenAware(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char ch : s) {
        if (ch == '(') { depth++; cur.push_back(ch); }
        else if (ch == ')') { if (depth > 0) depth--; cur.push_back(ch); }
        else if (std::isspace((unsigned char)ch) && depth == 0) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(ch);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string trimCopy(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Parses "linear-gradient(<direction>, <color>, <color>)". The direction is
// optional (defaults to "to bottom"); "to top" swaps the two stops. Only a
// two-stop, vertical gradient is modelled — other directions and extra stops
// fall back to a top->bottom render. Returns false if two colors can't be read.
// Parses "linear-gradient(<direction>, <color> [pos], <color> [pos], ...)" into
// ordered stops. The direction is optional (default "to bottom"); "to top"
// reverses the stop order. Each stop is an (offset, color) pair with offset in
// [0,1]: a stop may carry an explicit position ("<color> 40%"); stops without one
// are spaced evenly between their positioned neighbours. Returns false if fewer
// than two colors can be read.
// A parsed background gradient. Covers linear (by angle), radial (circle from
// the box centre), and their repeating forms. `stops` are (offset, color) with
// offset in [0,1] along the gradient line; for repeating gradients the offsets
// tile beyond [0,1] and are wrapped when sampled.
struct Gradient {
    bool has = false;
    bool radial = false;        // false = linear (by angle)
    bool repeating = false;
    double angle = 180.0;       // degrees clockwise from "to top"; 180 = to bottom
    std::vector<std::pair<double, Rgba>> stops;
};

// Parses any background gradient into a Gradient descriptor: linear-gradient
// (by angle or "to <side>"), radial-gradient (circle from the box centre), and
// their repeating- forms. Stops are (offset, color) with offset along the
// gradient line; a stop may carry an explicit position ("<color> 40%"), and
// unpositioned stops are spaced evenly between their positioned neighbours.
// Returns false for a non-gradient value (plain color, url(), etc.).
bool parseGradient(const std::string& value, Gradient& g) {
    g = Gradient{};
    size_t p = value.find('(');
    if (p == std::string::npos) return false;
    std::string kind = value.substr(0, p);
    for (char& c : kind) c = (char)std::tolower((unsigned char)c);
    bool repeating = false;
    if (kind.rfind("repeating-", 0) == 0) { repeating = true; kind.erase(0, 10); }
    if (kind == "linear-gradient") g.radial = false;
    else if (kind == "radial-gradient") g.radial = true;
    else return false;
    g.repeating = repeating;
    g.has = true;

    size_t q = value.rfind(')');
    if (q == std::string::npos || q < p) return false;
    // Split the inside on top-level commas (a color function holds commas too).
    std::vector<std::string> parts;
    {
        std::string cur;
        int depth = 0;
        for (char ch : value.substr(p + 1, q - p - 1)) {
            if (ch == '(') depth++;
            else if (ch == ')') { if (depth > 0) depth--; }
            if (ch == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
            else cur.push_back(ch);
        }
        if (!cur.empty()) parts.push_back(cur);
    }
    for (auto& s : parts) s = trimCopy(s);
    if (parts.empty()) return false;

    // The first part may be a direction ("to bottom", "45deg") for linear, or a
    // shape ("circle", "ellipse") for radial — either way not a color stop.
    size_t ci = 0;
    std::string first = parts[0];
    for (char& c : first) c = (char)std::tolower((unsigned char)c);
    if (!g.radial) {
        // Angle: "<N>deg" clockwise from to-top. Or "to <side>".
        double ang;
        char unit = 0;
        if (std::sscanf(first.c_str(), "%lf%c", &ang, &unit) >= 1 &&
            first.find("deg") != std::string::npos) {
            g.angle = ang;
        } else if (first.rfind("to ", 0) == 0) {
            g.angle = first.find("right") != std::string::npos ? 90.0
                    : first.find("left") != std::string::npos ? 270.0
                    : first.find("top") != std::string::npos ? 0.0
                    : 180.0;   // to bottom
        } else {
            g.angle = 180.0;   // no direction: default to bottom
        }
    }
    // Radial v1: a circle centred in the box; shape/size/position keywords are
    // consumed here if present but only a centred circle is rendered.
    if (g.radial && (first == "circle" || first == "ellipse" ||
                     first.find("at ") != std::string::npos ||
                     first.find("closest") != std::string::npos ||
                     first.find("farthest") != std::string::npos)) {
        ci = 1;
    }

    // Collect (offset, color); offset < 0 marks "not given".
    std::vector<std::pair<double, Rgba>> raw;
    for (size_t i = ci; i < parts.size(); i++) {
        // A stop is "<color>" or "<color> <position>". Split paren-aware so a
        // rgba() color stays one token.
        auto toks = splitParenAware(parts[i]);
        if (toks.empty()) continue;
        Rgba c;
        if (!parseColor(toks[0], c)) continue;
        double off = -1.0;
        if (toks.size() >= 2) {
            std::string pos = toks[1];
            if (!pos.empty() && pos.back() == '%') {
                off = std::atof(pos.c_str()) / 100.0;
                if (off < 0) off = 0;
                if (off > 1) off = 1;
            }
        }
        raw.push_back({off, c});
    }
    if (raw.size() < 2) return false;
    // Assign offsets: keep the explicit ones, space the rest evenly between the
    // brackets they sit in (CSS: even distribution when no position is given).
    // For a repeating gradient the last stop keeps its actual position — it
    // defines the tile period, which may be < 1 so the pattern repeats.
    if (raw.front().first < 0) raw.front().first = 0.0;
    if (!g.repeating && raw.back().first < 0) raw.back().first = 1.0;
    if (raw.back().first <= 0) raw.back().first = 1.0;
    for (size_t i = 1; i + 1 < raw.size(); i++) {
        if (raw[i].first >= 0) continue;
        size_t lo = i - 1, hi = i + 1;
        while (lo > 0 && raw[lo].first < 0) lo--;
        while (hi + 1 < raw.size() && raw[hi].first < 0) hi++;
        double a = raw[lo].first, b = raw[hi].first;
        if (b < a) b = a;
        double span = (double)(hi - lo);
        raw[i].first = a + (b - a) * (double)(i - lo) / span;
    }
    // Enforce non-decreasing offsets (CSS clamps out-of-order stops).
    for (size_t i = 1; i < raw.size(); i++)
        if (raw[i].first < raw[i - 1].first) raw[i].first = raw[i - 1].first;
    g.stops = raw;
    return g.stops.size() >= 2;
}

// Parses "box-shadow: <x> <y> <blur> <spread> <color>". The offsets are required
// (CSS); blur and spread are optional and default to 0, the color to a soft black.
// "inset" is recognised and skipped (not modelled). Returns false without the two
// offsets, so a bad value degrades to no shadow rather than a stray blob.
bool parseBoxShadow(const std::string& value, double& x, double& y,
                    double& blur, double& spread, Rgba& color) {
    double len[4] = {0, 0, 0, 0};
    int nl = 0;
    bool has_color = false;
    Rgba c{0, 0, 0, 0};
    for (const std::string& t : splitParenAware(value)) {
        if (t == "inset") continue;
        double px;
        if (parseLenPx(t, px)) { if (nl < 4) len[nl++] = px; continue; }
        Rgba cc;
        if (parseColor(t, cc)) { c = cc; has_color = true; }
    }
    if (nl < 2) return false;
    x = len[0]; y = len[1];
    blur = nl > 2 ? len[2] : 0.0;
    spread = nl > 3 ? len[3] : 0.0;
    color = has_color ? c : Rgba{0, 0, 0, 0.35f};
    return true;
}

// Samples a sorted (offset, color) stop list at offset t in [0,1], linearly
// interpolating between the two stops that bracket t. Clamps to the end colors.
Rgba sampleGradStops(const std::vector<std::pair<double, Rgba>>& stops, double t) {
    if (stops.empty()) return Rgba{0, 0, 0, 0};
    if (t <= stops.front().first) return stops.front().second;
    if (t >= stops.back().first) return stops.back().second;
    for (size_t i = 0; i + 1 < stops.size(); i++) {
        double a = stops[i].first, b = stops[i + 1].first;
        if (t >= a && t <= b) {
            double local = (b > a) ? (t - a) / (b - a) : 0.0;
            Rgba out;
            out.r = stops[i].second.r + (stops[i + 1].second.r - stops[i].second.r) * (float)local;
            out.g = stops[i].second.g + (stops[i + 1].second.g - stops[i].second.g) * (float)local;
            out.b = stops[i].second.b + (stops[i + 1].second.b - stops[i].second.b) * (float)local;
            out.a = stops[i].second.a + (stops[i + 1].second.a - stops[i].second.a) * (float)local;
            return out;
        }
    }
    return stops.back().second;
}

// Splits "a b c d" into 1-4 box values (CSS shorthand order).
std::vector<double> boxValues(const std::string& v) {
    std::vector<double> out;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) { double x; if (parseLenPx(cur, x)) out.push_back(x); }
        cur.clear();
    };
    for (char c : v) {
        if (std::isspace((unsigned char)c)) flush();
        else cur.push_back(c);
    }
    flush();
    if (out.size() == 1) return {out[0], out[0], out[0], out[0]};
    if (out.size() == 2) return {out[0], out[1], out[0], out[1]};       // tb lr
    if (out.size() == 3) return {out[0], out[1], out[2], out[1]};       // t lr b
    return out;                                                          // t r b l
}

enum class Display { Block, Flex, Inline, Table, TableRowGroup, TableRow, TableCell, Grid, None };
enum class FlexDir { Row, Column };
enum class FlexWrap { NoWrap, Wrap };
enum class TextDecoration { None, Underline, LineThrough };
enum class Overflow { Visible, Hidden, Scroll };
// Vertical alignment of an inline run relative to the line's baseline. Stored as
// a resolved CSS-px shift (negative = raise) so painting is a plain y offset.
enum class VAlign { Baseline, Shift };
enum class Position { Static, Relative, Absolute };
// CSS float. A floated box is taken out of normal flow and placed at the left or
// right edge of the current line; subsequent inline content wraps around it.
enum class Float { None, Left, Right };
enum class Justify { Start, Center, End, Between };
enum class Align { Stretch, Start, Center, End };
enum class TextAlign { Left, Center, Right };
// CSS transition timing function. The four named keywords map to cubic-bezier
// control points; linear is the identity. This is the profile's supported set.
enum class Ease { Linear, Ease, EaseIn, EaseOut, EaseInOut };

struct Style {
    Display display = Display::Block;
    FlexDir flex_dir = FlexDir::Row;
    FlexWrap flex_wrap = FlexWrap::NoWrap;
    Justify justify = Justify::Start;
    Align align = Align::Stretch;
    TextAlign text_align = TextAlign::Left;
    double gap = 0;
    double pad[4] = {0, 0, 0, 0};      // t r b l
    double margin[4] = {0, 0, 0, 0};   // t r b l
    double width = -1, height = -1, min_height = -1;
    // When the matching length was a percentage, the percent is kept here and
    // resolved against the containing block at layout time. -1 = not a percent.
    double width_pct = -1, height_pct = -1, min_height_pct = -1;
    Rgba bg{0, 0, 0, 0};
    Rgba color{0, 0, 0, 1};
    double radius = 0;
    double border = 0;
    Rgba border_color{0, 0, 0, 1};
    // Linear/radial gradient background. Replaces the flat bg when grad.has.
    Gradient grad;
    // box-shadow: offset (x, y), blur, spread, color. Drawn behind the box.
    bool has_shadow = false;
    double shadow_x = 0, shadow_y = 0, shadow_blur = 0, shadow_spread = 0;
    Rgba shadow_color{0, 0, 0, 0};
    double font_size = kDefaultFontSize;
    int font_weight = 400;
    bool bold = false;
    bool disabled = false;
    TextDecoration text_decoration = TextDecoration::None;
    Overflow overflow = Overflow::Visible;
    VAlign valign = VAlign::Baseline;
    // Resolved vertical-align shift in CSS px (negative = raise). Non-zero only
    // when valign == VAlign::Shift.
    double valign_shift = 0;
    Position position = Position::Static;
    Float float_ = Float::None;   // 'float' is a keyword; trailing underscore
    // top/right/bottom/left offsets. -1 = unset (auto). Resolved against the
    // nearest positioned ancestor's padding box at layout time.
    double top = -1, right = -1, bottom = -1, left = -1;
    // Group opacity (CSS 1-100 / 0.0-1.0). Applied by scaling the alpha of every
    // color this node paints (bg, border, text) and accumulating down the tree, so
    // a faded container fades its descendants. 1.0 = fully opaque (no change).
    float opacity = 1.f;
    // Resolved CSS custom properties (--name -> value) visible on this node:
    // declared here or inherited from the nearest ancestor that declares them.
    // Populated during computeStyle, before var() references are substituted.
    std::map<std::string, std::string> custom_props;
    // Grid (display: grid). Tracks hold the template as parsed; lengths in px,
    // percentages kept as pct (>=0), and flex 'fr' units kept as fr (>=0). An
    // empty columns list makes every child land in a single implicit column.
    struct Track { double px = -1, pct = -1, fr = -1; };
    std::vector<Track> grid_cols, grid_rows;
    double row_gap = -1, column_gap = -1;   // -1 = unset (fall back to gap)
    // CSS transitions (profile subset). Each entry is one comma-separated item of
    // the `transition` shorthand (or the longhands), holding the watched property,
    // the duration/delay in ms, and the timing function. duration_ms == 0 means the
    // property is not animated (the CSS default), so a static document never runs
    // the transition machinery at all.
    struct TransitionSpec {
        std::string prop;            // animatable property name, or "all"
        double duration_ms = 0;      // 0 = no animation
        double delay_ms = 0;
        Ease ease = Ease::Ease;      // CSS default timing function
    };
    std::vector<TransitionSpec> transitions;
    // CSS @keyframes animation (profile subset). `animation-name` references a
    // named block in Doc::keyframes; duration/delay in ms, iteration count, and
    // timing function mirror the transitions fields. An empty name means the
    // node is not animated, so a static document never runs the driver.
    std::string anim_name;
    double anim_duration_ms = 0;
    double anim_delay_ms = 0;
    double anim_iter = 1;            // iteration count; < 0 = infinite
    Ease anim_ease = Ease::Ease;
    // SVG paint (inline-svg feature). fill/stroke are inherited; alpha 0 means
    // "none" (SVG's default stroke; shapes get a black fill via UA defaults).
    // The two opacities stay separate from the colors so a child inherits the
    // parent's color and applies its own opacity on top (SVG 11).
    Rgba fill{0, 0, 0, 0};
    Rgba stroke{0, 0, 0, 0};
    float fill_opacity = 1.f;
    float stroke_opacity = 1.f;
    double stroke_width = 1.0;
};

// One CSS declaration set (inline style, a rule's block, or the UA defaults).
struct Decls { std::vector<std::pair<std::string, std::string>> kv; };
Decls parseDecls(const std::string& body);

// Parse a grid track list such as "100px 1fr auto 25%" into Track entries.
// Recognizes px lengths, percentages (kept as pct), fr flex units (kept as fr),
// and auto (treated as an fr:0 track sized by content -> px -1, pct -1, fr 0).
// Unrecognized tokens are skipped. repeat() / minmax() are not modeled.
std::vector<Style::Track> parseTracks(const std::string& value) {
    std::vector<Style::Track> out;
    std::string tok;
    auto commit = [&]() {
        if (tok.empty()) return;
        Style::Track t;
        double num = 0; char unit = 0;
        if (std::sscanf(tok.c_str(), "%lf%c", &num, &unit) >= 1) {
            if (unit == '%') t.pct = num;
            else if (unit == 'f') t.fr = num;              // "1fr"
            else if (unit == 0) t.px = num;                // bare number -> px
            else if (unit == 'p') t.px = num;              // "100px" (unit captured 'p')
            else t.px = num;
        }
        // auto / none -> all fields stay -1/fr0, sized by content.
        out.push_back(t);
        tok.clear();
    };
    for (char c : value) {
        if (std::isspace((unsigned char)c)) commit();
        else tok.push_back(c);
    }
    commit();
    return out;
}

// ───────────── transitions: easing + animatable values ─────────────

// Evaluate a cubic-bezier timing function at progress x (0..1). The four named
// keyword easings are defined by their CSS control points; linear is identity.
static double cubicBezier(double x1, double y1, double x2, double y2, double x) {
    // Find the curve parameter u where bezierX(u) == x, then return bezierY(u).
    // X(u) is monotonic for the profile's control points, so bisection is safe.
    auto bez = [](double a, double b, double u) {
        double v = 1 - u;
        return 3 * a * v * v * u + 3 * b * v * u * u + u * u * u;
    };
    double lo = 0, hi = 1;
    for (int i = 0; i < 32; i++) {
        double mid = (lo + hi) / 2;
        if (bez(x1, x2, mid) < x) lo = mid; else hi = mid;
    }
    double u = (lo + hi) / 2;
    return bez(y1, y2, u);
}
static double applyEase(Ease e, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
        case Ease::Linear:   return t;
        case Ease::Ease:     return cubicBezier(0.25, 0.1, 0.25, 1.0, t);
        case Ease::EaseIn:   return cubicBezier(0.42, 0.0, 1.0, 1.0, t);
        case Ease::EaseOut:  return cubicBezier(0.0, 0.0, 0.58, 1.0, t);
        case Ease::EaseInOut:return cubicBezier(0.42, 0.0, 0.58, 1.0, t);
    }
    return t;
}

// A transitionable property value: either a scalar (px length, opacity) or a color.
// `set` is false when the property is not one the engine animates.
struct AnimVal {
    bool set = false;
    bool is_color = false;
    double num = 0;
    Rgba col;
};
// Read the current value of an animatable property from a resolved style. Returns
// set=false for properties the engine does not animate (and for unset lengths,
// which keep their -1 sentinel and are skipped by the diff).
static AnimVal readAnim(const Style& s, const std::string& p) {
    AnimVal v;
    auto num = [&](double d) { v.set = true; v.num = d; return v; };
    auto col = [&](const Rgba& c) { v.set = true; v.is_color = true; v.col = c; return v; };
    if (p == "opacity") return num(s.opacity);
    if (p == "color") return col(s.color);
    if (p == "background-color") return col(s.bg);
    if (p == "border-color") return col(s.border_color);
    if (p == "width") return s.width >= 0 ? num(s.width) : AnimVal{};
    if (p == "height") return s.height >= 0 ? num(s.height) : AnimVal{};
    if (p == "min-height") return s.min_height >= 0 ? num(s.min_height) : AnimVal{};
    if (p == "border-radius") return num(s.radius);
    if (p == "border-width") return num(s.border);
    if (p == "font-size") return num(s.font_size);
    if (p == "gap") return num(s.gap);
    if (p == "padding-top") return num(s.pad[0]);
    if (p == "padding-right") return num(s.pad[1]);
    if (p == "padding-bottom") return num(s.pad[2]);
    if (p == "padding-left") return num(s.pad[3]);
    if (p == "margin-top") return num(s.margin[0]);
    if (p == "margin-right") return num(s.margin[1]);
    if (p == "margin-bottom") return num(s.margin[2]);
    if (p == "margin-left") return num(s.margin[3]);
    if (p == "top") return s.top >= 0 ? num(s.top) : AnimVal{};
    if (p == "right") return s.right >= 0 ? num(s.right) : AnimVal{};
    if (p == "bottom") return s.bottom >= 0 ? num(s.bottom) : AnimVal{};
    if (p == "left") return s.left >= 0 ? num(s.left) : AnimVal{};
    return AnimVal{};
}
static void writeAnim(Style& s, const std::string& p, const AnimVal& v) {
    if (!v.set) return;
    if (v.is_color) {
        if (p == "color") s.color = v.col;
        else if (p == "background-color") s.bg = v.col;
        else if (p == "border-color") s.border_color = v.col;
        return;
    }
    if (p == "opacity") s.opacity = (float)v.num;
    else if (p == "width") s.width = v.num;
    else if (p == "height") s.height = v.num;
    else if (p == "min-height") s.min_height = v.num;
    else if (p == "border-radius") s.radius = v.num;
    else if (p == "border-width") s.border = v.num;
    else if (p == "font-size") s.font_size = v.num;
    else if (p == "gap") s.gap = v.num;
    else if (p == "padding-top") s.pad[0] = v.num;
    else if (p == "padding-right") s.pad[1] = v.num;
    else if (p == "padding-bottom") s.pad[2] = v.num;
    else if (p == "padding-left") s.pad[3] = v.num;
    else if (p == "margin-top") s.margin[0] = v.num;
    else if (p == "margin-right") s.margin[1] = v.num;
    else if (p == "margin-bottom") s.margin[2] = v.num;
    else if (p == "margin-left") s.margin[3] = v.num;
    else if (p == "top") s.top = v.num;
    else if (p == "right") s.right = v.num;
    else if (p == "bottom") s.bottom = v.num;
    else if (p == "left") s.left = v.num;
}
static bool animEq(const AnimVal& a, const AnimVal& b) {
    if (a.set != b.set || a.is_color != b.is_color) return false;
    if (!a.set) return true;
    if (a.is_color) {
        auto eq = [](float x, float y) { return std::fabs(x - y) < 1.f / 255.f; };
        return eq(a.col.r, b.col.r) && eq(a.col.g, b.col.g) && eq(a.col.b, b.col.b) && eq(a.col.a, b.col.a);
    }
    return std::fabs(a.num - b.num) < 0.01;
}
static AnimVal lerpAnim(const AnimVal& a, const AnimVal& b, double t) {
    AnimVal o = a;
    if (a.is_color) {
        o.col.r = a.col.r + (b.col.r - a.col.r) * t;
        o.col.g = a.col.g + (b.col.g - a.col.g) * t;
        o.col.b = a.col.b + (b.col.b - a.col.b) * t;
        o.col.a = a.col.a + (b.col.a - a.col.a) * t;
    } else {
        o.num = a.num + (b.num - a.num) * t;
    }
    return o;
}

// Parse a CSS time ("300ms", "0.3s", ".5s") into milliseconds. Returns false if
// the token is not a time.
static bool parseTimeMs(const std::string& v, double& out_ms) {
    double n = 0; char unit = 0;
    if (std::sscanf(v.c_str(), "%lf%c", &n, &unit) < 1) return false;
    out_ms = (unit == 's') ? n * 1000.0 : n;   // 'ms' or bare (treated as ms)
    return true;
}
static Ease parseEase(const std::string& v) {
    if (v.find("linear") != std::string::npos) return Ease::Linear;
    if (v.find("ease-in-out") != std::string::npos) return Ease::EaseInOut;
    if (v.find("ease-in") != std::string::npos) return Ease::EaseIn;
    if (v.find("ease-out") != std::string::npos) return Ease::EaseOut;
    if (v.find("ease") != std::string::npos) return Ease::Ease;
    return Ease::Ease;
}
// Parse one item of the `transition` shorthand: "prop duration timing delay".
// The first time is the duration, the second is the delay; a keyword is the
// timing function. Missing parts keep CSS defaults (duration 0 = no animation).
static Style::TransitionSpec parseTransitionItem(const std::string& item) {
    Style::TransitionSpec spec;
    std::string cur;
    int times = 0;
    auto commit = [&] {
        if (cur.empty()) return;
        double ms;
        if (parseTimeMs(cur, ms)) {
            if (times == 0) spec.duration_ms = ms; else if (times == 1) spec.delay_ms = ms;
            times++;
        } else if (cur == "linear" || cur.find("ease") != std::string::npos ||
                   cur.find("cubic-bezier") != std::string::npos ||
                   cur.find("step") != std::string::npos) {
            spec.ease = parseEase(cur);
        } else if (cur != "none" && cur != "initial" && cur != "inherit") {
            spec.prop = cur;   // the property name (first non-time, non-keyword token)
        }
        cur.clear();
    };
    for (char c : item) {
        if (std::isspace((unsigned char)c)) commit();
        else cur.push_back(c);
    }
    commit();
    return spec;
}
static std::vector<Style::TransitionSpec> parseTransitionList(const std::string& value) {
    std::vector<Style::TransitionSpec> out;
    std::string item;
    auto commit = [&] {
        if (item.empty()) return;
        Style::TransitionSpec s = parseTransitionItem(item);
        // A default-constructed item (no property, no duration) contributes nothing.
        if (!s.prop.empty() || s.duration_ms > 0) out.push_back(s);
        item.clear();
    };
    for (char c : value) {
        if (c == ',') commit();
        else item.push_back(c);
    }
    commit();
    return out;
}

void applyDecl(Style& st, const std::string& name, const std::string& value) {
    auto is = [&](const char* k) { return name == k; };
    if (is("display")) {
        if (value == "flex") st.display = Display::Flex;
        else if (value == "none") st.display = Display::None;
        else if (value.find("inline") != std::string::npos) st.display = Display::Inline;
        else if (value == "grid") st.display = Display::Grid;
        else st.display = Display::Block;
    } else if (is("flex-direction")) {
        st.flex_dir = value.find("column") != std::string::npos ? FlexDir::Column : FlexDir::Row;
    } else if (is("flex-wrap")) {
        // wrap / wrap-reverse wrap; nowrap (the default) does not.
        st.flex_wrap = value.find("wrap") != std::string::npos ? FlexWrap::Wrap : FlexWrap::NoWrap;
    } else if (is("justify-content")) {
        if (value.find("space-between") != std::string::npos) st.justify = Justify::Between;
        else if (value.find("center") != std::string::npos) st.justify = Justify::Center;
        else if (value.find("flex-end") != std::string::npos || value.find("right") != std::string::npos) st.justify = Justify::End;
        else st.justify = Justify::Start;
    } else if (is("align-items")) {
        if (value.find("flex-start") != std::string::npos) st.align = Align::Start;
        else if (value.find("center") != std::string::npos) st.align = Align::Center;
        else if (value.find("flex-end") != std::string::npos) st.align = Align::End;
        else st.align = Align::Stretch;
    } else if (is("text-align")) {
        if (value.find("center") != std::string::npos) st.text_align = TextAlign::Center;
        else if (value.find("right") != std::string::npos || value.find("end") != std::string::npos)
            st.text_align = TextAlign::Right;
        else st.text_align = TextAlign::Left;
    } else if (is("gap")) {
        double x; if (parseLenPx(value, x)) st.gap = x;
        // A single value sets both axes; grid reads row_gap/column_gap which
        // default to -1 (unset) and fall back to gap at layout.
    } else if (is("row-gap")) {
        double x; if (parseLenPx(value, x)) st.row_gap = x;
    } else if (is("column-gap")) {
        double x; if (parseLenPx(value, x)) st.column_gap = x;
    } else if (is("grid-template-columns")) {
        st.grid_cols = parseTracks(value);
    } else if (is("grid-template-rows")) {
        st.grid_rows = parseTracks(value);
    } else if (is("padding")) { auto b = boxValues(value); for (int i = 0; i < 4 && i < (int)b.size(); i++) st.pad[i] = b[i]; }
    else if (is("margin")) { auto b = boxValues(value); for (int i = 0; i < 4 && i < (int)b.size(); i++) st.margin[i] = b[i]; }
    else if (is("width")) { double x; bool p; if (parseLenPx(value, x, p)) { st.width = x; st.width_pct = p ? x : -1; } }
    else if (is("height")) { double x; bool p; if (parseLenPx(value, x, p)) { st.height = x; st.height_pct = p ? x : -1; } }
    else if (is("min-height")) { double x; bool p; if (parseLenPx(value, x, p)) { st.min_height = x; st.min_height_pct = p ? x : -1; } }
    else if (is("background") || is("background-color") || is("background-image")) {
        // A gradient paints over a flat color (CSS: background-image over
        // background-color). The `background` shorthand can carry either, so try
        // the gradient first and fall back to a flat color.
        Gradient g;
        if (parseGradient(value, g)) {
            st.grad = g;
            st.bg = {0, 0, 0, 0};
        }
        else if (!is("background-image")) {
            Rgba c;
            if (parseColor(value, c)) st.bg = c;
        }
    }
    else if (is("background-image")) {
        // Only gradients are modelled; other image types (url()) don't parse and
        // leave the background unchanged.
        Gradient g;
        if (parseGradient(value, g)) st.grad = g;
    }
    else if (is("box-shadow")) {
        double x, y, blur, spread; Rgba c;
        if (parseBoxShadow(value, x, y, blur, spread, c)) {
            st.has_shadow = true;
            st.shadow_x = x; st.shadow_y = y; st.shadow_blur = blur;
            st.shadow_spread = spread; st.shadow_color = c;
        }
    }
    else if (is("color")) { Rgba c; if (parseColor(value, c)) st.color = c; }
    else if (is("border-radius")) { double x; if (parseLenPx(value, x)) st.radius = x; }
    else if (is("border")) {
        // "1px solid #ccc" — take the width and the color.
        double w = 0; Rgba c{0, 0, 0, 1};
        std::string cur;
        auto tryLen = [&](const std::string& t) { double x; if (parseLenPx(t, x)) { w = x; return true; } return false; };
        auto tryColor = [&](const std::string& t) { Rgba cc; if (parseColor(t, cc)) { c = cc; return true; } return false; };
        for (char ch : value) {
            if (std::isspace((unsigned char)ch)) { tryLen(cur) || tryColor(cur); cur.clear(); }
            else cur.push_back(ch);
        }
        tryLen(cur) || tryColor(cur);
        st.border = w; st.border_color = c;
    } else if (is("border-color")) { Rgba c; if (parseColor(value, c)) st.border_color = c; }
    else if (is("font-size")) { double x; if (parseLenPx(value, x)) st.font_size = x; }
    else if (is("font-weight")) {
        if (value.find("bold") != std::string::npos) { st.bold = true; st.font_weight = 700; }
        else { int w = std::atoi(value.c_str()); if (w) { st.font_weight = w; st.bold = w >= 600; } }
    } else if (is("opacity")) {
        // Group opacity 0.0-1.0 (values >1 clamp to 1). Scales the alpha of every
        // color this node paints and accumulates down the tree at computeStyle.
        float o = (float)std::atof(value.c_str());
        st.opacity = std::clamp(o, 0.f, 1.f);
    } else if (is("text-decoration")) {
        if (value.find("underline") != std::string::npos) st.text_decoration = TextDecoration::Underline;
        else if (value.find("line-through") != std::string::npos) st.text_decoration = TextDecoration::LineThrough;
        else st.text_decoration = TextDecoration::None;
    } else if (is("overflow")) {
        // visible doesn't clip. scroll/auto make the box a scroll container
        // (content is clipped to the box and offset by its scroll_top). hidden
        // and clip clip but don't scroll.
        if (value.find("visible") != std::string::npos) st.overflow = Overflow::Visible;
        else if (value.find("scroll") != std::string::npos || value.find("auto") != std::string::npos)
            st.overflow = Overflow::Scroll;
        else st.overflow = Overflow::Hidden;
    } else if (is("vertical-align")) {
        // super/sub/middle resolve against the element's font size; a px length
        // is taken literally. Positive = raise (CSS), stored as a negative y shift.
        if (value.find("super") != std::string::npos) {
            st.valign = VAlign::Shift; st.valign_shift = -0.33 * st.font_size;
        } else if (value.find("sub") != std::string::npos) {
            st.valign = VAlign::Shift; st.valign_shift = 0.33 * st.font_size;
        } else if (value.find("middle") != std::string::npos) {
            st.valign = VAlign::Shift; st.valign_shift = -0.25 * st.font_size;
        } else {
            double x; char end = 0;
            if (std::sscanf(value.c_str(), "%lf%c", &x, &end) >= 1 &&
                (end == 0 || end == 'p')) {   // bare number or px
                st.valign = VAlign::Shift; st.valign_shift = -x;
            } else {
                st.valign = VAlign::Baseline; st.valign_shift = 0;   // baseline / top / bottom
            }
        }
    } else if (is("position")) {
        if (value.find("absolute") != std::string::npos) st.position = Position::Absolute;
        else if (value.find("relative") != std::string::npos) st.position = Position::Relative;
        else st.position = Position::Static;
    } else if (is("float")) {
        if (value.find("left") != std::string::npos) st.float_ = Float::Left;
        else if (value.find("right") != std::string::npos) st.float_ = Float::Right;
        else st.float_ = Float::None;
    } else if (is("transition")) {
        // Shorthand: "width 250ms ease-in-out, opacity .3s". Replaces the set.
        st.transitions = parseTransitionList(value);
    } else if (is("transition-property")) {
        // Longhand: reset then add one spec per named property (duration 0 until
        // a matching transition-duration is merged in computeStyle).
        st.transitions.clear();
        std::string cur;
        auto commit = [&] {
            if (!cur.empty()) { Style::TransitionSpec s; s.prop = cur; st.transitions.push_back(s); }
            cur.clear();
        };
        for (char c : value) { if (c == ',') commit(); else cur.push_back(c); }
        commit();
    } else if (is("transition-duration") || is("transition-delay") || is("transition-timing-function")) {
        // These refine the specs added by transition-property, positional per CSS.
        std::vector<std::string> toks;
        { std::string cur; auto cm=[&]{ if(!cur.empty()) toks.push_back(cur); cur.clear(); };
          for (char c : value) { if (c==',') cm(); else cur.push_back(c); } cm(); }
        bool isdur = is("transition-duration");
        for (size_t i = 0; i < toks.size(); i++) {
            if (st.transitions.size() <= i) st.transitions.resize(i + 1);
            if (isdur) { double ms; if (parseTimeMs(toks[i], ms)) st.transitions[i].duration_ms = ms; }
            else if (is("transition-delay")) { double ms; if (parseTimeMs(toks[i], ms)) st.transitions[i].delay_ms = ms; }
            else st.transitions[i].ease = parseEase(toks[i]);
        }
    } else if (is("animation-name")) {
        // One named @keyframes animation (a comma list keeps the first). "none"
        // clears it. whitespace-stripped + lowercased so it matches the map key.
        std::string n;
        for (char c : value) if (!std::isspace((unsigned char)c)) n.push_back((char)std::tolower((unsigned char)c));
        size_t comma = n.find(',');
        if (comma != std::string::npos) n = n.substr(0, comma);
        st.anim_name = (n == "none") ? "" : n;
    } else if (is("animation-duration") || is("animation-delay")) {
        double ms;
        if (parseTimeMs(value, ms)) {
            if (is("animation-duration")) st.anim_duration_ms = ms;
            else st.anim_delay_ms = ms;
        }
    } else if (is("animation-timing-function")) {
        st.anim_ease = parseEase(value);
    } else if (is("animation-iteration-count")) {
        std::string v;
        for (char c : value) if (!std::isspace((unsigned char)c)) v.push_back((char)std::tolower((unsigned char)c));
        if (v.find("infinite") != std::string::npos) st.anim_iter = -1;
        else st.anim_iter = std::atof(v.c_str());
    } else if (is("animation")) {
        // Shorthand subset: "name duration [timing] [delay] [count]". Order-flexible
        // like CSS; the first time token is duration, the second is delay.
        std::string cur; std::vector<std::string> toks;
        auto cm = [&] { if (!cur.empty()) toks.push_back(cur); cur.clear(); };
        for (char c : value) { if (std::isspace((unsigned char)c)) cm(); else cur.push_back(c); }
        cm();
        int times = 0;
        for (const std::string& t : toks) {
            std::string low;
            for (char c : t) low.push_back((char)std::tolower((unsigned char)c));
            double ms;
            if (low.find("infinite") != std::string::npos) { st.anim_iter = -1; continue; }
            // A bare number is the iteration count, not a time: CSS times carry a
            // unit (100ms / 1s). Check it before parseTimeMs, which would otherwise
            // read "1" as a 1 ms delay and stall the animation behind `t < 0`.
            if (!low.empty() && low.find_first_not_of("0123456789.") == std::string::npos) {
                st.anim_iter = std::atof(low.c_str()); continue;
            }
            if (parseTimeMs(t, ms)) {
                if (times == 0) st.anim_duration_ms = ms;
                else if (times == 1) st.anim_delay_ms = ms;
                times++;
                continue;
            }
            if (parseEase(low) != Ease::Ease || low.find("linear") != std::string::npos) {
                st.anim_ease = parseEase(low); continue;
            }
            // fill-mode / direction / play-state keywords belong to the shorthand
            // but the driver models none of them yet; skip rather than misread as name.
            if (low == "both" || low == "forwards" || low == "backwards" ||
                low == "alternate" || low == "reverse" || low == "normal" ||
                low == "running" || low == "paused") continue;
            if (low != "none") st.anim_name = low;
        }
    } else if (is("top") || is("right") || is("bottom") || is("left")) {
        double x; if (parseLenPx(value, x)) {
            if (is("top")) st.top = x;
            else if (is("right")) st.right = x;
            else if (is("bottom")) st.bottom = x;
            else st.left = x;
        }
    } else if (is("fill")) {
        // SVG fill: a color, or "none" (= transparent). Inherited.
        Rgba c; if (value == "none" || value == "transparent") st.fill = Rgba{0, 0, 0, 0};
        else if (parseColor(value, c)) st.fill = c;
    } else if (is("stroke")) {
        // SVG stroke: a color, or "none" (= transparent, the SVG default). Inherited.
        Rgba c; if (value == "none" || value == "transparent") st.stroke = Rgba{0, 0, 0, 0};
        else if (parseColor(value, c)) st.stroke = c;
    } else if (is("stroke-width")) {
        double x; if (parseLenPx(value, x)) st.stroke_width = std::max(0.0, x);
    } else if (is("fill-opacity")) {
        st.fill_opacity = std::clamp((float)std::atof(value.c_str()), 0.f, 1.f);
    } else if (is("stroke-opacity")) {
        st.stroke_opacity = std::clamp((float)std::atof(value.c_str()), 0.f, 1.f);
    }
}

// A stylesheet rule whose selector is a tag, a ".class" or a "#id" (the profile subset).
struct Rule {
    std::string tag, cls, id;
    Decls decls;
    bool disabled = false;
    bool is_root = false;
    // Structural pseudo-class attached to the simple selector. Only one is tracked
    // (the profile's flat markup uses at most one per selector).
    enum Pseudo { None, FirstChild, LastChild, NthChild } pseudo = None;
    int nth_a = 0, nth_b = 0;   // :nth-child(an+b); a=0 -> the single index b
    // Enclosing @media constraint, evaluated against the document's current CSS
    // width at match time. -1 = unconstrained. Supports min-width / max-width.
    double media_min = -1, media_max = -1;
};

// Case-insensitive compare of a trimmed pseudo name.
static bool psEq(const std::string& raw, const char* name) {
    std::string s;
    for (char c : raw) if (!std::isspace((unsigned char)c)) s.push_back((char)std::tolower((unsigned char)c));
    return s == name;
}

// Parse a structural pseudo-class into rule fields. Returns the pseudo kind and
// fills a/b for :nth-child(an+b). Recognized: first-child, last-child, nth-child.
// Unrecognized pseudos (hover, focus, …) return None -> the rule never matches, so
// an unsupported pseudo degrades to "no style" rather than mis-styling every node.
static Rule::Pseudo parsePseudo(const std::string& raw, int& a, int& b) {
    std::string s;
    for (char c : raw) if (!std::isspace((unsigned char)c)) s.push_back((char)std::tolower((unsigned char)c));
    if (s == "first-child") return Rule::FirstChild;
    if (s == "last-child") return Rule::LastChild;
    if (s.compare(0, 10, "nth-child(") == 0 && s.back() == ')') {
        std::string arg = s.substr(10, s.size() - 11);
        // Accept "odd" (2n+1), "even" (2n), and the general an+b / b / an forms.
        int A = 0, B = 0;
        if (arg == "odd") { A = 2; B = 1; }
        else if (arg == "even") { A = 2; B = 0; }
        else {
            // Strip spaces, then find the optional 'n' term.
            std::string t;
            for (char c : arg) if (c != ' ') t.push_back(c);
            size_t npos = t.find('n');
            if (npos == std::string::npos) {
                B = std::atoi(t.c_str());
            } else {
                std::string coef = t.substr(0, npos);
                A = (coef == "" || coef == "+") ? 1 : (coef == "-" ? -1 : std::atoi(coef.c_str()));
                std::string rest = t.substr(npos + 1);
                if (!rest.empty()) B = std::atoi(rest.c_str());   // handles "+1" / "-1"
            }
        }
        a = A; b = B;
        return Rule::NthChild;
    }
    return Rule::None;
}

// Parse the width constraints out of an @media condition such as
// "(min-width: 400px)" or "(min-width: 320px) and (max-width: 600px)". Only
// min-width / max-width are modeled; other features (orientation, …) are ignored.
// Unrecognized constraints make the whole query not match (returns false) so a
// widget doesn't apply a rule we can't evaluate.
bool parseMediaWidth(const std::string& cond, double& mn, double& mx) {
    mn = -1; mx = -1;
    std::string s;
    for (char c : cond) s.push_back((char)std::tolower((unsigned char)c));
    bool sawAny = false, allKnown = true;
    size_t i = 0;
    while (i < s.size()) {
        size_t open = s.find('(', i);
        if (open == std::string::npos) break;
        size_t close = s.find(')', open);
        if (close == std::string::npos) return false;
        std::string feat = s.substr(open + 1, close - open - 1);
        i = close + 1;
        size_t colon = feat.find(':');
        if (colon == std::string::npos) continue;
        std::string name = feat.substr(0, colon);
        std::string val = feat.substr(colon + 1);
        auto trim = [&](std::string& t) {
            size_t a = t.find_first_not_of(" \t");
            size_t b = t.find_last_not_of(" \t");
            t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
        };
        trim(name); trim(val);
        double px; char end = 0;
        if (std::sscanf(val.c_str(), "%lf%c", &px, &end) < 1) { allKnown = false; continue; }
        sawAny = true;
        if (name == "min-width") mn = px;
        else if (name == "max-width") mx = px;
        else allKnown = false;
    }
    return sawAny && allKnown;
}

// One named @keyframes animation: an ordered list of stops, each an offset in
// 0..1 (from=0, to=1, "50%"=0.5) plus the declarations to apply at that point.
// The driver interpolates each animatable property between the two stops that
// bracket the current clock position, reusing the transition engine's AnimVal
// lerp so keyframes and transitions share one value model.
struct KeyframeStop {
    double offset = 0;                 // 0..1
    Decls decls;
};
struct Keyframes {
    std::string name;
    std::vector<KeyframeStop> stops;   // sorted by offset
};

void parseSheet(const std::string& css, std::vector<Rule>& out,
                std::map<std::string, Keyframes>& kf_out,
                double media_min = -1, double media_max = -1) {
    size_t i = 0;
    auto skipBlock = [&](size_t p) {
        int depth = 0;
        for (; p < css.size(); p++) {
            if (css[p] == '{') depth++;
            else if (css[p] == '}') { depth--; if (depth == 0) return p + 1; }
        }
        return p;
    };
    while (i < css.size()) {
        size_t brace = css.find('{', i);
        if (brace == std::string::npos) break;
        std::string sel = css.substr(i, brace - i);
        size_t end = skipBlock(brace);
        std::string body = css.substr(brace + 1, end - brace - 2);
        // sel runs from the end of the previous block, so an at-rule selector
        // carries a leading `}` plus newline/indent. Trim it so the `@` test and
        // the @media/@keyframes prefix compares below see the bare at-rule.
        {
            size_t sa = sel.find_first_not_of("} \t\r\n");
            if (sa == std::string::npos) { i = end; continue; }
            sel.erase(0, sa);
        }
        // @media: parse the width constraint and recurse into the block so the
        // inner rules carry it.
        if (!sel.empty() && sel[0] == '@') {
            std::string head = sel;
            if (head.size() >= 6 && head.substr(0, 6) == "@media") {
                std::string cond = head.substr(6);
                double mn, mx;
                if (parseMediaWidth(cond, mn, mx))
                    parseSheet(body, out, kf_out, mn, mx);
                // An unevaluable @media (orientation, …) is dropped entirely.
            }
            // @keyframes name { from/50%/to { decls } }: collect the stops.
            // The prefix "@keyframes" is 10 chars; head[10] is the separating space.
            else if (head.size() > 10 && head.substr(0, 10) == "@keyframes") {
                std::string name = head.substr(10);
                // trim whitespace around the name
                auto trim = [&](std::string& t) {
                    size_t a = t.find_first_not_of(" \t\r\n");
                    size_t b = t.find_last_not_of(" \t\r\n");
                    t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
                };
                trim(name);
                if (!name.empty()) {
                    Keyframes kf;
                    kf.name = name;
                    // Walk inner blocks "<offset> { decls }" with a local skipper
                    // over `body`. Offsets: from=0, to=1, or a percentage.
                    size_t p = 0;
                    while (p < body.size()) {
                        size_t ob = body.find('{', p);
                        if (ob == std::string::npos) break;
                        int depth = 0;
                        size_t cb = ob;
                        for (; cb < body.size(); cb++) {
                            if (body[cb] == '{') depth++;
                            else if (body[cb] == '}') { depth--; if (depth == 0) { cb++; break; } }
                        }
                        std::string stopSel = body.substr(p, ob - p);
                        std::string stopBody = body.substr(ob + 1, cb - ob - 2);
                        trim(stopSel);
                        double off;
                        if (psEq(stopSel, "from")) off = 0.0;
                        else if (psEq(stopSel, "to")) off = 1.0;
                        else {
                            std::string num;
                            for (char c : stopSel)
                                if (isdigit((unsigned char)c) || c == '.' || c == '-')
                                    num.push_back(c);
                            off = std::atof(num.c_str()) / 100.0;
                        }
                        if (off >= 0.0 && off <= 1.0) {
                            KeyframeStop st;
                            st.offset = off;
                            st.decls = parseDecls(stopBody);
                            kf.stops.push_back(std::move(st));
                        }
                        p = cb;
                    }
                    std::stable_sort(kf.stops.begin(), kf.stops.end(),
                                     [](const KeyframeStop& a, const KeyframeStop& b) {
                                         return a.offset < b.offset;
                                     });
                    kf_out[name] = std::move(kf);
                }
            }
            i = end;
            continue;
        }
        for (char c : sel) if (c == '>' || c == '+' || c == '~') { i = end; goto next; }
        {
            // Trim, strip a trailing pseudo (:hover / :disabled handled separately).
            std::string s;
            for (char c : sel) if (!std::isspace((unsigned char)c)) s.push_back(c);
            // :root (optionally with a trailing pseudo) matches the document root.
            bool isRoot = s.compare(0, 5, ":root") == 0;
            size_t colon = s.find(':');
            Rule r;
            bool hasDisabled = false;
            if (colon != std::string::npos) {
                std::string ps = s.substr(colon + 1);   // e.g. "first-child", "nth-child(2n+1)"
                s = s.substr(0, colon);
                hasDisabled = psEq(ps, "disabled");
                r.pseudo = parsePseudo(ps, r.nth_a, r.nth_b);
            }
            if (isRoot) r.is_root = true;
            else if (!s.empty() && s[0] == '.') r.cls = s.substr(1);
            else if (!s.empty() && s[0] == '#') r.id = s.substr(1);
            else r.tag = s;
            r.decls = parseDecls(body);
            r.disabled = hasDisabled;
            r.media_min = media_min;
            r.media_max = media_max;
            out.push_back(std::move(r));
        }
    next:
        i = end;
    }
}

// ───────────── document ─────────────

// One <canvas> 2D-context command, recorded by the `canvas` op and replayed at
// paint. Args are the numeric operands in order; col carries a parsed color for
// the state-setting commands; text carries fillText's string.
struct CanvasCmd {
    enum Kind {
        FillStyle, StrokeStyle, LineWidth, GlobalAlpha,
        FillRect, ClearRect, StrokeRect,
        BeginPath, MoveTo, LineTo, Rect, Arc, ClosePath, Fill, Stroke,
        FillText, DrawImage
    };
    Kind kind;
    double a[6] = {0, 0, 0, 0, 0, 0};
    cuiw_svg::Rgba col{0, 0, 0, 1};
    std::string text;
};

struct Node {
    int64_t id = 0;
    enum Kind { Element, Text, Comment } kind = Element;
    std::string tag;                          // lowercase, '' for non-elements
    bool svg = false;
    bool ever_rendered = false;
    int64_t parent = 0;
    std::vector<int64_t> children;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string data;                         // text / comment content
    Style style;                              // computed at layout
    // Layout result, CSS px relative to the document origin.
    double x = 0, y = 0, w = 0, h = 0;
    struct Line { std::string text; double width; };
    std::vector<Line> lines;
    double line_height_px = 0;
    // Inline layout: when this node participates in an inline flow run its text is
    // stored as positioned segments (absolute CSS px) instead of stacked line
    // boxes; paintNode draws segments when non-empty.
    struct Seg { std::string text; double x, y, w; };
    std::vector<Seg> segs;
    // <canvas> 2D-context command list (the `canvas` op). Recorded verbatim and
    // replayed at paint into the element box; see paintCanvas. Empty = no drawing.
    std::vector<CanvasCmd> canvas_cmds;
    // <img>: decoded RGBA8 pixels (row-major, stride = img_w*4), decoded once
    // from the data: URL in the src attribute and cached here. img_w/h are 0
    // until the decode succeeds (or fails, which is not retried).
    std::vector<uint8_t> img_px;
    int img_w = 0, img_h = 0;
    bool img_tried = false;           // decode attempted (success or failure)
    cui_texture img_tex = 0;          // uploaded texture, created lazily at paint
    bool img_tex_valid = false;
    // Scroll container state (overflow: scroll/auto). The host sets scroll_top
    // via the `prop` op; paintNode translates this box's content (text +
    // children) up by it while the box frame and clip stay put. Clamped to
    // [0, content - box] at paint so it can't scroll past the end.
    double scroll_top = 0;
    // Background gradient, uploaded to a texture once and reused. Generated at
    // a reduced resolution (gradients are smooth) and scaled to the box.
    cui_texture grad_tex = 0;
    bool grad_tex_valid = false;
};

struct Doc {
    std::map<int64_t, Node> nodes;
    std::vector<Rule> sheet;
    // Named @keyframes animations, keyed by animation-name. Populated by
    // parseSheet; the animation driver looks stages up here.
    std::map<std::string, Keyframes> keyframes;
    std::string theme = "light";
    double width = 320;
    // Nodes under <head>, never rendered.
    std::vector<int64_t> head_children;
};

Decls parseDecls(const std::string& body) {
    Decls d;
    std::string name, value;
    bool inValue = false;
    auto commit = [&] {
        if (!name.empty()) d.kv.emplace_back(name, value);
        name.clear(); value.clear(); inValue = false;
    };
    for (char c : body) {
        if (c == ':') inValue = true;
        else if (c == ';') commit();
        else if (inValue) { if (!std::isspace((unsigned char)c) || !value.empty()) value.push_back(c); }
        else if (!std::isspace((unsigned char)c)) name.push_back((char)std::tolower((unsigned char)c));
    }
    commit();
    return d;
}

// Substitute var() references in a CSS property value against a node's resolved
// custom properties. Supports var(--name) and var(--name, <fallback>). Unresolved
// references with no fallback make the whole declaration invalid (returns false),
// matching CSS: the property is then treated as unset rather than mis-rendered.
bool resolveVarRefs(const std::map<std::string, std::string>& props,
                    const std::string& value, std::string& out) {
    out.clear();
    size_t i = 0;
    while (i < value.size()) {
        if (value.compare(i, 4, "var(") == 0) {
            size_t start = i + 4;
            int depth = 1; size_t p = start;
            for (; p < value.size() && depth > 0; p++) {
                if (value[p] == '(') depth++;
                else if (value[p] == ')') depth--;
            }
            if (depth != 0) return false;              // unbalanced var(
            std::string inner = value.substr(start, p - 1 - start);
            // Split the first top-level comma: --name, fallback.
            std::string name, fallback;
            int d2 = 0; bool comma = false;
            for (char ch : inner) {
                if (ch == '(') d2++;
                else if (ch == ')') d2--;
                else if (ch == ',' && d2 == 0) { comma = true; continue; }
                if (comma) fallback.push_back(ch);
                else name.push_back(ch);
            }
            auto trim = [](std::string& s) {
                size_t a = s.find_first_not_of(" \t");
                size_t b = s.find_last_not_of(" \t");
                s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
            };
            trim(name); trim(fallback);
            auto it = props.find(name);
            if (it != props.end()) out += it->second;
            else if (comma) { std::string fb; if (!resolveVarRefs(props, fallback, fb)) return false; out += fb; }
            else return false;                          // unresolved, no fallback
            i = p;
        } else {
            out.push_back(value[i++]);
        }
    }
    return true;
}

Node* node(Doc& d, int64_t id) {
    auto it = d.nodes.find(id);
    return it == d.nodes.end() ? nullptr : &it->second;
}

int64_t ensureNode(Doc& d, int64_t id) { return d.nodes[id].id = id; }

void attach(Doc& d, int64_t parent, int64_t child, int64_t before) {
    Node* p = node(d, parent);
    Node* c = node(d, child);
    if (!p || !c || parent == child) return;
    // Detach from any current parent first.
    if (c->parent) if (Node* op = node(d, c->parent)) {
        auto& v = op->children;
        v.erase(std::remove(v.begin(), v.end(), child), v.end());
    }
    c->parent = parent;
    if (before) {
        auto& v = p->children;
        auto it = std::find(v.begin(), v.end(), before);
        v.insert(it == v.end() ? v.end() : it, child);
    } else p->children.push_back(child);
}

void markRendered(Doc& d, int64_t id) {
    if (Node* n = node(d, id)) {
        n->ever_rendered = true;
        for (int64_t c : n->children) markRendered(d, c);
    }
}

// ───────────── theme + UA defaults ─────────────

void applyUaDefaults(Doc& d, Node& n) {
    Style& s = n.style;
    s.pad[0] = s.pad[1] = s.pad[2] = s.pad[3] = -1;   // -1 = unset; computeStyle resolves it
    s.color = d.theme == "dark" ? Rgba{0.886f, 0.910f, 0.941f, 1}   // #e2e8f0
                                : Rgba{0.133f, 0.133f, 0.133f, 1};   // #222222
    s.font_size = kDefaultFontSize;
    const std::string& t = n.tag;
    if (t == "b" || t == "strong") { s.bold = true; s.font_weight = 600; }
    else if (t == "i" || t == "em") { /* italic not modelled */ }
    else if (t == "small") s.font_size = kDefaultFontSize * 0.85;
    else if (t == "h1") { s.bold = true; s.font_size = 1.6 * kDefaultFontSize; s.margin[2] = 0.35 * kDefaultFontSize; s.margin[0] = 0.75 * kDefaultFontSize; }
    else if (t == "h2") { s.bold = true; s.font_size = 1.35 * kDefaultFontSize; s.margin[0] = 0.75 * kDefaultFontSize; s.margin[2] = 0.35 * kDefaultFontSize; }
    else if (t == "h3") { s.bold = true; s.font_size = 1.2 * kDefaultFontSize; s.margin[0] = 0.75 * kDefaultFontSize; s.margin[2] = 0.35 * kDefaultFontSize; }
    else if (t == "h4") { s.bold = true; s.font_size = 1.07 * kDefaultFontSize; }
    else if (t == "h5") { s.bold = true; s.font_size = kDefaultFontSize; }
    else if (t == "h6") { s.bold = true; s.font_size = 0.87 * kDefaultFontSize; }
    else if (t == "p" || t == "ul" || t == "ol" || t == "pre" || t == "blockquote" || t == "figure") { s.margin[0] = s.margin[2] = 0.6 * kDefaultFontSize; }
    else if (t == "button") {
        s.pad[0] = s.pad[2] = 8; s.pad[1] = s.pad[3] = 14;
        s.radius = 8;
        s.border = 0;
        s.bold = true; s.font_weight = 500;
    }
    else if (t == "br") { s.display = Display::Block; s.height = 0; }
    // <canvas> replaced-element default: 300x150 (HTML 4.12.5) and a block box,
    // so the element has a real box to paint its 2D-context commands into even
    // with no width/height attrs or CSS.
    else if (t == "canvas") {
        s.width = 300; s.height = 150;
        s.display = Display::Block;
    }
    // SVG paint defaults (SVG 1.1 initial values). Shape elements fill black and
    // stroke none; the <svg> root paints no fill/stroke of its own (it is a
    // viewport). CSS/presentation attrs override these in the cascade.
    else if (n.svg) {
        if (t == "svg") {
            s.fill = {0, 0, 0, 0}; s.stroke = {0, 0, 0, 0};
            // SVG replaced-element default size (SVG 7.2): 300x150 when neither
            // width/height attrs nor CSS give a size. This is the box the viewBox
            // maps into; without it the root would collapse to 0 height (its
            // shape children are display:none) and nothing would paint.
            s.width = 300; s.height = 150;
            // A replaced element is a block-level box (CSS 9.2.1 / SVG). Without
            // this the root lays out inline and its height never contributes to
            // the parent, so the viewport collapses and nothing paints.
            s.display = Display::Block;
        }
        else { s.fill = {0, 0, 0, 1}; s.stroke = {0, 0, 0, 0}; }   /* shape: fill #000, stroke none */
        // SVG shapes are positioned by their own geometry (x/cx/d …) in the
        // viewBox, not by block flow. Take them out of layout entirely;
        // paintSvg walks them directly. The <svg> root stays a block box.
        if (t != "svg" && t != "g" && t != "defs") s.display = Display::None;
    }
    // CSS table model roles (CSS 17). The table layout pass walks these; painting
    // is the normal block/inline recursion once each cell has a position.
    else if (t == "table") { s.display = Display::Table; s.margin[0] = s.margin[2] = 0.6 * kDefaultFontSize; }
    else if (t == "tr") { s.display = Display::TableRow; }
    else if (t == "td" || t == "th") {
        s.display = Display::TableCell;
        s.pad[0] = s.pad[2] = 6; s.pad[1] = s.pad[3] = 10;
        if (t == "th") { s.bold = true; s.font_weight = 600; }
    }
    else if (t == "thead" || t == "tbody" || t == "tfoot") { s.display = Display::TableRowGroup; }
    // Phrasing content flows inline by default (CSS 9.2.2). Text nodes are inline
    // by nature; these elements join their inline siblings on a shared line box.
    else if (t == "span" || t == "a" || t == "em" || t == "i" || t == "strong" ||
             t == "b" || t == "small" || t == "code" || t == "u" || t == "s" ||
             t == "mark" || t == "sub" || t == "sup" || t == "abbr" || t == "cite" ||
             t == "q" || t == "time" || t == "var" || t == "kbd" || t == "samp")
        s.display = Display::Inline;
}

// The UA layer's button tokens; overridable by the author.
void applyUaButtonTokens(Style& s, bool dark) {
    // bg rgba(51,144,236,.12); pressed .7; padding 8px 14px; radius 8px.
    s.bg = Rgba{0.2f, 0.565f, 0.925f, 0.12f};
    s.color = dark ? Rgba{0.886f, 0.910f, 0.941f, 1} : Rgba{0.133f, 0.133f, 0.133f, 1};
    (void)s;
}

bool hasClass(const Node& n, const std::string& cls) {
    for (const auto& a : n.attrs) {
        if (a.first != "class") continue;
        std::string cur;
        for (char c : a.second) {
            if (std::isspace((unsigned char)c)) { if (cur == cls) return true; cur.clear(); }
            else cur.push_back(c);
        }
        if (cur == cls) return true;
    }
    return false;
}

std::string getAttr(const Node& n, const std::string& name) {
    for (const auto& a : n.attrs) if (a.first == name) return a.second;
    return {};
}

// Evaluate a structural pseudo-class for node `n` against the Rule's parsed kind.
// Position is among the parent's *element* children (text/comments don't count),
// 1-based, matching CSS. A node with no parent never satisfies a positional pseudo.
bool matchesPseudo(Doc& d, const Node& n, const Rule& r) {
    if (r.pseudo == Rule::None) return true;
    if (!n.parent) return false;
    const Node* p = node(d, n.parent);
    if (!p) return false;
    int index = 0, count = 0;
    for (int64_t cid : p->children) {
        const Node* c = node(d, cid);
        if (!c || c->kind != Node::Element) continue;
        count++;
        if (cid == n.id) index = count;
    }
    if (index == 0) return false;
    switch (r.pseudo) {
        case Rule::FirstChild: return index == 1;
        case Rule::LastChild:  return index == count;
        case Rule::NthChild: {
            // Match the smallest k>=0 with index == a*k + b, i.e. (index-b) is a
            // non-negative multiple of a. a==0 -> only index==b.
            if (r.nth_a == 0) return index == r.nth_b;
            int diff = index - r.nth_b;
            if (diff % r.nth_a != 0) return false;
            int k = diff / r.nth_a;
            return k >= 0;
        }
        default: return true;
    }
}

void computeStyle(Doc& d, int64_t id) {
    Node& n = *node(d, id);
    n.style = Style{};
    applyUaDefaults(d, n);
    // CSS inheritance: text-align inherits down the tree. The parent's style is
    // already final here (computeStyleAll walks parent-before-child), and the
    // author rules below can still override it on this node — the cascade order
    // is inherited value, then this node's own declarations.
    if (n.parent) if (Node* p = node(d, n.parent)) {
        n.style.text_align = p->style.text_align;
        // color inherits by default (CSS 16.1). applyUaDefaults seeded the theme
        // text color; the parent's (possibly author-set) color overrides it here,
        // and this node's own author/inline declarations override that below.
        n.style.color = p->style.color;
        // text-decoration propagates to descendants the way color does: the span
        // carries underline/line-through, but the text is drawn by the child text
        // node, which must inherit the decoration to draw the line.
        n.style.text_decoration = p->style.text_decoration;
        // vertical-align is not a CSS-inherited property, but the inline run
        // applies the shift from the text node's own style. Propagate the nearest
        // styled ancestor's shift down to the text so <span class=sup>text</span>
        // raises its text. A vertical-align set directly on the text's own
        // author/inline declarations overrides this below.
        n.style.valign = p->style.valign;
        n.style.valign_shift = p->style.valign_shift;
        // Group opacity accumulates down the tree (CSS 1-100): a faded container
        // fades its descendants multiplicatively.
        n.style.opacity = p->style.opacity;
        // SVG fill and stroke are inherited properties (SVG 11): a shape inherits
        // its parent's fill/stroke unless it sets its own. Opacities and
        // stroke-width are NOT inherited (each element applies its own).
        n.style.fill = p->style.fill;
        n.style.stroke = p->style.stroke;
    }
    // Custom properties inherit parent->child (CSS 15.3). The parent's set is
    // already final here (computeStyleAll walks parent-before-child).
    if (n.parent) if (Node* p = node(d, n.parent))
        n.style.custom_props = p->style.custom_props;

    // Gather every declaration that applies to this node — matching rules then
    // inline style — in one pass. Custom properties (--name) are collected into
    // custom_props first so a var() in any value can reference a --name declared
    // anywhere on the element (CSS resolves custom props before substituting).
    Decls all;
    // SVG presentation attributes (SVG 1.1 §6.2) are author-level declarations
    // with zero specificity: they override UA defaults but lose to any author CSS
    // rule, which is why they are pushed first (applyDecl is last-write-wins).
    // fill/stroke/opacity/stroke-width carry the paint; width/height size the
    // <svg> root (on shapes they are geometry, read directly by the SVG pass).
    if (n.svg)
        for (const auto& a : n.attrs) {
            const std::string& an = a.first;
            bool pres = an == "fill" || an == "stroke" || an == "stroke-width"
                     || an == "fill-opacity" || an == "stroke-opacity"
                     || an == "opacity"
                     || ((an == "width" || an == "height") && n.tag == "svg");
            if (pres) all.kv.push_back({an, a.second});
        }
    for (const Rule& r : d.sheet) {
        bool match = r.is_root ? (n.tag == "body")
                   : ((!r.tag.empty() && r.tag == n.tag) || (!r.cls.empty() && hasClass(n, r.cls))
                   || (!r.id.empty() && getAttr(n, "id") == r.id));
        if (!match) continue;
        if (!matchesPseudo(d, n, r)) continue;
        // @media gate: the rule only applies when the document's current CSS
        // width satisfies its min/max-width constraint (CSS 9; mobile-first).
        if (r.media_min >= 0 && d.width < r.media_min) continue;
        if (r.media_max >= 0 && d.width > r.media_max) continue;
        if (r.disabled && !n.style.disabled) continue;
        for (const auto& kv : r.decls.kv) all.kv.push_back(kv);
    }
    for (const auto& a : n.attrs)
        if (a.first == "style")
            for (const auto& kv : parseDecls(a.second).kv) all.kv.push_back(kv);

    for (const auto& kv : all.kv)
        if (kv.first.size() > 1 && kv.first[0] == '-' && kv.first[1] == '-')
            n.style.custom_props[kv.first] = kv.second;

    // Apply normal declarations with var() references resolved. An unresolved
    // var() with no fallback makes that declaration invalid (CSS: treated as unset).
    for (const auto& kv : all.kv) {
        if (kv.first.size() > 1 && kv.first[0] == '-' && kv.first[1] == '-') continue;
        std::string resolved;
        if (!resolveVarRefs(n.style.custom_props, kv.second, resolved)) continue;
        applyDecl(n.style, kv.first, resolved);
    }
    if (n.style.bold && n.style.font_weight < 600) n.style.font_weight = 700;
    // A button with no author background gets the UA button token.
    if (n.tag == "button" && n.style.bg.a == 0 && n.style.border == 0) applyUaButtonTokens(n.style, d.theme == "dark");
    if (n.tag == "button" && n.style.disabled) { n.style.bg.a *= 0.4f; n.style.color.a *= 0.4f; }
    // Resolve the "unset" (-1) padding sentinel to 0 for every node but the body; the body's
    // is resolved in relayout() against the root padding, honoring an explicit padding:0.
    if (n.tag != "body")
        for (double& p : n.style.pad) if (p < 0) p = 0;
}

// ───────────── SVG geometry (inline-svg feature) ─────────────
// These translate a shape element's own attributes into cuiw_svg::Path geometry
// in the viewBox's user coordinates. Fill/stroke come from the node's resolved
// Style (the CSS cascade), not from parsing here, so presentation attributes and
// stylesheets behave identically.

// Splits "1,2 3,4,5,6" into pairs of doubles (polyline/polygon points).
static void parsePointList(const std::string& v, std::vector<cuiw_svg::Pt>& out) {
    std::vector<double> nums;
    std::string cur;
    auto commit = [&] {
        if (cur.empty()) return;
        char* end = nullptr;
        double x = std::strtod(cur.c_str(), &end);
        if (end != cur.c_str()) nums.push_back(x);
        cur.clear();
    };
    for (char c : v) {
        if (c == ',' || std::isspace((unsigned char)c)) commit();
        else cur.push_back(c);
    }
    commit();
    for (size_t i = 0; i + 1 < nums.size(); i += 2)
        out.push_back(cuiw_svg::Pt{nums[i], nums[i + 1]});
}

// Appends an ellipse flattened to `seg` points around (cx,cy).
static void appendEllipse(double cx, double cy, double rx, double ry,
                          std::vector<cuiw_svg::Pt>& out) {
    if (rx <= 0 || ry <= 0) return;
    const int seg = 64;
    for (int i = 0; i < seg; i++) {
        double a = 2.0 * 3.14159265358979323846 * i / seg;
        out.push_back(cuiw_svg::Pt{cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
}

// Builds a rectangle outline, honouring rx/ry rounded corners.
static void appendRect(double x, double y, double w, double h, double rx, double ry,
                       std::vector<cuiw_svg::Pt>& out) {
    if (w <= 0 || h <= 0) return;
    if (rx <= 0 && ry <= 0) {
        out.push_back({x, y});
        out.push_back({x + w, y});
        out.push_back({x + w, y + h});
        out.push_back({x, y + h});
        return;
    }
    // SVG 9.2: if only one of rx/ry is given, both equal it; clamp to half the side.
    if (rx <= 0) rx = ry;
    if (ry <= 0) ry = rx;
    rx = std::min(rx, w / 2.0);
    ry = std::min(ry, h / 2.0);
    const int seg = 16;   // per corner
    auto corner = [&](double cx, double cy, double a0) {
        for (int i = 0; i <= seg; i++) {
            double a = a0 + (3.14159265358979323846 / 2.0) * i / seg;
            out.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
        }
    };
    corner(x + w - rx, y + ry, 3.0 * 3.14159265358979323846 / 2.0);   // top-right
    corner(x + w - rx, y + h - ry, 0.0);                              // bottom-right
    corner(x + rx, y + h - ry, 3.14159265358979323846 / 2.0);         // bottom-left
    corner(x + rx, y + ry, 3.14159265358979323846);                   // top-left
}

// Parses a <path d> supporting M/L/H/V/C/Q/Z (absolute + relative). Curves are
// flattened to line segments; the arc (A) command is not modelled and is skipped.
static bool parsePathData(const std::string& d, std::vector<cuiw_svg::Pt>& out,
                          bool& closed) {
    closed = false;
    std::vector<cuiw_svg::Pt> pts;
    double cx = 0, cy = 0;      // current point
    double sx = 0, sy = 0;      // subpath start (for Z)
    size_t i = 0;
    auto number = [&](double& val) -> bool {
        while (i < d.size() && (std::isspace((unsigned char)d[i]) || d[i] == ','))
            if (d[i++] == ',') break;
        // Skip separators but keep at least the scan position valid.
        while (i < d.size() && (std::isspace((unsigned char)d[i]) || d[i] == ',')) i++;
        if (i >= d.size()) return false;
        char* end = nullptr;
        val = std::strtod(d.c_str() + i, &end);
        if (end == d.c_str() + i) return false;
        i = (size_t)(end - d.c_str());
        return true;
    };
    auto lineTo = [&](double x, double y) {
        pts.push_back({x, y});
        cx = x;
        cy = y;
    };
    auto cubicTo = [&](double x1, double y1, double x2, double y2, double x, double y) {
        const int seg = 16;   // flatten each cubic
        for (int k = 1; k <= seg; k++) {
            double t = (double)k / seg, u = 1 - t;
            double bx = u * u * u * cx + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x;
            double by = u * u * u * cy + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y;
            pts.push_back({bx, by});
        }
        cx = x;
        cy = y;
    };
    auto quadTo = [&](double x1, double y1, double x, double y) {
        const int seg = 16;
        for (int k = 1; k <= seg; k++) {
            double t = (double)k / seg, u = 1 - t;
            double bx = u * u * cx + 2 * u * t * x1 + t * t * x;
            double by = u * u * cy + 2 * u * t * y1 + t * t * y;
            pts.push_back({bx, by});
        }
        cx = x;
        cy = y;
    };
    // The most recent command; repeated coordinate groups reuse it (SVG 9.3.4).
    char cmd = 0;
    while (i < d.size()) {
        while (i < d.size() && std::isspace((unsigned char)d[i])) i++;
        if (i >= d.size()) break;
        char c = d[i];
        if (std::isalpha((unsigned char)c)) {
            cmd = c;
            i++;
        } else if (cmd == 'M' || cmd == 'm') {
            // An implicit repeat of M becomes L (SVG 9.3.4).
            cmd = (cmd == 'M') ? 'L' : 'l';
        }
        if (!cmd) return false;
        double rel = (cmd >= 'a' && cmd <= 'z') ? 1.0 : 0.0;
        char up = (char)std::toupper((unsigned char)cmd);
        if (up == 'Z') {
            // Close the subpath back to its start.
            if (!pts.empty()) pts.push_back({sx, sy});
            cx = sx;
            cy = sy;
            closed = true;
            // Z is not followed by coordinates; the next token (if any) is a command.
            continue;
        }
        if (up == 'M' || up == 'L') {
            double x, y;
            if (!number(x) || !number(y)) break;
            x += rel * cx;
            y += rel * cy;
            lineTo(x, y);
            if (up == 'M') { sx = x; sy = y; }
        } else if (up == 'H') {
            double x;
            if (!number(x)) break;
            lineTo(x + rel * cx, cy);
        } else if (up == 'V') {
            double y;
            if (!number(y)) break;
            lineTo(cx, y + rel * cy);
        } else if (up == 'C') {
            double x1, y1, x2, y2, x, y;
            if (!number(x1) || !number(y1) || !number(x2) || !number(y2) || !number(x) || !number(y)) break;
            cubicTo(x1 + rel * cx, y1 + rel * cy, x2 + rel * cx, y2 + rel * cy, x + rel * cx, y + rel * cy);
        } else if (up == 'Q') {
            double x1, y1, x, y;
            if (!number(x1) || !number(y1) || !number(x) || !number(y)) break;
            quadTo(x1 + rel * cx, y1 + rel * cy, x + rel * cx, y + rel * cy);
        } else {
            // Unsupported command (A, S, T) — stop rather than mis-parse.
            break;
        }
    }
    if (pts.size() < 2) return false;
    out.insert(out.end(), pts.begin(), pts.end());
    return true;
}

// Translates one shape element into geometry. Returns false for non-shapes
// (<svg>, <g>, <defs>, unsupported) which paint nothing themselves.
static bool svgShapeGeometry(const Node& n, cuiw_svg::Path& path) {
    const std::string& t = n.tag;
    path.closed = true;
    path.stroke_width = n.style.stroke_width;
    if (t == "rect") {
        double x = std::atof(getAttr(n, "x").c_str());
        double y = std::atof(getAttr(n, "y").c_str());
        double w = std::atof(getAttr(n, "width").c_str());
        double h = std::atof(getAttr(n, "height").c_str());
        double rx = std::atof(getAttr(n, "rx").c_str());
        double ry = std::atof(getAttr(n, "ry").c_str());
        appendRect(x, y, w, h, rx, ry, path.pts);
        return !path.pts.empty();
    }
    if (t == "circle") {
        double cx = std::atof(getAttr(n, "cx").c_str());
        double cy = std::atof(getAttr(n, "cy").c_str());
        double r = std::atof(getAttr(n, "r").c_str());
        appendEllipse(cx, cy, r, r, path.pts);
        return !path.pts.empty();
    }
    if (t == "ellipse") {
        appendEllipse(std::atof(getAttr(n, "cx").c_str()), std::atof(getAttr(n, "cy").c_str()),
                      std::atof(getAttr(n, "rx").c_str()), std::atof(getAttr(n, "ry").c_str()),
                      path.pts);
        return !path.pts.empty();
    }
    if (t == "line") {
        path.closed = false;
        path.pts.push_back({std::atof(getAttr(n, "x1").c_str()), std::atof(getAttr(n, "y1").c_str())});
        path.pts.push_back({std::atof(getAttr(n, "x2").c_str()), std::atof(getAttr(n, "y2").c_str())});
        // A line only strokes; force no fill so it never paints a filled sliver.
        path.fill = {0, 0, 0, 0};
        return path.pts.size() == 2;
    }
    if (t == "polyline" || t == "polygon") {
        path.closed = (t == "polygon");
        parsePointList(getAttr(n, "points"), path.pts);
        return path.pts.size() >= 2;
    }
    if (t == "path") {
        return parsePathData(getAttr(n, "d"), path.pts, path.closed);
    }
    return false;
}

} // namespace

// ───────────── the engine, hidden behind the adapter ─────────────

struct CuiRenderer::Impl {
    Doc doc;
    cui_sw_renderer* sw = nullptr;
    cui_ctx* ui = nullptr;
    double scale = 1;
    double content_height = 0;
    int64_t seq = 1;
    std::vector<std::string> out;             // messages for the host
    // ── transition state (see advanceTransitions / startTransitions) ──
    // One running transition per (node, property). `from`/`to` are the values at
    // the start/end of the change; `start_ms` is the anim clock time the (post-
    // delay) interpolation begins. A finished run is erased, not kept.
    struct TransRun {
        AnimVal from, to;
        double start_ms = 0, duration_ms = 0;
        Ease ease = Ease::Ease;
    };
    // Last value we displayed for a (node, property), animated or not. This is the
    // diff baseline so a change starts a transition from where the eye is now,
    // not from the freshly-recomputed target. Absent = never animated.
    std::map<int64_t, std::map<std::string, AnimVal>> anim_now_;
    std::map<int64_t, std::map<std::string, TransRun>> trans_runs_;
    // ── @keyframes state ──
    // When this node's animation began (anim-clock ms), latched the first time
    // the driver sees it so the clock starts at the node's birth, not doc load.
    // A node not present here has not started yet.
    std::map<int64_t, double> kf_start_;
    // Nodes whose animation has run its finite course (frozen at the last stop).
    std::set<int64_t> kf_done_;
    // Animation clock. Wall-clock by default; a host/test can freeze it and step
    // it deterministically via the {"type":"clock"} message.
    bool clock_virtual_ = false;
    double clock_virtual_now_ = 0;
    double animNow() const {
        if (clock_virtual_)
            return clock_virtual_now_;
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    ~Impl() {
        // Release every cached <img> texture before the CUI context goes away.
        for (auto& [id, n] : doc.nodes) {
            if (n.img_tex_valid && ui) cui_image_destroy(ui, n.img_tex);
            if (n.grad_tex_valid && ui) cui_image_destroy(ui, n.grad_tex);
        }
        // And every canvas drawImage texture cached by data: URL.
        for (auto& [url, tex] : canvas_img_tex_)
            if (tex != 0 && ui) cui_image_destroy(ui, tex);
        if (ui) cui_destroy(ui);
        if (sw) cui_sw_destroy(sw);
    }

    // Returns a texture for a canvas drawImage data: URL, decoding and uploading
    // it on first use and caching by URL so repeated frames don't re-decode.
    cui_texture canvasImageTex(const std::string& url, int& w, int& h) {
        auto it = canvas_img_tex_.find(url);
        if (it != canvas_img_tex_.end()) return it->second;
        std::vector<uint8_t> px;
        cui_texture tex = 0;
        if (cuiw_img::decode_data_url(url, px, w, h) && !px.empty())
            tex = cui_image_create(ui, w, h, px.data());
        canvas_img_tex_[url] = tex;   // cache even a failure (0) to avoid retry
        return tex;
    }
    std::map<std::string, cui_texture> canvas_img_tex_;

    void retheme() {
        cui_set_theme(ui, doc.theme == "dark" ? CUI_THEME_DARK : CUI_THEME_LIGHT);
    }

    cui_font fontFor(const Style& s) const { return s.bold ? CUI_FONT_MEDIUM : CUI_FONT_BODY; }

    double textWidth(const Style& s, const std::string& t) const {
        cui_text_metrics m = cui_measure_text(ui, fontFor(s), (float)s.font_size, t.c_str(), -1);
        return m.width;
    }

    void wrap(Node& n) {
        n.lines.clear();
        const Style& s = n.style;
        double avail = n.w;
        cui_text_metrics m = cui_measure_text(ui, fontFor(s), (float)s.font_size, n.data.c_str(), -1);
        n.line_height_px = m.line_height > 0 ? m.line_height : s.font_size * kDefaultLineHeight;
        std::string word, line;
        double lw = 0;
        auto putLine = [&](const std::string& t, double w) { n.lines.push_back({t, w}); };
        if (n.data.empty()) return;
        for (size_t i = 0; i <= n.data.size(); i++) {
            char c = i < n.data.size() ? n.data[i] : ' ';
            if (c == ' ' || c == '\n' || i == n.data.size()) {
                if (!word.empty()) {
                    double ww = textWidth(s, word);
                    double cand = line.empty() ? ww : lw + textWidth(s, " ") + ww;
                    if (!line.empty() && cand > avail && avail > 0) {
                        putLine(line, lw);
                        line = word; lw = ww;
                    } else if (line.empty()) {
                        line = word; lw = ww;
                    } else {
                        line += " " + word; lw = cand;
                    }
                    word.clear();
                }
            } else word.push_back(c);
        }
        if (!line.empty()) putLine(line, lw);
        if (n.lines.empty()) putLine("", 0);
    }

    double maxChildWidth(const Node& n) const {
        double mx = 0;
        for (int64_t cid : n.children) {
            const Node& c = doc.nodes.at(cid);
            mx = std::max(mx, c.x + c.w);
        }
        return mx;
    }

    // Intrinsic (content-based) width of a node, used when no width is set and
    // the box should size to its content (flex items, inline-ish boxes). A text
    // node measures its full string; an element sums its children's intrinsic
    // widths along the main axis and adds its own horizontal padding + border.
    double intrinsicWidth(int64_t id) const {
        const Node& n = doc.nodes.at(id);
        const Style& s = n.style;
        if (n.kind == Node::Text) return textWidth(s, n.data);
        double sum = 0;
        for (int64_t cid : n.children) {
            const Node& c = doc.nodes.at(cid);
            if (c.kind == Node::Comment || c.style.display == Display::None) continue;
            sum += intrinsicWidth(cid) + c.style.margin[3] + c.style.margin[1];
        }
        return sum + s.pad[1] + s.pad[3] + s.border * 2;
    }

    // Flatten an inline subtree to its ordered text nodes (the nodes that actually
    // paint glyphs) and the inline element wrappers (for bounding boxes).
    void flattenInline(int64_t id, std::vector<int64_t>& texts, std::vector<int64_t>& elems) {
        Node& n = doc.nodes[id];
        if (n.kind == Node::Comment || n.style.display == Display::None) return;
        if (n.kind == Node::Text) { texts.push_back(id); return; }
        elems.push_back(id);
        for (int64_t c : n.children) flattenInline(c, texts, elems);
    }

    double segLineHeight(Node& tn) {
        if (tn.line_height_px > 0) return tn.line_height_px;
        cui_text_metrics m = cui_measure_text(ui, fontFor(tn.style), (float)tn.style.font_size, " ", -1);
        double lh = m.line_height > 0 ? m.line_height : tn.style.font_size * kDefaultLineHeight;
        tn.line_height_px = lh;
        return lh;
    }

    // Lay a run of consecutive inline-level children out as inline content: their
    // text flows as words across line boxes of avail_inner width and wraps as a
    // unit. Returns the run height. Words keep their owning text node's style, so
    // per-segment color/weight follow the closest styled ancestor.
    double layoutInlineRun(const std::vector<int64_t>& run, double cx, double cy,
                           double avail_inner) {
        std::vector<int64_t> texts, elems;
        for (int64_t id : run) flattenInline(id, texts, elems);
        for (int64_t tid : texts) { Node& t = doc.nodes[tid]; t.segs.clear(); segLineHeight(t); }
        if (texts.empty()) return 0;

        struct W { int64_t node; std::string text; double w, lh; bool sp; };
        std::vector<W> words;
        for (int64_t tid : texts) {
            Node& tn = doc.nodes[tid];
            double lh = tn.line_height_px;
            std::string cur; bool sp = false; bool any = false;
            auto flush = [&]() {
                if (!cur.empty())
                    words.push_back({tid, cur, textWidth(tn.style, cur), lh, sp});
                cur.clear();
            };
            for (char ch : tn.data) {
                if (std::isspace((unsigned char)ch)) { if (any) { flush(); sp = true; } }
                else { cur.push_back(ch); any = true; }
            }
            flush();
        }

        double total = 0, cursor = 0, line_h = 0;
        for (const W& wd : words) {
            Node& tn = doc.nodes[wd.node];
            double sw = wd.sp ? textWidth(tn.style, " ") : 0;
            if (cursor > 0 && cursor + sw + wd.w > avail_inner + 0.5) {
                total += line_h; cursor = 0; line_h = 0; sw = 0;
            }
            tn.segs.push_back({wd.text, cx + cursor + sw, cy + total + tn.style.valign_shift, wd.w});
            cursor += sw + wd.w;
            line_h = std::max(line_h, wd.lh);
        }
        total += line_h;

        // Position each participating node's box from its own segments (for hit
        // testing and inline-element backgrounds/borders).
        for (int64_t tid : texts) {
            Node& t = doc.nodes[tid];
            double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
            for (const auto& sg : t.segs) {
                x0 = std::min(x0, sg.x); y0 = std::min(y0, sg.y);
                x1 = std::max(x1, sg.x + sg.w); y1 = std::max(y1, sg.y + t.line_height_px);
            }
            if (x1 >= x0) { t.x = x0; t.y = y0; t.w = x1 - x0; t.h = y1 - y0; }
        }
        for (int64_t eid : elems) {
            std::vector<int64_t> et, ee;
            flattenInline(eid, et, ee);
            double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
            for (int64_t tid : et)
                for (const auto& sg : doc.nodes[tid].segs) {
                    Node& t = doc.nodes[tid];
                    x0 = std::min(x0, sg.x); y0 = std::min(y0, sg.y);
                    x1 = std::max(x1, sg.x + sg.w); y1 = std::max(y1, sg.y + t.line_height_px);
                }
            if (x1 >= x0) {
                Node& e = doc.nodes[eid];
                e.x = x0; e.y = y0; e.w = x1 - x0; e.h = y1 - y0;
            }
        }
        return total;
    }

    // Collect the rows of a table: direct <tr> children, plus the rows of any
    // row-group children (<thead>/<tbody>/<tfoot>). Returns row node ids in order.
    void collectRows(int64_t table, std::vector<int64_t>& rows) {
        for (int64_t cid : doc.nodes[table].children) {
            Node& c = doc.nodes[cid];
            if (c.kind == Node::Comment || c.style.display == Display::None) continue;
            if (c.style.display == Display::TableRowGroup) collectRows(cid, rows);
            else if (c.style.display == Display::TableRow) rows.push_back(cid);
        }
    }

    // Auto table layout (CSS 17.5, simplified): size columns to the max intrinsic
    // (min-content) width of the single-column cells in each, honor colspan by
    // distributing a spanning cell's excess over the columns it spans, then scale
    // the whole grid to fit the available width. Lays each cell out at its final
    // position and stretches every cell to its row's height.
    double layoutTable(int64_t table, double cx, double cy, double avail_inner) {
        std::vector<int64_t> rows;
        collectRows(table, rows);

        // Flatten each row to its cells, reading colspan.
        struct Cell { int64_t id; int col; int span; };
        std::vector<std::vector<Cell>> grid;
        int ncols = 0;
        for (int64_t rid : rows) {
            std::vector<Cell> row;
            int col = 0;
            for (int64_t cid : doc.nodes[rid].children) {
                Node& c = doc.nodes[cid];
                if (c.kind == Node::Comment || c.style.display != Display::TableCell) continue;
                int span = 1;
                for (const auto& a : c.attrs)
                    if (a.first == "colspan") { int v = std::atoi(a.second.c_str()); if (v > 0) span = v; }
                row.push_back({cid, col, span});
                col += span;
            }
            ncols = std::max(ncols, col);
            grid.push_back(row);
        }
        if (ncols == 0 || grid.empty()) {
#ifdef CUIW_LAYOUT_DEBUG
            fprintf(stderr, "[table] EARLY-OUT table=%lld ncols=%d grid=%zu\n",
                    (long long)table, ncols, grid.size());
#endif
            doc.nodes[table].w = 0; return 0;
        }

        // Column widths from single-column cells (min-content via intrinsicWidth).
        std::vector<double> colw(ncols, 0);
        for (const auto& row : grid)
            for (const Cell& cell : row)
                if (cell.span == 1)
                    colw[cell.col] = std::max(colw[cell.col], intrinsicWidth(cell.id));

        // Distribute a spanning cell's excess over the columns it covers.
        for (const auto& row : grid)
            for (const Cell& cell : row) {
                if (cell.span <= 1) continue;
                int last = std::min(cell.col + cell.span, ncols);
                double sum = 0;
                for (int k = cell.col; k < last; k++) sum += colw[k];
                double need = intrinsicWidth(cell.id);
                if (need > sum) {
                    double add = (need - sum) / (last - cell.col);
                    for (int k = cell.col; k < last; k++) colw[k] += add;
                }
            }

        // Fit the grid to the available content width: shrink proportionally if it
        // overflows, grow proportionally if the table has an explicit width.
        double total = 0;
        for (double w : colw) total += w;
        double target = total;
        Node& tn = doc.nodes[table];
        if (tn.style.width_pct >= 0) target = tn.style.width_pct / 100.0 * avail_inner;
        else if (tn.style.width >= 0) target = tn.style.width;
        target = std::min(target, avail_inner);
        if (total > 0 && target > 0 && std::abs(target - total) > 0.5) {
            double f = target / total;
            for (double& w : colw) w *= f;
            total = target;
        }
        if (total < target) total = target;
#ifdef CUIW_LAYOUT_DEBUG
        fprintf(stderr, "[table] table=%lld rows=%zu ncols=%d target=%.1f total=%.1f colw=[",
                (long long)table, grid.size(), ncols, target, total);
        for (double w : colw) fprintf(stderr, "%.1f ", w);
        fprintf(stderr, "]\n");
#endif

        // Place rows and cells; each cell stretches to its row's height.
        double ty = cy;
        for (size_t r = 0; r < grid.size(); r++) {
            double row_h = 0;
            for (const Cell& cell : grid[r]) {
                double cellx = cx;
                for (int k = 0; k < cell.col; k++) cellx += colw[k];
                double cellw = 0;
                int last = std::min(cell.col + cell.span, ncols);
                for (int k = cell.col; k < last; k++) cellw += colw[k];
#ifdef CUIW_LAYOUT_DEBUG
                fprintf(stderr, "[cell] r=%zu col=%d span=%d cellx=%.1f cellw=%.1f ty=%.1f\n",
                        r, cell.col, cell.span, cellx, cellw, ty);
#endif
                double ch = layoutNode(cell.id, cellx, ty, cellw, cellw, -1);
                row_h = std::max(row_h, ch);
            }
            // Stretch this row's cells to the row height (border/background fill).
            for (const Cell& cell : grid[r]) {
                Node& c = doc.nodes[cell.id];
                if (c.h < row_h) c.h = row_h;
            }
            ty += row_h;
        }
        tn.w = total;
        return ty - cy;
    }

    // Grid layout (display: grid). Bounded subset of CSS Grid:
    //   - Explicit column tracks from grid-template-columns (px / % / fr / auto).
    //     With no template, a single implicit column stacks the items.
    //   - Auto-placement: items fill the columns left-to-right, wrapping to a new
    //     row. No explicit grid-column/grid-row placement, no spanning, no dense
    //     packing.
    //   - Row tracks from grid-template-rows set a row's height when given (px / %);
    //     otherwise a row is as tall as its tallest item. fr rows are treated as
    //     auto (a definite container height would be needed to resolve them).
    //   - column-gap / row-gap (falling back to gap) separate the tracks.
    double layoutGrid(int64_t grid, double cx, double cy, double avail_inner) {
        Node& gn = doc.nodes[grid];
        Style& s = gn.style;
        double col_gap = s.column_gap >= 0 ? s.column_gap : s.gap;
        double row_gap = s.row_gap >= 0 ? s.row_gap : s.gap;

        // In-flow items (comments / display:none / abs are skipped by callers).
        std::vector<int64_t> items;
        for (int64_t cid : gn.children) {
            Node& c = doc.nodes[cid];
            if (c.kind == Node::Comment || c.style.display == Display::None) continue;
            if (outOfFlow(c)) { abs_nodes_.push_back(cid); continue; }
            items.push_back(cid);
        }
        if (items.empty()) return 0;

        // Resolve column track widths. fr tracks split the space left after the
        // definite (px / %) tracks and the gaps.
        int ncols = s.grid_cols.empty() ? 1 : (int)s.grid_cols.size();
        std::vector<double> colw(ncols, 0);
        double fixed = 0, fr_total = 0;
        for (int c = 0; c < ncols; c++) {
            const Style::Track& t = s.grid_cols.empty() ? Style::Track{} : s.grid_cols[c];
            if (t.px >= 0) colw[c] = t.px;
            else if (t.pct >= 0) colw[c] = t.pct / 100.0 * avail_inner;
            else if (t.fr >= 0) fr_total += t.fr;
            fixed += colw[c];
        }
        double leftover = avail_inner - fixed - col_gap * (ncols - 1);
        if (fr_total > 0 && leftover > 0)
            for (int c = 0; c < ncols; c++) {
                double fr = s.grid_cols.empty() ? 0 : s.grid_cols[c].fr;
                if (fr > 0) colw[c] = leftover * (fr / fr_total);
            }
        // With no template, the single implicit column fills the container.
        if (s.grid_cols.empty()) colw[0] = avail_inner;

        // Place items row by row. A row's height is its tallest item, or the
        // explicit grid-template-rows value when given.
        int ncols_eff = std::max(1, ncols);
        int nrows = (int)((items.size() + ncols_eff - 1) / ncols_eff);
        double row_y = cy;
        double total_h = 0;
        for (int r = 0; r < nrows; r++) {
            double row_h = 0;
            double explicit_h = -1;
            if (r < (int)s.grid_rows.size()) {
                const Style::Track& rt = s.grid_rows[r];
                if (rt.px >= 0) explicit_h = rt.px;
                else if (rt.pct >= 0) explicit_h = rt.pct / 100.0 * avail_inner;
            }
            for (int c = 0; c < ncols_eff; c++) {
                int idx = r * ncols_eff + c;
                if (idx >= (int)items.size()) break;
                double cellw = (c < (int)colw.size()) ? colw[c] : 0;
                double cellx = cx;
                for (int k = 0; k < c && k < (int)colw.size(); k++) cellx += colw[k] + col_gap;
                Node& it = doc.nodes[items[idx]];
                it.x = cellx; it.y = row_y;
                double ih = layoutNode(items[idx], cellx, row_y, cellw, cellw, -1);
                row_h = std::max(row_h, ih);
            }
            if (explicit_h > row_h) row_h = explicit_h;
            // Stretch this row's items to the row height (background/border fill).
            for (int c = 0; c < ncols_eff; c++) {
                int idx = r * ncols_eff + c;
                if (idx >= (int)items.size()) break;
                Node& it = doc.nodes[items[idx]];
                if (it.h < row_h) it.h = row_h;
            }
            row_y += row_h + row_gap;
            total_h += row_h + (r + 1 < nrows ? row_gap : 0);
        }
        gn.w = avail_inner;
        return total_h;
    }

    double layoutNode(int64_t id, double x, double y, double avail_w,
                      double main_w = -1, double parent_definite_h = -1) {
        Node& n = doc.nodes[id];
        if (n.kind == Node::Comment || n.style.display == Display::None) { n.w = n.h = 0; return 0; }
        Style& s = n.style;
        double ml = s.margin[3], mr = s.margin[1], mt = s.margin[0], mb = s.margin[2];
        double outer_x = x + ml;
        double content_avail = avail_w - ml - mr;

        // This node's definite content height, if its height is definite. Children
        // resolve a percentage height against this (CSS 10.5). -1 = height is auto,
        // so a child's percentage height computes to auto.
        double definite_content_h = -1;
        {
            double eh = s.height;
            if (s.height_pct >= 0)
                eh = parent_definite_h >= 0 ? s.height_pct / 100.0 * parent_definite_h : -1;
            if (eh >= 0) definite_content_h = eh - s.pad[0] - s.pad[2] - s.border * 2;
        }

        // Resolve the element's own width.
        double w;
        // A percentage width resolves against the containing block's content width.
        double explicit_w = s.width;
        if (s.width_pct >= 0) explicit_w = s.width_pct / 100.0 * content_avail;
        if (main_w >= 0) w = std::min(main_w, content_avail);   // flex item: measured main-axis size
        else if (explicit_w >= 0) w = std::min(explicit_w, content_avail);
        else if (s.display == Display::Flex && s.flex_dir == FlexDir::Row) w = content_avail;
        else if (s.display == Display::Grid) w = content_avail;   // a grid fills the container
        else if (n.kind == Node::Text) w = content_avail;
        else if (s.display == Display::Block) w = content_avail;   // a block box fills the container
        else {
            // Block: fit the widest line / child.
            double fit = 0;
            for (int64_t cid : n.children) {
                Node& c = doc.nodes[cid];
                if (c.kind == Node::Comment || c.style.display == Display::None) continue;
                if (outOfFlow(c)) continue;   // an abs child never sizes its parent
                double cw;
                if (c.style.width >= 0) cw = c.style.width;
                else if (c.kind == Node::Text) cw = textWidth(c.style, c.data);
                else {
                    // Intrinsic: lay out a probe at a huge width is costly; use the
                    // widest intrinsic child, else the text of the subtree.
                    cw = 0;
                    for (int64_t gc : c.children) {
                        Node& g = doc.nodes[gc];
                        if (g.kind == Node::Text) cw = std::max(cw, textWidth(g.style, g.data) + c.style.pad[1] + c.style.pad[3]);
                        else cw = std::max(cw, (double)0);
                    }
                    if (cw == 0 && c.children.empty() && c.kind == Node::Element) cw = 0;
                }
                fit = std::max(fit, cw + c.style.margin[3] + c.style.margin[1]);
            }
            if (n.kind == Node::Text) fit = textWidth(s, n.data);
            w = std::min(fit + s.pad[1] + s.pad[3] + s.border * 2, content_avail);
        }
        if (w < 0) w = 0;

        double cx = outer_x + s.pad[3] + s.border;   // content x
        double avail_inner = w - s.pad[1] - s.pad[3] - s.border * 2;
        if (avail_inner < 0) avail_inner = 0;
        n.x = outer_x; n.w = w;

        // Lay out children.
        double inner_h = 0;
        if (s.display == Display::Table) {
            inner_h = layoutTable(id, cx, y + s.pad[0] + s.border, avail_inner);
        }
        else if (s.display == Display::Grid) {
            inner_h = layoutGrid(id, cx, y + s.pad[0] + s.border, avail_inner);
        }
        else if (s.display == Display::Flex) {
            std::vector<int64_t> kids;
            for (int64_t cid : n.children) {
                Node& c = doc.nodes[cid];
                if (c.kind == Node::Comment || c.style.display == Display::None) continue;
                if (outOfFlow(c)) { abs_nodes_.push_back(cid); continue; }
                kids.push_back(cid);
            }
            bool row = s.flex_dir == FlexDir::Row;
            // First pass: intrinsic sizes along the main axis.
            struct Kid { int64_t id; double main, cross; };
            std::vector<Kid> ks;
            double total_main = 0;
            for (int64_t cid : kids) {
                Kid k{cid, 0, 0};
                Node& c = doc.nodes[cid];
                if (row) {
                    k.main = c.style.width >= 0 ? c.style.width : intrinsicWidth(cid);
                    k.cross = 0; // resolved after we know the line cross size
                } else {
                    k.main = layoutNode(cid, 0, 0, avail_inner, -1, definite_content_h);  // column: full measure
                    k.cross = doc.nodes[cid].w;
                }
                ks.push_back(k);
                total_main += k.main;
            }
            if (row && s.flex_wrap == FlexWrap::Wrap) {
                // Greedily pack items into lines that fit avail_inner. Each line is
                // laid out independently: its own cursor (justified within the line)
                // and its own cross size; lines stack down the cross axis.
                double gap = s.gap;
                std::vector<std::vector<size_t>> lines;
                std::vector<size_t> cur;
                double cur_w = 0;
                for (size_t i = 0; i < ks.size(); i++) {
                    double add = ks[i].main + (cur.empty() ? 0 : gap);
                    if (!cur.empty() && cur_w + add > avail_inner + 0.5) {
                        lines.push_back(cur);
                        cur.clear(); cur_w = 0;
                        add = ks[i].main;
                    }
                    cur.push_back(i);
                    cur_w += add;
                }
                if (!cur.empty()) lines.push_back(cur);

                double child_avail_cross = s.height >= 0 ? s.height - s.pad[0] - s.pad[2] : 0;
                child_avail_cross = child_avail_cross > 0 ? child_avail_cross : avail_inner;
                double cross_cursor = y + s.pad[0] + s.border;
                double inner = 0;
                for (auto& ln : lines) {
                    double line_main = 0;
                    for (size_t j = 0; j < ln.size(); j++)
                        line_main += ks[ln[j]].main + (j ? gap : 0);
                    double line_free = avail_inner - line_main;
                    double cursor = 0;
                    if (s.justify == Justify::Center) cursor = line_free / 2;
                    else if (s.justify == Justify::End) cursor = line_free;
                    double between = (s.justify == Justify::Between && ln.size() > 1)
                                         ? line_free / (ln.size() - 1) : 0;
                    double line_cross = 0;
                    for (size_t j = 0; j < ln.size(); j++) {
                        Node& c = doc.nodes[ks[ln[j]].id];
                        double ch = layoutNode(ks[ln[j]].id, cx + cursor, cross_cursor,
                                               child_avail_cross, ks[ln[j]].main, definite_content_h);
                        c.y = cross_cursor;
                        line_cross = std::max(line_cross, ch);
                        cursor += ks[ln[j]].main + gap + between;
                    }
                    // cross-axis alignment within this line
                    for (size_t j : ln) {
                        Node& c = doc.nodes[ks[j].id];
                        double dy = 0;
                        if (s.align == Align::Center) dy = (line_cross - c.h) / 2;
                        else if (s.align == Align::End) dy = line_cross - c.h;
                        if (dy) shiftY(ks[j].id, dy);
                    }
                    cross_cursor += line_cross + gap;
                    inner += line_cross + gap;
                }
                if (inner > 0) inner -= gap;   // no trailing gap between lines
                inner_h = inner;
            } else {
            double gaps = ks.empty() ? 0 : s.gap * (ks.size() - 1);
            double free_main = (row ? avail_inner : 0) - total_main - gaps;
            // distribute free space on the main axis (row only; a column grows downward)
            double cursor = 0;
            if (row) {
                double start = 0;
                if (s.justify == Justify::Center) start = free_main / 2;
                else if (s.justify == Justify::End) start = free_main;
                else if (s.justify == Justify::Between && ks.size() > 1) { /* handled below */ }
                cursor = start;
            }
            double between = (s.justify == Justify::Between && row && ks.size() > 1) ? free_main / (ks.size() - 1) : 0;
            double line_cross = 0;
            for (size_t i = 0; i < ks.size(); i++) {
                Node& c = doc.nodes[ks[i].id];
                if (row) {
                    double ch;
                    double child_avail_cross = s.height >= 0 ? s.height - s.pad[0] - s.pad[2] : 0;
                    // A row flex item is sized along the main axis by the measure
                    // above (ks[i].main), not by filling the container. Pass it as
                    // an explicit width so a block child doesn't stretch to 100%.
                    ch = layoutNode(ks[i].id, cx + cursor, y + s.pad[0] + s.border,
                                    child_avail_cross > 0 ? child_avail_cross : avail_inner,
                                    ks[i].main, definite_content_h);
                    c.y = y + s.pad[0] + s.border;
                    line_cross = std::max(line_cross, ch);
                    cursor += ks[i].main + s.gap + between;
                } else {
                    // column: main already measured; place at the running y.
                    Node& cc = doc.nodes[ks[i].id];
                    double cross = cc.w;
                    double child_x = cx;
                    if (s.align == Align::Center) child_x = cx + (avail_inner - cross) / 2;
                    else if (s.align == Align::End) child_x = cx + avail_inner - cross;
                    else if (s.align == Align::Start) child_x = cx;
                    // re-layout at the final x to fix the subtree's absolute coords
                    layoutNode(ks[i].id, child_x, y + s.pad[0] + s.border + inner_h, avail_inner, -1, definite_content_h);
                    inner_h += ks[i].main + s.gap;
                }
            }
            if (row) {
                inner_h = line_cross;
                // cross-axis alignment for row children
                for (int64_t cid : kids) {
                    Node& c = doc.nodes[cid];
                    double dy = 0;
                    if (s.align == Align::Center) dy = (line_cross - c.h) / 2;
                    else if (s.align == Align::End) dy = line_cross - c.h;
                    if (dy) shiftY(cid, dy);
                }
            }
            } // end non-wrap flex path
        } else {
            // Block flow: block children stack vertically; runs of consecutive
            // inline-level children (text, span/em/strong/a/…) flow together on
            // shared line boxes and wrap as a unit.
            double cy = y + s.pad[0] + s.border;
            size_t ci = 0;
            // Active floats in this block: boxes taken out of flow that inline
            // content and following siblings must wrap around. Bounded subset —
            // no `clear`, no nested-float stacking beyond side accumulation.
            struct ActiveFloat { bool left; double x, w, top, bottom; };
            std::vector<ActiveFloat> floats;
            double float_bottom = cy;   // lowest float bottom, so the container can contain floats
            // Width consumed by left/right floats overlapping vertical band [top,top+h).
            auto floatSpan = [&](double top, double h, double& leftW, double& rightW) {
                leftW = rightW = 0;
                for (const auto& f : floats)
                    if (f.top < top + h && f.bottom > top) {
                        if (f.left) leftW += f.w; else rightW += f.w;
                    }
            };
            while (ci < n.children.size()) {
                int64_t cid = n.children[ci];
                Node& c = doc.nodes[cid];
                if (c.kind == Node::Comment || c.style.display == Display::None) { ci++; continue; }
                if (outOfFlow(c)) { abs_nodes_.push_back(cid); ci++; continue; }
                // Floated box: out of normal flow. Shrink-to-fit width, placed at
                // the current line's left/right content edge (beside any floats
                // already on that side). Later content wraps around its box.
                if (c.style.float_ != Float::None) {
                    double fw = c.style.width >= 0 ? c.style.width
                              : (c.kind == Node::Text ? textWidth(c.style, c.data)
                                                      : intrinsicWidth(cid));
                    fw = std::min(fw, avail_inner);
                    double lh, rh;
                    floatSpan(cy, 0, lh, rh);            // floats flush at this line top
                    double fx = c.style.float_ == Float::Left
                              ? cx + lh
                              : cx + avail_inner - rh - fw;
                    double fh = layoutNode(cid, fx, cy, fw, fw, definite_content_h);
                    floats.push_back({c.style.float_ == Float::Left, fx, fw, cy, cy + fh});
                    float_bottom = std::max(float_bottom, cy + fh);
                    ci++;
                    continue;
                }
                bool c_inline = c.kind == Node::Text || c.style.display == Display::Inline;
                if (!c_inline) {
                    double lh, rh;
                    floatSpan(cy, avail_inner, lh, rh);   // conservative: full band
                    double child_avail = std::max(0.0, avail_inner - lh - rh);
                    double used_w = c.style.width >= 0 ? c.style.width : child_avail;
                    // Record the block child's box top before laying it out —
                    // layoutNode sets x/w but not y, so without this every block
                    // sibling kept y=0 and stacked boxes overlapped.
                    c.y = cy + c.style.margin[0];
                    double bx = cx + lh;                 // indent past left floats
                    double h = layoutNode(cid, bx, cy, child_avail, -1, definite_content_h);
                    if (c.w < used_w && s.align == Align::Center) shiftX(cid, (used_w - c.w) / 2);
                    cy += h + c.style.margin[0] + c.style.margin[2];
                    inner_h += h + c.style.margin[0] + c.style.margin[2];
                    ci++;
                    continue;
                }
                // Gather the maximal run of inline-level siblings.
                std::vector<int64_t> run;
                while (ci < n.children.size()) {
                    int64_t rid = n.children[ci];
                    Node& r = doc.nodes[rid];
                    if (r.kind == Node::Comment || r.style.display == Display::None) { ci++; continue; }
                    if (outOfFlow(r)) { abs_nodes_.push_back(rid); ci++; continue; }
                    bool r_inline = r.kind == Node::Text || r.style.display == Display::Inline;
                    if (!r_inline) break;
                    run.push_back(rid);
                    ci++;
                }
                // Narrow the inline run around any floats overlapping this line so
                // text wraps beside a floated box instead of under it.
                double lh, rh;
                floatSpan(cy, 1.0, lh, rh);              // 1 line; refined per-line below
                double run_x = cx + lh;
                double run_avail = std::max(0.0, avail_inner - lh - rh);
                double rh_run = layoutInlineRun(run, run_x, cy, run_avail);
                cy += rh_run;
                inner_h += rh_run;
            }
            // A float that hangs below the in-flow content must still be contained
            // (CSS 9.5.1): grow the content height to the lowest float bottom.
            inner_h = std::max(inner_h, float_bottom - (y + s.pad[0] + s.border));
        }

        // The element's own height. A percentage height resolves against the
        // containing block's height only when that height is definite (CSS 10.5);
        // otherwise the percentage computes to auto.
        double h;
        double explicit_h = s.height;
        if (s.height_pct >= 0)
            explicit_h = parent_definite_h >= 0 ? s.height_pct / 100.0 * parent_definite_h : -1;
        double explicit_minh = s.min_height;
        if (s.min_height_pct >= 0)
            explicit_minh = parent_definite_h >= 0 ? s.min_height_pct / 100.0 * parent_definite_h : -1;
        if (explicit_h >= 0) h = explicit_h;
        else {
            h = inner_h + s.pad[0] + s.pad[2] + s.border * 2;
            if (explicit_minh > h) h = explicit_minh;
        }
        n.h = h;
        // Wrap our own text (an element may carry a text child we already wrapped;
        // an element with data of its own is not modelled by the shim).
        return h;
    }

    void shiftY(int64_t id, double dy) {
        Node& n = doc.nodes[id];
        n.y += dy;
        // Inline content paints from its segments' own coordinates, not n.y, so a
        // subtree shift must move the segments too or the text stays behind.
        for (auto& sg : n.segs) sg.y += dy;
        for (int64_t c : n.children) shiftY(c, dy);
    }
    void shiftX(int64_t id, double dx) {
        Node& n = doc.nodes[id];
        n.x += dx;
        for (auto& sg : n.segs) sg.x += dx;
        for (int64_t c : n.children) shiftX(c, dx);
    }
    // An absolutely-positioned element is out of flow: it does not advance the
    // parent's cursor and does not contribute to its height.
    bool outOfFlow(const Node& n) const { return n.style.position == Position::Absolute; }

    // Position every collected absolute node against its nearest positioned
    // ancestor (position != static). Called after the normal flow layout, so the
    // containing block's box is final. Offsets resolve against the containing
    // block's padding box (CSS 10.1); a missing axis falls back to the static
    // position the node would have had at the top-left of that box.
    void resolveAbsolute() {
        for (int64_t id : abs_nodes_) {
            Node& n = doc.nodes[id];
            if (n.style.position != Position::Absolute) continue;
            // Nearest positioned ancestor.
            int64_t cb = n.parent;
            while (cb && doc.nodes[cb].style.position == Position::Static) cb = doc.nodes[cb].parent;
            if (!cb) for (auto& [bid, bn] : doc.nodes) if (bn.tag == "body") { cb = bid; break; }
            if (!cb) continue;
            Node& c = doc.nodes[cb];
            // Containing block = the padding box of the positioned ancestor.
            double cbx = c.x + c.style.border;
            double cby = c.y + c.style.border;
            double cbw = std::max(0.0, c.w - c.style.border * 2);
            double cbh = std::max(0.0, c.h - c.style.border * 2);
            // Shrink-to-fit / explicit width, resolved against the containing block.
            double avail = cbw;
            if (n.style.left >= 0 && n.style.right >= 0) avail = std::max(0.0, cbw - n.style.left - n.style.right);
            double saved_ml = n.style.margin[3], saved_mr = n.style.margin[1];
            n.style.margin[3] = n.style.margin[1] = 0;   // offsets, not margins, place an abs box
            layoutNode(id, cbx, cby, avail);
            n.style.margin[3] = saved_ml; n.style.margin[1] = saved_mr;
            // layoutNode sets x/w but leaves y alone, so pin the box to the origin
            // it was measured at before shifting — otherwise a stale y makes the
            // delta 0 on a re-layout while the children sit at the origin.
            n.x = cbx; n.y = cby;
            double nx = cbx, ny = cby;
            if (n.style.left >= 0) nx = cbx + n.style.left;
            else if (n.style.right >= 0) nx = cbx + cbw - n.style.right - n.w;
            if (n.style.top >= 0) ny = cby + n.style.top;
            else if (n.style.bottom >= 0) ny = cby + cbh - n.style.bottom - n.h;
            if (nx != n.x) shiftX(id, nx - n.x);
            if (ny != n.y) shiftY(id, ny - n.y);
        }
    }

    void relayout() {
        // Recompute styles, then lay out from the body at the root padding.
        for (auto& [id, n] : doc.nodes) { (void)id; n.lines.clear(); n.segs.clear(); }
        int64_t body = 0;
        for (auto& [id, n] : doc.nodes) if (n.tag == "body") { body = id; break; }
        if (!body) return;
        computeStyleAll(body);
        // Transitions: the styles above are the targets. Detect changes against the
        // last displayed values and start runs, then advance them to now so this
        // relayout lays out from the interpolated value, not a hard jump to target.
        startTransitions();
        advanceTransitions();
        // @keyframes: write the clock-positioned values over the computed style
        // so this relayout lays out from the animated value.
        advanceKeyframes();
        abs_nodes_.clear();
        Node& b = doc.nodes[body];
        double w = doc.width;
        b.style.margin[3] = b.style.margin[1] = 0;   // the UA body margin is the root padding
        double padX = kRootPaddingX, padY = kRootPaddingY;
        for (const auto& a : b.attrs) if (a.first == "style") { /* inline handled in computeStyle */ }
        // The root padding comes from the UA body margin/padding; respect the author's.
        // pad is -1 when unset, so an explicit padding:0 is honored (not overridden here).
        padX = b.style.pad[3] >= 0 ? b.style.pad[3] : padX;
        padY = b.style.pad[0] >= 0 ? b.style.pad[0] : padY;
        b.style.pad[3] = b.style.pad[1] = padX;
        b.style.pad[0] = b.style.pad[2] = padY;
#ifdef CUIW_LAYOUT_DEBUG
        fprintf(stderr, "[body] pad L=%g R=%g T=%g B=%g doc.width=%g\n",
                b.style.pad[3], b.style.pad[1], b.style.pad[0], b.style.pad[2], doc.width);
#endif
        double h = layoutNode(body, 0, 0, w);
        resolveAbsolute();
        content_height = h;
        // Renderer → host: the document height in CSS px drives the bubble resize
        // (widget_host.cpp reads {type:"size"}). Clamped to the host's [50, 600].
        double reported = std::clamp(h > 0 ? h : 50.0, 50.0, 600.0);
        Json m = Json::object();
        m.set("type", Json("size"));
        m.set("height", Json(reported));
        m.set("innerWidth", Json(doc.width));
        m.set("innerHeight", Json(reported));
        m.set("dpr", Json(scale));
        out.push_back(m.dump());
    }

    void computeStyleAll(int64_t id) {
        computeStyle(doc, id);
        for (int64_t c : doc.nodes[id].children) computeStyleAll(c);
    }

    // ───────────── transitions ─────────────
    // Called after computeStyleAll: the freshly-computed `n.style` now holds the
    // TARGET for every property. For each property this node transitions, diff the
    // target against the last value we displayed (anim_now_). A change starts a
    // new run from the displayed value to the target; no change leaves any active
    // run alone (it keeps heading to the same target). duration_ms == 0 jumps.
    void startTransitions() {
        double now = animNow();
        for (auto& [id, n] : doc.nodes) {
            if (n.style.transitions.empty()) continue;
            auto& nowMap = anim_now_[id];
            auto& runMap = trans_runs_[id];
            for (const auto& spec : n.style.transitions) {
                if (spec.duration_ms <= 0) continue;          // not animated
                if (spec.prop.empty()) continue;
                // Resolve the watched property set: "all" fans out to every
                // property the engine animates on this node; otherwise just the
                // named one. For "all" we still only act on properties whose
                // target is set (readAnim returns set=false for unset lengths).
                std::vector<std::string> props;
                if (spec.prop == "all") {
                    for (const char* q : kAnimatableAll) props.push_back(q);
                } else props.push_back(spec.prop);
                for (const std::string& p : props) {
                    AnimVal target = readAnim(n.style, p);
                    if (!target.set) continue;
                    auto itNow = nowMap.find(p);
                    AnimVal displayed;
                    bool haveDisplayed = false;
                    if (itNow != nowMap.end()) { displayed = itNow->second; haveDisplayed = true; }
                    if (!haveDisplayed) {
                        // First time we see this property: record the target as the
                        // baseline. CSS does not transition on initial style.
                        nowMap[p] = target;
                        continue;
                    }
                    // Already heading to this exact target? Leave the run alone.
                    auto itRun = runMap.find(p);
                    if (itRun != runMap.end() && animEq(itRun->second.to, target)) continue;
                    // No actual change? Snap the baseline, drop any finished run.
                    if (animEq(displayed, target)) {
                        runMap.erase(p);
                        nowMap[p] = target;
                        continue;
                    }
                    // A real change: start a run from the displayed value.
                    TransRun r;
                    r.from = displayed;
                    r.to = target;
                    r.duration_ms = spec.duration_ms;
                    r.start_ms = now + spec.delay_ms;
                    r.ease = spec.ease;
                    runMap[p] = r;
                    nowMap[p] = displayed;   // stays at `from` until interpolation moves it
                }
            }
        }
    }

    // Called each paint (and after startTransitions in relayout): move every active
    // run forward to the anim clock's current time, write the interpolated value
    // into the node's style so layout+paint use it, and retire finished runs.
    // Returns true if any run is still active (the caller then keeps painting).
    bool advanceTransitions() {
        double now = animNow();
        bool any_active = false;
        for (auto& [id, runMap] : trans_runs_) {
            auto itNode = doc.nodes.find(id);
            if (itNode == doc.nodes.end()) continue;   // node removed; run is stale
            Node& n = itNode->second;
            auto& nowMap = anim_now_[id];
            for (auto it = runMap.begin(); it != runMap.end();) {
                const std::string& p = it->first;
                TransRun& r = it->second;
                double elapsed = now - r.start_ms;
                if (elapsed < 0) {                          // still in the delay
                    writeAnim(n.style, p, r.from);
                    nowMap[p] = r.from;
                    any_active = true;
                    ++it;
                    continue;
                }
                if (r.duration_ms <= 0 || elapsed >= r.duration_ms) {   // done
                    writeAnim(n.style, p, r.to);
                    nowMap[p] = r.to;
                    it = runMap.erase(it);
                    continue;
                }
                double t = applyEase(r.ease, elapsed / r.duration_ms);
                AnimVal v = lerpAnim(r.from, r.to, t);
                writeAnim(n.style, p, v);
                nowMap[p] = v;
                any_active = true;
                ++it;
            }
        }
        return any_active;
    }

    // ───────────── @keyframes driver ─────────────
    // Evaluates every node's named animation against the shared anim clock and
    // writes the interpolated property values into the node's style, exactly the
    // way advanceTransitions drives transitions. Reuses readAnim/writeAnim/lerp
    // so keyframes and transitions share one value model. Returns true if any
    // animation is still running (the caller then keeps painting).
    bool advanceKeyframes() {
        double now = animNow();
        bool any_active = false;
        for (auto& [id, n] : doc.nodes) {
            const std::string& name = n.style.anim_name;
            if (name.empty()) continue;
            auto itk = doc.keyframes.find(name);
            if (itk == doc.keyframes.end()) continue;      // unknown name; ignore
            const Keyframes& kf = itk->second;
            if (kf.stops.size() < 2) continue;             // need >=2 stops to lerp
            if (n.style.anim_duration_ms <= 0) continue;   // not animated
            // Latch the start time the first time we see this node animating.
            auto its = kf_start_.find(id);
            if (its == kf_start_.end()) {
                its = kf_start_.emplace(id, now).first;
                kf_done_.erase(id);
            }
            double cycle = n.style.anim_duration_ms;
            double t = now - its->second - n.style.anim_delay_ms;
            if (t < 0) {                                    // still in the delay
                any_active = true;
                continue;
            }
            double iter = n.style.anim_iter;
            double pos;
            if (iter < 0) {                                 // infinite
                pos = std::fmod(t, cycle) / cycle;
                any_active = true;
            } else {
                double total = cycle * iter;
                if (t >= total) {                           // finished: freeze at 100%
                    pos = 1.0;
                    kf_done_.insert(id);
                } else {
                    pos = std::fmod(t, cycle) / cycle;
                    any_active = true;
                }
            }
            // Apply each animatable declaration at the interpolated position: find
            // the two stops bracketing `pos` and lerp the overlapping properties.
            // A property only present at some stops takes the nearest bracketing
            // value (CSS: a property not in a stop is interpolated from the stops
            // that do mention it).
            for (size_t s = 0; s + 1 < kf.stops.size(); s++) {
                const KeyframeStop& a = kf.stops[s];
                const KeyframeStop& b = kf.stops[s + 1];
                if (pos < a.offset || pos > b.offset) continue;
                double span = b.offset - a.offset;
                double local = span > 0 ? (pos - a.offset) / span : 0.0;
                local = applyEase(n.style.anim_ease, local);
                // Values as of each bracketing stop, for the properties this node
                // animates (read from the stops' own declarations).
                for (const auto& kv : a.decls.kv) {
                    const std::string& prop = kv.first;
                    // The property must be one the engine animates.
                    AnimVal from = animValFromDecl(prop, kv.second);
                    if (!from.set) continue;
                    // Find the same property at the upper stop (or carry `from`).
                    AnimVal to = from;
                    for (const auto& kv2 : b.decls.kv)
                        if (kv2.first == prop) { to = animValFromDecl(prop, kv2.second); break; }
                    AnimVal v = lerpAnim(from, to, local);
                    writeAnim(n.style, prop, v);
                }
            }
        }
        return any_active;
    }

    // Parse one declaration value into an AnimVal, for the properties the engine
    // animates. Returns set=false for anything else (mirrors readAnim's set=false
    // for non-animatable properties).
    static AnimVal animValFromDecl(const std::string& prop, const std::string& value) {
        AnimVal v;
        if (prop == "opacity") {
            v.set = true; v.num = std::atof(value.c_str()); return v;
        }
        if (prop == "color" || prop == "background-color" || prop == "border-color") {
            Rgba c;
            if (parseColor(value, c)) { v.set = true; v.is_color = true; v.col = c; }
            return v;
        }
        double px;
        if (parseLenPx(value, px)) { v.set = true; v.num = px; return v; }
        return v;
    }

    // ───────────── paint ─────────────

    void drawTextDecoration(const Style& s, double x, double y_top, double width) {
        if (s.text_decoration == TextDecoration::None || width <= 0) return;
        float thickness = (float)std::max(1.0, s.font_size / 16.0);
        double ly = (s.text_decoration == TextDecoration::Underline)
                        ? y_top + s.font_size * 0.92
                        : y_top + s.font_size * 0.55;
        cui_color col = paintColor(s.color, s.opacity);
        cui_draw_line(ui, cui_vec2{ (float)x, (float)ly }, cui_vec2{ (float)(x + width), (float)ly },
                      thickness, col);
    }

    // Convert a style color to a cui_color, folding in the node's group opacity
    // (accumulated down the tree). The alpha channel carries the product, so the
    // rasterizer composites the whole primitive at the faded strength.
    static cui_color paintColor(const Rgba& c, float opacity) {
        float a = std::clamp(c.a * opacity, 0.f, 1.f);
        return cui_color{ (uint8_t)std::clamp(c.r * 255.f, 0.f, 255.f),
                          (uint8_t)std::clamp(c.g * 255.f, 0.f, 255.f),
                          (uint8_t)std::clamp(c.b * 255.f, 0.f, 255.f),
                          (uint8_t)std::clamp(a * 255.f, 0.f, 255.f) };
    }

    // Paints an <svg> subtree: map its viewBox into the element box and draw each
    // shape child. Shape children carry display:none (set in applyUaDefaults), so
    // the normal block recursion never places them — we read their own geometry
    // here and return without recursing, which is what keeps them from being
    // double-drawn or stacked vertically.
    void paintSvg(int64_t id) {
        Node& n = doc.nodes[id];
        // The <svg> root's own fill/stroke are none by default; it is a viewport.
        // Shapes resolve their paint from their own computed Style (which inherits
        // fill/stroke down the tree), so we read each child's Style, not the root's.
        cuiw_svg::Xform x;
        if (!cuiw_svg::makeXform(getAttr(n, "viewBox").c_str(), n.x, n.y, n.w, n.h, x))
            return;
        cui_rect clip{(float)n.x, (float)n.y, (float)n.w, (float)n.h};
        std::vector<cuiw_svg::Path> paths;
        collectSvgShapes(id, paths);
        cuiw_svg::paintPaths(ui, paths, x, clip);
        // Labels (<text>) paint after the shapes so they sit on top of them.
        paintSvgText(id, x, clip);
    }

    // Depth-walks an SVG subtree drawing <text> elements. x/y are viewBox coords
    // mapped through the same transform as the shapes; y is the baseline (SVG),
    // so we step up by ~the ascent to get the draw origin cui_draw_text expects.
    // Font size / fill / text-anchor come from the element (presentation attrs
    // resolve through the cascade); a missing fill falls back to the text color.
    void paintSvgText(int64_t id, const cuiw_svg::Xform& xf, const cui_rect& clip) {
        for (int64_t cid : doc.nodes[id].children) {
            Node& c = doc.nodes[cid];
            if (c.kind == Node::Element && c.tag == "text") {
                // The string is the concatenated text-node children.
                std::string str;
                for (int64_t gc : c.children) {
                    Node& t = doc.nodes[gc];
                    if (t.kind == Node::Text) str += t.data;
                }
                if (!str.empty()) {
                    double vx = std::atof(getAttr(c, "x").c_str());
                    double vy = std::atof(getAttr(c, "y").c_str());
                    cuiw_svg::Pt sp = cuiw_svg::apply(xf, {vx, vy});
                    double fsize = c.style.font_size;
                    std::string fs = getAttr(c, "font-size");
                    if (!fs.empty()) { double px; if (parseLenPx(fs, px)) fsize = px; }
                    // baseline -> top: the ascent is roughly 0.8 of the font size.
                    float px_x = (float)sp.x, px_y = (float)(sp.y - fsize * 0.8);
                    std::string anchor = getAttr(c, "text-anchor");
                    if (anchor == "middle" || anchor == "end") {
                        float w = (float)textWidth(c.style, str);
                        if (anchor == "middle") px_x -= w / 2.f;
                        else px_x -= w;
                    }
                    Rgba col = c.style.fill.a > 0 ? c.style.fill : c.style.color;
                    cui_draw_text(ui, fontFor(c.style), (float)fsize,
                                  cui_vec2{px_x, px_y},
                                  paintColor(col, c.style.opacity), str.c_str(), -1);
                }
            }
            paintSvgText(cid, xf, clip);
        }
    }

    // Depth-walks an SVG subtree turning shape elements into Path records. Group
    // containers (<g>, <defs>, unknown) contribute nothing themselves but their
    // children still inherit fill/stroke through the CSS cascade.
    void collectSvgShapes(int64_t id, std::vector<cuiw_svg::Path>& paths) {
        for (int64_t cid : doc.nodes[id].children) {
            Node& c = doc.nodes[cid];
            if (c.kind != Node::Element) continue;
            if (c.tag != "g" && c.tag != "defs" && c.tag != "svg") {
                cuiw_svg::Path p;
                if (svgShapeGeometry(c, p)) {
                    // Fill/stroke come from the resolved cascade; fold in the two
                    // SVG opacities and the accumulated group opacity (CSS 1-100).
                    p.fill = scaleRgba(c.style.fill, c.style.fill_opacity * c.style.opacity);
                    p.stroke = scaleRgba(c.style.stroke, c.style.stroke_opacity * c.style.opacity);
                    p.stroke_width = c.style.stroke_width;
                    paths.push_back(std::move(p));
                }
            }
            collectSvgShapes(cid, paths);
        }
    }

    static cuiw_svg::Rgba scaleRgba(const Rgba& c, float a) {
        return cuiw_svg::Rgba{c.r, c.g, c.b, c.a * std::clamp(a, 0.f, 1.f)};
    }

    // Paints a <canvas> element: replays its recorded 2D-context commands into
    // the element box. Canvas coords are CSS px relative to the element's top-
    // left, so they map straight onto the box origin (n.x, n.y) — the same
    // rasterizer inline SVG uses draws the fills/strokes, clipped to the box.
    void paintCanvas(int64_t id) {
        Node& n = doc.nodes[id];
        if (n.canvas_cmds.empty() || n.w <= 0 || n.h <= 0) return;
        const double ox = n.x, oy = n.y;
        cui_rect clip{(float)n.x, (float)n.y, (float)n.w, (float)n.h};
        cui_push_clip(ui, clip);

        // Canvas state. Defaults per the 2D-context spec: black fill/stroke,
        // 1 px line, alpha 1.
        Rgba fill_col{0, 0, 0, 1}, stroke_col{0, 0, 0, 1};
        double line_w = 1.0, galpha = 1.0;
        // The current path: a list of subpaths, each a point list. Canvas builds
        // one path across many commands, then fill()/stroke() paint it all.
        std::vector<std::vector<cuiw_svg::Pt>> subpaths;
        bool cur_closed = false;
        auto new_subpath = [&]() {
            subpaths.emplace_back();
            cur_closed = false;
        };
        new_subpath();

        auto paint_poly = [&](const std::vector<cuiw_svg::Pt>& pts, bool closed,
                              bool do_fill, bool do_stroke) {
            if (pts.empty()) return;
            if (do_fill) {
                cuiw_svg::Path p;
                p.pts = pts; p.closed = true;
                p.stroke = {0, 0, 0, 0};
                p.fill = scaleRgba(fill_col, (float)galpha);
                cuiw_svg::paintPaths(ui, {p}, cuiw_svg::Xform{1, ox, oy}, clip);
            }
            if (do_stroke && line_w > 0) {
                cuiw_svg::Path p;
                p.pts = pts; p.closed = closed;
                p.fill = {0, 0, 0, 0};
                p.stroke = scaleRgba(stroke_col, (float)galpha);
                p.stroke_width = line_w;
                cuiw_svg::paintPaths(ui, {p}, cuiw_svg::Xform{1, ox, oy}, clip);
            }
        };

        for (const CanvasCmd& c : n.canvas_cmds) {
            switch (c.kind) {
                case CanvasCmd::FillStyle: fill_col = {c.col.r, c.col.g, c.col.b, c.col.a}; break;
                case CanvasCmd::StrokeStyle: stroke_col = {c.col.r, c.col.g, c.col.b, c.col.a}; break;
                case CanvasCmd::LineWidth: line_w = c.a[0]; break;
                case CanvasCmd::GlobalAlpha: galpha = std::clamp(c.a[0], 0.0, 1.0); break;
                case CanvasCmd::FillRect: {
                    std::vector<cuiw_svg::Pt> r;
                    appendRect(c.a[0], c.a[1], c.a[2], c.a[3], 0, 0, r);
                    paint_poly(r, true, true, false);
                    break;
                }
                case CanvasCmd::ClearRect: {
                    // Clear = paint the element's own background over the rect
                    // (the canvas box is opaque in this renderer, so "erase" is
                    // "repaint the backdrop"), honouring the CSS background.
                    Rgba bg = n.style.bg;
                    if (bg.a <= 0) break;
                    std::vector<cuiw_svg::Pt> r;
                    appendRect(c.a[0], c.a[1], c.a[2], c.a[3], 0, 0, r);
                    cuiw_svg::Path p;
                    p.pts = r; p.closed = true; p.stroke = {0, 0, 0, 0};
                    p.fill = scaleRgba(bg, (float)galpha);
                    cuiw_svg::paintPaths(ui, {p}, cuiw_svg::Xform{1, ox, oy}, clip);
                    break;
                }
                case CanvasCmd::StrokeRect: {
                    std::vector<cuiw_svg::Pt> r;
                    appendRect(c.a[0], c.a[1], c.a[2], c.a[3], 0, 0, r);
                    paint_poly(r, true, false, true);
                    break;
                }
                case CanvasCmd::BeginPath:
                    subpaths.clear();
                    new_subpath();
                    break;
                case CanvasCmd::MoveTo:
                    // A moveTo after drawing starts a new, unconnected subpath.
                    if (!subpaths.back().empty()) new_subpath();
                    subpaths.back().push_back({c.a[0], c.a[1]});
                    break;
                case CanvasCmd::LineTo:
                    subpaths.back().push_back({c.a[0], c.a[1]});
                    break;
                case CanvasCmd::Rect: {
                    // rect() adds a closed rectangular subpath.
                    if (!subpaths.back().empty()) new_subpath();
                    appendRect(c.a[0], c.a[1], c.a[2], c.a[3], 0, 0, subpaths.back());
                    cur_closed = true;
                    new_subpath();
                    break;
                }
                case CanvasCmd::Arc: {
                    // Flatten the arc sweep into points (canvas angles are in
                    // radians, y-down so increasing angle sweeps clockwise).
                    double cx = c.a[0], cy = c.a[1], rad = c.a[2];
                    double a0 = c.a[3], a1 = c.a[4];
                    if (rad <= 0) break;
                    double sweep = a1 - a0;
                    while (sweep < 0) sweep += 2 * 3.14159265358979;
                    if (sweep > 2 * 3.14159265358979) sweep = 2 * 3.14159265358979;
                    int steps = std::max(8, (int)std::ceil(sweep / 0.2));
                    bool full = sweep >= 2 * 3.14159265358979 - 1e-6;
                    if (!subpaths.back().empty()) new_subpath();
                    for (int i = 0; i <= steps; i++) {
                        double a = a0 + sweep * (double)i / (double)steps;
                        subpaths.back().push_back({cx + rad * std::cos(a), cy + rad * std::sin(a)});
                    }
                    cur_closed = full;
                    if (full) new_subpath();   // a full circle is its own subpath
                    break;
                }
                case CanvasCmd::ClosePath:
                    if (!subpaths.back().empty()) subpaths.back().push_back(subpaths.back().front());
                    cur_closed = true;
                    break;
                case CanvasCmd::Fill:
                    for (auto& sp : subpaths) paint_poly(sp, true, true, false);
                    break;
                case CanvasCmd::Stroke:
                    for (auto& sp : subpaths) paint_poly(sp, cur_closed, false, true);
                    break;
                case CanvasCmd::FillText:
                    if (!c.text.empty()) {
                        cui_color col = paintColor(fill_col, (float)galpha);
                        cui_draw_text(ui, fontFor(n.style), (float)n.style.font_size,
                                      cui_vec2{ (float)(ox + c.a[0]), (float)(oy + c.a[1]) },
                                      col, c.text.c_str(), -1);
                    }
                    break;
                case CanvasCmd::DrawImage: {
                    // Decode the data: URL once (cached by URL) and draw it into
                    // the dest rect (x, y, w, h), scaled to cover like <img>.
                    int iw = 0, ih = 0;
                    cui_texture tex = canvasImageTex(c.text, iw, ih);
                    if (tex == 0 || iw <= 0 || ih <= 0 || c.a[2] <= 0 || c.a[3] <= 0) break;
                    double bw = c.a[2], bh = c.a[3];
                    double dw, dh;
                    double bar = bw / bh, iar = (double)iw / (double)ih;
                    if (iar > bar) { dh = bh; dw = bh * iar; }
                    else { dw = bw; dh = bw / iar; }
                    double dx = c.a[0] + (bw - dw) / 2.0, dy = c.a[1] + (bh - dh) / 2.0;
                    cui_rect dst{(float)(ox + dx), (float)(oy + dy), (float)dw, (float)dh};
                    cui_draw_image(ui, tex, dst, cui_rect{0, 0, 1, 1},
                                   CUI_CORNERS(0), CUI_TRANSPARENT);
                    break;
                }
            }
        }
        cui_pop_clip(ui);
    }

    // Paints an <img>: decode its data: URL once into RGBA pixels, upload to a
    // texture on first paint, then draw it fitted into the element box. The
    // picture is scaled to cover the box preserving aspect ratio and centred
    // (CSS object-fit: cover), clipped to the box, so it never distorts. A decode
    // failure is remembered (img_tried) and draws nothing rather than retrying.
    void paintImage(int64_t id) {
        Node& n = doc.nodes[id];
        if (n.w <= 0 || n.h <= 0) return;
        // Decode once. img_tried guards against re-attempting a URL that failed.
        if (!n.img_tried) {
            n.img_tried = true;
            std::string src = getAttr(n, "src");
            if (!cuiw_img::decode_data_url(src, n.img_px, n.img_w, n.img_h)) {
                n.img_px.clear(); n.img_w = 0; n.img_h = 0;
            }
        }
        if (n.img_px.empty() || n.img_w <= 0 || n.img_h <= 0) return;
        // Upload to a texture once and reuse it across frames.
        if (!n.img_tex_valid) {
            n.img_tex = cui_image_create(ui, n.img_w, n.img_h, n.img_px.data());
            n.img_tex_valid = n.img_tex != 0;
            if (!n.img_tex_valid) return;
        }
        // Fit the picture inside the box preserving aspect ratio (object-fit:
        // cover) — scale to cover, then centre, so no letterboxing or distortion.
        double box_ar = n.w / n.h;
        double img_ar = (double)n.img_w / (double)n.img_h;
        double dw, dh;
        if (img_ar > box_ar) { dh = n.h; dw = n.h * img_ar; }
        else { dw = n.w; dh = n.w / img_ar; }
        double dx = n.x + (n.w - dw) / 2.0, dy = n.y + (n.h - dh) / 2.0;
        cui_rect clip{(float)n.x, (float)n.y, (float)n.w, (float)n.h};
        cui_push_clip(ui, clip);
        cui_rect dst{(float)dx, (float)dy, (float)dw, (float)dh};
        // uv is the full 0..1 source rectangle; tint white = draw as-is.
        cui_rect uv{0, 0, 1, 1};
        cui_draw_image(ui, n.img_tex, dst, uv,
                       CUI_CORNERS((float)n.style.radius), CUI_TRANSPARENT);
        cui_pop_clip(ui);
    }

    // Paints a background gradient (linear by angle, radial from the centre,
    // repeating variants) by sampling it per pixel into a small RGBA texture,
    // uploading that once, and drawing it scaled to the element box with the
    // element's rounded corners. A texture handles every gradient type and
    // rounded corners uniformly, unlike per-band drawing.
    void paintGradient(int64_t id) {
        Node& n = doc.nodes[id];
        if (n.w <= 0 || n.h <= 0 || !n.style.grad.has) return;
        const Gradient& g = n.style.grad;

        // Generate the texture once at a reduced resolution — gradients are
        // smooth, so a modest texture upscaled to the box looks identical and
        // is cheap to build. Cap the longer side to keep it small.
        if (!n.grad_tex_valid) {
            const int kMax = 128;
            double s = std::min(kMax / std::max(1.0, n.w), kMax / std::max(1.0, n.h));
            int tw = std::max(1, (int)std::lround(n.w * s));
            int th = std::max(1, (int)std::lround(n.h * s));
            std::vector<uint8_t> px((size_t)tw * th * 4);

            // Gradient-line direction (CSS coords, y down). angle is clockwise
            // from "to top": 0 = up, 90 = right, 180 = down, 270 = left.
            double a = g.angle * 3.14159265358979 / 180.0;
            double dx = std::sin(a), dy = -std::cos(a);
            // Radial: distance from the box centre to the farthest corner is the
            // radius, so the gradient reaches the corners exactly.
            double cx = n.w / 2.0, cy = n.h / 2.0;
            double radius = std::hypot(std::max(cx, n.w - cx), std::max(cy, n.h - cy));
            double pmin = 0, pmax = 0;
            if (!g.radial) {
                const double corners[4][2] = {{0, 0}, {n.w, 0}, {0, n.h}, {n.w, n.h}};
                for (int i = 0; i < 4; i++) {
                    double pr = (corners[i][0] - cx) * dx + (corners[i][1] - cy) * dy;
                    pmin = std::min(pmin, pr); pmax = std::max(pmax, pr);
                }
            }
            double prange = (pmax - pmin) > 0 ? (pmax - pmin) : 1.0;
            // For a repeating gradient the last stop offset is the tile length
            // along the projection axis; the pattern repeats every tile.
            double tile = g.stops.back().first;
            if (tile <= 0) tile = 1.0;

            for (int py = 0; py < th; py++) {
                for (int pxi = 0; pxi < tw; pxi++) {
                    double ux = (pxi + 0.5) / tw * n.w;
                    double uy = (py + 0.5) / th * n.h;
                    double t;
                    if (g.radial) {
                        t = std::hypot(ux - cx, uy - cy) / radius;
                    } else {
                        t = ((ux - cx) * dx + (uy - cy) * dy - pmin) / prange;
                    }
                    if (g.repeating) t = t - std::floor(t / tile) * tile;
                    Rgba c = sampleGradStops(g.stops, t);
                    uint8_t* p = px.data() + ((size_t)py * tw + pxi) * 4;
                    p[0] = (uint8_t)std::clamp(c.r * 255.f, 0.f, 255.f);
                    p[1] = (uint8_t)std::clamp(c.g * 255.f, 0.f, 255.f);
                    p[2] = (uint8_t)std::clamp(c.b * 255.f, 0.f, 255.f);
                    p[3] = (uint8_t)std::clamp(c.a * 255.f, 0.f, 255.f);
                }
            }
            n.grad_tex = cui_image_create(ui, tw, th, px.data());
            n.grad_tex_valid = n.grad_tex != 0;
            if (!n.grad_tex_valid) return;
        }

        // Draw the texture scaled to the full box with the rounded corners.
        cui_rect dst{(float)n.x, (float)n.y, (float)n.w, (float)n.h};
        cui_rect uv{0, 0, 1, 1};
        cui_draw_image(ui, n.grad_tex, dst, uv,
                       CUI_CORNERS((float)n.style.radius), CUI_TRANSPARENT);
    }

    // `content_dy` is the accumulated vertical offset from ancestor scroll
    // containers (0 for a non-scrolled document, so non-scrolling docs are
    // byte-identical to before). The box's own frame (background, border,
    // shadow, clip) ignores it — only its content (text + children) translates.
    void paintNode(int64_t id, double content_dy = 0) {
        Node& n = doc.nodes[id];
        if (n.kind == Node::Comment || n.style.display == Display::None) return;
        if (n.tag == "head" || !n.ever_rendered) { for (int64_t c : n.children) paintNode(c); return; }
        // An <svg> viewport paints its whole subtree geometrically and must not
        // fall through to the block/inline recursion (shape children carry
        // display:none and are placed by the viewBox, not by layout).
        if (n.tag == "svg") { paintSvg(id); return; }
        // A <canvas> paints its recorded 2D-context commands into its box and
        // has no HTML children, so it does not fall through to the recursion.
        if (n.tag == "canvas") { paintCanvas(id); return; }
        // An <img> paints its decoded picture into its box and has no HTML
        // children, so it does not fall through to the recursion.
        if (n.tag == "img") { paintImage(id); return; }
        Style& s = n.style;
        // Draw in CSS logical points — cui_set_viewport(scale) scales them to
        // device px internally, so pre-multiplying here double-scales (the card
        // landed at 2x its CSS x). r/rad/border/font_size are all CSS units.
        // content_dy is the offset inherited from a scrolled ancestor: this node
        // is content inside that ancestor, so its whole box (background, border,
        // shadow, clip) moves with the scroll. Its OWN scroll_top offsets only
        // its children, applied further down via `eff`.
        cui_rect r{(float)n.x, (float)(n.y + content_dy), (float)n.w, (float)n.h};
#ifdef CUIW_LAYOUT_DEBUG
        if (n.tag == "div" || n.tag == "body")
            fprintf(stderr, "[paintgeo] tag=%s css_x=%g css_w=%g scale=%g\n",
                    n.tag.c_str(), n.x, n.w, scale);
#endif
        cui_corners rad = CUI_CORNERS((float)s.radius);
        // box-shadow is painted before the box so the box covers the shadow's
        // center and only the blurred edge shows around it (CSS paints it behind).
        if (s.has_shadow && n.w > 0 && n.h > 0)
            cui_draw_shadow(ui, r, rad, (float)s.shadow_blur, (float)s.shadow_spread,
                            cui_vec2{ (float)s.shadow_x, (float)s.shadow_y },
                            paintColor(s.shadow_color, s.opacity));
        if ((s.bg.a > 0 || s.grad.has) && n.w > 0 && n.h > 0) {
#ifdef CUIW_LAYOUT_DEBUG
            if (n.tag == "div" || n.tag == "body")
                fprintf(stderr, "[paintrect] tag=%s cls=%s css=(%g,%g %gx%g)\n", n.tag.c_str(),
                        [&]{ std::string c; for (auto&a:n.attrs) if(a.first=="class") c=a.second; return c; }().c_str(),
                        r.x, r.y, r.w, r.h);
#endif
            // A gradient replaces the flat fill (the gradient wins when both are
            // set, matching CSS where background-image paints over background-color).
            if (s.grad.has) {
                paintGradient(id);
                if (s.border > 0)
                    cui_draw_rect_border(ui, r, rad, CUI_TRANSPARENT,
                                         paintColor(s.border_color, s.opacity),
                                         (float)s.border);
            } else {
                cui_color bg = paintColor(s.bg, s.opacity);
                if (s.border > 0)
                    cui_draw_rect_border(ui, r, rad, bg,
                                         paintColor(s.border_color, s.opacity),
                                         (float)s.border);
                else {
#ifdef CUIW_LAYOUT_DEBUG
                    if (n.tag == "div")
                        fprintf(stderr, "[drawrect] card css=(%.0f,%.0f %.0fx%.0f) scale=%g\n",
                                r.x, r.y, r.w, r.h, scale);
#endif
                    cui_draw_rect(ui, r, rad, bg);
                }
            }
        }
        // overflow: hidden clips and scroll/auto clip + translate content to the
        // padding box. Background/border above are painted unclipped, matching
        // CSS — only descendant content is cut at the rounded edge.
        const bool clips = (s.overflow != Overflow::Visible && n.w > 0 && n.h > 0);
        // A scroll container shifts its content up by its (clamped) scroll_top.
        // The scrollable extent is how far the children run past the box bottom.
        double scroll = 0;
        if (s.overflow == Overflow::Scroll && n.h > 0) {
            double content_bottom = n.y;
            for (int64_t c : n.children) {
                Node& ch = doc.nodes[c];
                content_bottom = std::max(content_bottom, ch.y + ch.h);
            }
            double scrollable = std::max(0.0, content_bottom - (n.y + n.h));
            scroll = std::clamp(n.scroll_top, 0.0, scrollable);
        }
        // eff is the total y offset for this box's CONTENT (text + children):
        // the offset inherited from ancestor scrollers minus this box's scroll.
        // The box frame itself ignores it and stays at n.x/n.y.
        const double eff = content_dy - scroll;
        if (clips) {
            double pad_t = s.pad[0] + s.border, pad_r = s.pad[1] + s.border;
            double pad_b = s.pad[2] + s.border, pad_l = s.pad[3] + s.border;
            cui_rect cr{ (float)(n.x + pad_l), (float)(n.y + pad_t),
                         (float)std::max(0.0, n.w - pad_l - pad_r),
                         (float)std::max(0.0, n.h - pad_t - pad_b) };
            cui_push_clip(ui, cr);
        }
        if (n.kind == Node::Text && !n.segs.empty()) {
            // Inline flow: draw positioned segments, each at its own (x, y).
            cui_color col = paintColor(s.color, s.opacity);
            for (const auto& sg : n.segs)
                if (!sg.text.empty()) {
                    cui_draw_text(ui, fontFor(s), (float)s.font_size,
                                  cui_vec2{ (float)sg.x, (float)(sg.y + eff) }, col, sg.text.c_str(), -1);
                    drawTextDecoration(s, sg.x, sg.y + eff, sg.w);
                }
        }
        else if (n.kind == Node::Text && !n.lines.empty()) {
            cui_color col = paintColor(s.color, s.opacity);
            double ty = n.y + eff;
            for (const auto& ln : n.lines) {
                if (!ln.text.empty()) {
                    /* text-align: offset the line within the text box (n.w).
                     * A block text node spans its parent's content width, so the
                     * free space is n.w - line width. */
                    double free_w = n.w - ln.width;
                    double tx = n.x;
                    if (free_w > 0) {
                        if (s.text_align == TextAlign::Center) tx = n.x + free_w / 2.0;
                        else if (s.text_align == TextAlign::Right) tx = n.x + free_w;
                    }
                    cui_draw_text(ui, fontFor(s), (float)s.font_size,
                                  cui_vec2{ (float)tx, (float)ty }, col, ln.text.c_str(), -1);
                    drawTextDecoration(s, tx, ty, ln.width);
                }
                ty += n.line_height_px;
            }
        }
        for (int64_t c : n.children) paintNode(c, eff);
        if (clips) cui_pop_clip(ui);
    }

    cui_color bubbleColor() const {
        return doc.theme == "dark" ? cui_color{0x21, 0x21, 0x21, 0xFF} : cui_color{0xFF, 0xFF, 0xFF, 0xFF};
    }

    // ───────────── ops ─────────────

    // Parses one canvas 2D-context command: ["fillRect", x, y, w, h]. Returns
    // false for a name the engine does not model (the caller skips it).
    static bool parseCanvasCmd(const Json& c, CanvasCmd& cc) {
        const std::string name = c[0].asString();
        auto num = [&](int i) { return c.size() > (size_t)i ? c[i].asNumber() : 0.0; };
        auto str = [&](int i) { return c.size() > (size_t)i ? c[i].asString() : std::string(); };
        Rgba col;
        if (name == "fillStyle" || name == "strokeStyle") {
            cc.kind = name == "fillStyle" ? CanvasCmd::FillStyle : CanvasCmd::StrokeStyle;
            if (!parseColor(str(1), col)) return false;
            cc.col = {col.r, col.g, col.b, col.a};
            return true;
        }
        if (name == "lineWidth") { cc.kind = CanvasCmd::LineWidth; cc.a[0] = num(1); return true; }
        if (name == "globalAlpha") { cc.kind = CanvasCmd::GlobalAlpha; cc.a[0] = num(1); return true; }
        if (name == "fillRect") {
            cc.kind = CanvasCmd::FillRect;
            cc.a[0] = num(1); cc.a[1] = num(2); cc.a[2] = num(3); cc.a[3] = num(4);
            return true;
        }
        if (name == "clearRect") {
            cc.kind = CanvasCmd::ClearRect;
            cc.a[0] = num(1); cc.a[1] = num(2); cc.a[2] = num(3); cc.a[3] = num(4);
            return true;
        }
        if (name == "strokeRect") {
            cc.kind = CanvasCmd::StrokeRect;
            cc.a[0] = num(1); cc.a[1] = num(2); cc.a[2] = num(3); cc.a[3] = num(4);
            return true;
        }
        if (name == "beginPath") { cc.kind = CanvasCmd::BeginPath; return true; }
        if (name == "moveTo") { cc.kind = CanvasCmd::MoveTo; cc.a[0] = num(1); cc.a[1] = num(2); return true; }
        if (name == "lineTo") { cc.kind = CanvasCmd::LineTo; cc.a[0] = num(1); cc.a[1] = num(2); return true; }
        if (name == "rect") {
            cc.kind = CanvasCmd::Rect;
            cc.a[0] = num(1); cc.a[1] = num(2); cc.a[2] = num(3); cc.a[3] = num(4);
            return true;
        }
        // arc(x, y, r, start, end): a full circle when the sweep is >= 2*pi.
        if (name == "arc") {
            cc.kind = CanvasCmd::Arc;
            cc.a[0] = num(1); cc.a[1] = num(2); cc.a[2] = num(3);
            cc.a[3] = num(4); cc.a[4] = num(5);
            return true;
        }
        if (name == "closePath") { cc.kind = CanvasCmd::ClosePath; return true; }
        if (name == "fill") { cc.kind = CanvasCmd::Fill; return true; }
        if (name == "stroke") { cc.kind = CanvasCmd::Stroke; return true; }
        if (name == "fillText") {
            cc.kind = CanvasCmd::FillText;
            cc.text = str(1); cc.a[0] = num(2); cc.a[1] = num(3);
            return true;
        }
        // drawImage(<data: URL>, x, y, w, h): the URL rides in cc.text, the dest
        // rect in a[0..3]. The URL is decoded once and cached by the caller.
        if (name == "drawImage") {
            cc.kind = CanvasCmd::DrawImage;
            cc.text = str(1);
            cc.a[0] = num(2); cc.a[1] = num(3); cc.a[2] = num(4); cc.a[3] = num(5);
            return !cc.text.empty();
        }
        return false;
    }

    void applyOps(const Json& ops) {
        for (const Json& op : ops.arr()) applyOp(op);
    }

    void applyOp(const Json& op) {
        std::string k = op["op"].asString();
        int64_t id = (int64_t)op["id"].asNumber();
        if (k == "el") {
            Node& n = doc.nodes[id];
            n.id = id; n.kind = Node::Element; n.tag = op["tag"].asString();
            for (char& c : n.tag) c = (char)std::tolower((unsigned char)c);
            // An <svg> root carries no ns="svg" in real markup (only its shape
            // children do), so recognize the viewport by tag too — otherwise it
            // never gets UA size/display or presentation-attribute handling.
            n.svg = op["ns"].asString() == "svg" || n.tag == "svg";
            n.ever_rendered = false;
            const Json& at = op["attrs"];
            if (at.isArray()) for (const Json& p : at.arr()) if (p.isArray() && p.size() >= 2)
                n.attrs.emplace_back(p[0].asString(), p[1].asString());
            // Where an adopted node lands once it is inserted.
            std::string adopt = op["adopt"].asString();
            if (adopt == "body" || adopt == "head" || adopt == "html") pending_adopt_[id] = adopt;
        } else if (k == "text") {
            Node& n = doc.nodes[id];
            n.id = id; n.kind = Node::Text; n.data = op["data"].asString(); n.ever_rendered = false;
        } else if (k == "comment") {
            Node& n = doc.nodes[id];
            n.id = id; n.kind = Node::Comment; n.data = op["data"].asString();
        } else if (k == "data") {
            if (Node* n = node(doc, id)) n->data = op["data"].asString();
        } else if (k == "attr") {
            if (Node* n = node(doc, id)) setAttr(*n, op["name"].asString(), op["value"].asString());
        } else if (k == "rattr") {
            if (Node* n = node(doc, id)) {
                std::string nm = op["name"].asString();
                n->attrs.erase(std::remove_if(n->attrs.begin(), n->attrs.end(),
                                              [&](const auto& p) { return p.first == nm; }), n->attrs.end());
            }
        } else if (k == "style") {
            if (Node* n = node(doc, id)) setAttr(*n, "style", mergeStyle(*n, op["name"].asString(), op["value"].asString()));
        } else if (k == "css") {
            if (Node* n = node(doc, id)) if (n->tag == "style") n->data += op["text"].asString();
            parseSheet(op["text"].asString(), doc.sheet, doc.keyframes);
        } else if (k == "ins") {
            int64_t parent = (int64_t)op["parent"].asNumber();
            int64_t before = (int64_t)op["before"].asNumber();
            // "adopt" is only a default host for a node that would otherwise be a
            // detached root. An explicit parent from this ins op wins over it,
            // otherwise a nested node (e.g. a button in a flex .row) is pulled
            // back out to body and the parent never sees it as a child.
            auto it = pending_adopt_.find(id);
            if (it != pending_adopt_.end()) {
                if (parent == 0) parent = findHost(it->second);
                pending_adopt_.erase(it);
            }
            attach(doc, parent, id, before);
            if (Node* n = node(doc, id)) n->ever_rendered = true;
            markRendered(doc, id);
        } else if (k == "rm") {
            if (Node* n = node(doc, id)) {
                if (n->parent) if (Node* p = node(doc, n->parent)) {
                    auto& v = p->children;
                    v.erase(std::remove(v.begin(), v.end(), id), v.end());
                }
                doc.nodes.erase(id);
            }
        } else if (k == "prop") {
            // The host drives scrolling: {"op":"prop","id":N,"name":"scrollTop",
            // "value":Y}. Only vertical is modelled. The value is clamped at
            // paint time (once the box and its content heights are known), so
            // here we just store it; a non-scrollable box ignores it.
            if (Node* n = node(doc, id)) {
                std::string nm = op["name"].asString();
                double v = op["value"].asNumber();
                if (nm == "scrollTop") n->scroll_top = v;
            }
        }
        else if (k == "call") { /* scrollTo/setPointerCapture: no-ops here */ }
        else if (k == "canvas") {
            // Record a batch of 2D-context commands against the canvas element.
            // Each command is [name, ...args]; unknown names are skipped so a
            // command the engine does not model degrades to "not drawn" rather
            // than corrupting the batch.
            if (Node* n = node(doc, id)) {
                const Json& cmds = op["cmds"];
                if (cmds.isArray()) for (const Json& c : cmds.arr()) {
                    if (!c.isArray() || c.size() < 1) continue;
                    CanvasCmd cc;
                    if (!parseCanvasCmd(c, cc)) continue;
                    n->canvas_cmds.push_back(std::move(cc));
                }
            }
        }
    }

    std::map<int64_t, std::string> pending_adopt_;
    // Nodes with position:absolute, collected during the flow layout (they are
    // skipped from normal flow) and positioned against their nearest positioned
    // ancestor once that ancestor's box is final.
    std::vector<int64_t> abs_nodes_;

    int64_t findHost(const std::string& tag) const {
        for (const auto& [id, n] : doc.nodes) if (n.tag == tag) return id;
        return 0;
    }

    static void setAttr(Node& n, const std::string& name, const std::string& value) {
        for (auto& a : n.attrs) if (a.first == name) { a.second = value; return; }
        n.attrs.emplace_back(name, value);
    }

    static std::string mergeStyle(const Node& n, const std::string& prop, const std::string& value) {
        std::string out;
        for (const auto& a : n.attrs) if (a.first == "style") out = a.second;
        // Replace an existing declaration of `prop`, else append.
        std::string find = prop + ":";
        size_t p = out.find(find);
        if (p != std::string::npos) {
            size_t end = out.find(';', p);
            std::string repl = prop + ":" + value;
            if (end == std::string::npos) return out.substr(0, p) + repl;
            return out.substr(0, p) + repl + out.substr(end);
        }
        if (!out.empty() && out.back() != ';') out += ";";
        out += prop + ":" + value;
        return out;
    }

    // ───────────── queries ─────────────

    Json answerQuery(const Json& q) {
        std::string kind = q["q"].asString();
        Json a = Json::object();
        if (kind == "measureText") {
            Style s; s.font_size = kDefaultFontSize;
            std::string font = q["font"].asString();
            size_t px = font.find("px");
            if (px != std::string::npos) {
                size_t st = font.find_last_of(' ');
                if (st != std::string::npos) s.font_size = std::atof(font.substr(st + 1, px - st - 1).c_str());
            }
            s.bold = font.find("bold") != std::string::npos || font.find("600") != std::string::npos || font.find("700") != std::string::npos;
            std::string text = q["text"].asString();
            cui_text_metrics m = cui_measure_text(ui, fontFor(s), (float)s.font_size, text.c_str(), -1);
            a.set("width", Json(m.width));
            a.set("height", Json(m.height));
            a.set("ascent", Json(m.ascent));
            a.set("descent", Json(m.descent));
            a.set("lineHeight", Json(m.line_height));
            return a;
        }
        if (kind == "rect" || kind == "rects" || kind == "box") {
            relayout();   // geometry the query reads must reflect every op applied so far
            int64_t id = (int64_t)q["id"].asNumber();
            auto it = doc.nodes.find(id);
            Json r = Json::object();
            if (it != doc.nodes.end()) {
                const Node& n = it->second;
                // getBoundingClientRect consumes the result directly as the rect, so a
                // single box (q:"rect"/"box") must be the object, not wrapped in an array.
                r.set("x", Json(n.x)); r.set("y", Json(n.y));
                r.set("width", Json(n.w)); r.set("height", Json(n.h));
                r.set("left", Json(n.x)); r.set("top", Json(n.y));
                r.set("right", Json(n.x + n.w)); r.set("bottom", Json(n.y + n.h));
            }
            if (kind == "rects") { Json arr = Json::array(); if (it != doc.nodes.end()) arr.push(r); return arr; }
            return r;
        }
        if (kind == "elementFromPoint") {
            double x = q["x"].asNumber(), y = q["y"].asNumber();
            int64_t hit = hitTest(x, y);
            a.set("id", Json((double)hit));
            return a;
        }
        (void)seq;
        return a;
    }

    int64_t hitTest(double x, double y) {
        int64_t best = 0;
        for (const auto& [id, n] : doc.nodes) {
            if (n.kind == Node::Comment || !n.ever_rendered) continue;
            if (x >= n.x && x <= n.x + n.w && y >= n.y && y <= n.y + n.h) {
                if (!best || n.w * n.h < doc.nodes[best].w * doc.nodes[best].h) best = id;
            }
        }
        return best;
    }

    int64_t documentId() const {
        for (const auto& [id, n] : doc.nodes) if (n.id == 1) return id;
        return 1;
    }

    // Applies one host → renderer message to the document and queues answers.
    void handleMessage(const Json& msg) {
        std::string type = msg["type"].asString();
        if (type == "ops") { applyOps(msg["ops"]); relayout(); }
        else if (type == "clock") {
            // Test/determinism hook: freeze the animation clock and step it by hand.
            // With virtual=true animNow() returns `now` instead of wall-clock, so a
            // host can render exact transition frames. Not part of the widget profile.
            clock_virtual_ = msg["virtual"].asBool();
            if (const Json* nowv = msg.find("now")) clock_virtual_now_ = nowv->asNumber();
        }
        else if (type == "query") {
            Json answer = answerQuery(msg["query"]);
            Json result = Json::object();
            result.set("type", Json("result"));
            result.set("qid", msg["qid"]);
            result.set("result", answer);
            out.push_back(result.dump());
        } else if (type == "listen") { /* the widget subscribed to an event */ }
        else if (type == "preventDefault") { /* the widget consumed a gesture */ }
        else if (type == "theme") doc.theme = msg["theme"].asString();
        else if (type == "safeArea") { /* content safe area: not modelled yet */ }
        else if (type == "policy") { /* paint policy */ }
        else if (type == "viewport") {
            const Json* w = msg.find("width");
            if (w) doc.width = w->asNumber();
        }
    }
};

// ───────────── the adapter (mirrors BlitzRenderer) ─────────────

namespace {

} // namespace

RendererFactory CuiRenderer::factory(double dpr) {
    return [dpr](std::shared_ptr<SandboxResources>, const std::string& theme, double width) -> std::unique_ptr<Renderer> {
        return std::make_unique<CuiRenderer>(theme, width, dpr);
    };
}

double CuiRenderer::nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

CuiRenderer::CuiRenderer(const std::string& theme, double width, double dpr) : dpr_(dpr), width_(width), theme_(theme) {
    double t0 = nowMs();
    impl_ = std::make_unique<Impl>();
    impl_->doc.theme = theme.empty() ? "light" : theme;
    impl_->doc.width = width > 0 ? width : 320;
    impl_->sw = cui_sw_create();
    cui_ctx_desc desc{};
    desc.renderer = cui_sw_iface(impl_->sw);
    desc.theme = impl_->doc.theme == "dark" ? CUI_THEME_DARK : CUI_THEME_LIGHT;
    impl_->ui = cui_create(&desc);
    impl_->scale = dpr;
    if (impl_->ui) cui_set_viewport(impl_->ui, (float)impl_->doc.width, 600, (float)dpr);
    initMs_ = nowMs() - t0;
    startClock();
}

CuiRenderer::~CuiRenderer() { destroy(); }

void CuiRenderer::startClock() {
    std::lock_guard<std::mutex> c(clockMutex_);
    if (stopClock_ || clock_.joinable()) return;
    stopClock_ = false;
    clock_ = std::thread([this] { clockLoop(); });
}

void CuiRenderer::stopClock() {
    {
        std::lock_guard<std::mutex> c(clockMutex_);
        if (stopClock_) return;
        stopClock_ = true;
    }
    clockCv_.notify_all();
    if (clock_.joinable()) clock_.join();
}

std::string CuiRenderer::fontReport() { return "cui: built-in Inter/Geist (body/medium)"; }

void CuiRenderer::load() {
    if (onDiag) onDiag("cui-init", Json::number(initMs_) + " ms");
    if (onMessage) onMessage("{\"type\":\"ready\"}");
    schedulePaint();
}

void CuiRenderer::send(const std::string& json) {
    if (json.rfind("{\"type\":\"requestFrame\"", 0) == 0) { requestFrame(); return; }
    if (json.rfind("{\"type\":\"frozen\"", 0) == 0) { std::lock_guard<std::mutex> c(clockMutex_); frozen_ = true; return; }
    std::vector<std::string> out;
    {
        std::lock_guard<std::mutex> l(mutex_);
        if (destroyed_ || !impl_) return;
        impl_->out.clear();
        if (auto msg = Json::parse(json)) impl_->handleMessage(*msg);
        else if (auto arr = Json::parse(json)) { for (const Json& m : arr->arr()) impl_->handleMessage(m); }
        impl_->relayout();
        out = impl_->out;
    }
    // Dispatch outside mutex_: the host may re-enter (send / paint) from onMessage.
    for (const std::string& m : out) if (onMessage) onMessage(m);
    // An ops batch may change pixels without producing a message.
    schedulePaint();
}

void CuiRenderer::setTheme(const std::string& theme) {
    std::lock_guard<std::mutex> l(mutex_);
    if (destroyed_ || !impl_) return;
    impl_->doc.theme = theme.empty() ? "light" : theme;
    impl_->retheme();
    impl_->relayout();
    schedulePaint();
}

void CuiRenderer::setWidth(double width) {
    std::lock_guard<std::mutex> l(mutex_);
    if (destroyed_ || !impl_) return;
    impl_->doc.width = width;
    impl_->relayout();
    schedulePaint();
}

void CuiRenderer::touch(int action, double x, double y, int pointer, bool primary) {
    std::vector<std::string> out;
    {
        std::lock_guard<std::mutex> l(mutex_);
        if (destroyed_ || !impl_) return;
        int64_t target = impl_->hitTest(x, y);
        auto emit = [&](const char* ev, int detail) {
            Json m = Json::object();
            m.set("type", Json("event"));
            Json e = Json::object();
            e.set("type", Json(ev));
            e.set("targetId", Json((double)(target ? target : impl_->documentId())));
            e.set("detail", Json((double)detail));
            e.set("pointerId", Json((double)pointer));
            e.set("primary", Json(primary));
            m.set("ev", e);
            // The renderer saw real input, so the event is trusted: it counts as a user gesture.
            m.set("trusted", Json(true));
            out.push_back(m.dump());
        };
        if (action == 0) emit("pointerdown", 1);
        else if (action == 2) { emit("pointerup", 1); emit("click", 1); }
        else if (action == 1) emit("pointermove", 0);
        else if (action == 3) emit("pointercancel", 0);
    }
    for (const std::string& m : out) if (onMessage) onMessage(m);
}

void CuiRenderer::destroy() {
    stopClock();
    std::lock_guard<std::mutex> l(mutex_);
    destroyed_ = true;
    impl_.reset();
}

void CuiRenderer::dispatch(const std::string& messages) {
    if (messages.empty() || !onMessage) return;
    if (auto arr = Json::parse(messages); arr && arr->isArray()) {
        for (const Json& msg : arr->arr()) {
            if (msg["type"].asString() == "violation" && onDiag) onDiag("renderer-violation", msg["detail"].asString());
            onMessage(msg.dump());
        }
    }
}

CuiRenderer::Frame CuiRenderer::paint() {
    Frame frame;
    std::lock_guard<std::mutex> l(mutex_);
    if (!impl_ || destroyed_ || !impl_->ui) return frame;
    double t0 = nowMs();
    int64_t body = 0;
    for (const auto& [id, n] : impl_->doc.nodes) if (n.tag == "body") { body = id; break; }
    if (!body) return frame;
    double cssH = std::clamp(impl_->content_height > 0 ? impl_->content_height : 50.0, 50.0, 600.0);
    // Draw in CSS logical px; cui_set_viewport applies `scale`, so the draw data
    // comes back at width*scale × height*scale physical px.
    cui_set_viewport(impl_->ui, (float)impl_->doc.width, (float)cssH, (float)impl_->scale);
    cui_begin_frame(impl_->ui, 1.0f / 60.0f);
    cui_draw_rect(impl_->ui, cui_rect{0, 0, (float)impl_->doc.width, (float)cssH},
                  CUI_CORNERS(0), impl_->bubbleColor());
    // Move every active transition to the current anim-clock time and write the
    // interpolated values into the node styles before painting. Static documents
    // have no runs, so this is a no-op pass over an empty map.
    bool animating = impl_->advanceTransitions();
    animating = impl_->advanceKeyframes() || animating;
    impl_->paintNode(body);
    cui_end_frame(impl_->ui);
    // An in-flight transition changes pixels every frame: ask the clock for the
    // next one. When the last run retires this stops on its own.
    if (animating) schedulePaint();
#ifdef CUIW_LAYOUT_DEBUG
    fprintf(stderr, "[paint] cssH=%.1f content_height=%.1f doc.width=%.1f\n", cssH, impl_->content_height, impl_->doc.width);
    for (const auto& [id, n] : impl_->doc.nodes) {
        if (n.kind == Node::Element || n.kind == Node::Text) {
            std::string cls;
            for (const auto& a : n.attrs) if (a.first == "class") cls = a.second;
            fprintf(stderr, "[layout] id=%lld kind=%d tag=%s cls=%s x=%.1f y=%.1f w=%.1f h=%.1f lines=%zu\n",
                    (long long)id, (int)n.kind, n.tag.c_str(), cls.c_str(), n.x, n.y, n.w, n.h, n.lines.size());
            for (size_t li = 0; li < n.lines.size(); li++)
                fprintf(stderr, "    line[%zu] \"%s\" w=%.1f\n", li, n.lines[li].text.c_str(), n.lines[li].width);
        }
    }
#endif
    const cui_draw_data *dd = cui_get_draw_data(impl_->ui);
    if (!dd || dd->width_px == 0 || dd->height_px == 0) return frame;
    frame.width = dd->width_px; frame.height = dd->height_px;
    frame.rgba.assign(size_t(dd->width_px) * dd->height_px * 4, 0);
    cui_sw_render(impl_->sw, dd, frame.rgba.data(), dd->width_px, dd->height_px,
                  dd->width_px * 4, CUI_SW_RGBA);
#ifdef CUIW_LAYOUT_DEBUG
    {
        int y = 10;
        fprintf(stderr, "[pixels] y=%d:", y);
        for (int x = 0; x < (int)dd->width_px; x += 32) {
            const unsigned char *p = frame.rgba.data() + (size_t)y * (dd->width_px * 4) + (size_t)x * 4;
            fprintf(stderr, " x%d=(%u,%u,%u)", x, p[0], p[1], p[2]);
        }
        fprintf(stderr, "\n");
    }
#endif
    frame.paintMs = nowMs() - t0;
    paints_++;
    lastPaintMs_ = frame.paintMs;
    return frame;
}

void CuiRenderer::requestFrame() {
    { std::lock_guard<std::mutex> c(clockMutex_); if (frozen_ || frameRequested_) return; frameRequested_ = true; }
    clockCv_.notify_all();
}

void CuiRenderer::schedulePaint() {
    { std::lock_guard<std::mutex> c(clockMutex_); if (paintScheduled_ || stopClock_) return; paintScheduled_ = true; }
    clockCv_.notify_all();
}

void CuiRenderer::clockLoop() {
    for (;;) {
        bool frame, paint;
        {
            std::unique_lock<std::mutex> l(clockMutex_);
            clockCv_.wait(l, [&] { return stopClock_ || frameRequested_ || paintScheduled_; });
            if (stopClock_) return;
            clockCv_.wait_for(l, std::chrono::duration<double>(kTickSeconds), [&] { return stopClock_; });
            if (stopClock_) return;
            frame = frameRequested_; paint = paintScheduled_;
            frameRequested_ = false; paintScheduled_ = false;
        }
        if (frame && onMessage) onMessage("{\"type\":\"frame\",\"time\":" + Json::number(nowMs()) + "}");
        if (paint) { Frame f = this->paint(); if (!f.empty() && onPaint) onPaint(f); }
    }
}

} // namespace twidget
