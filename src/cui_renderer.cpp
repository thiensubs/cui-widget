#include "twidget/cui_renderer.h"

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
#include <string>
#include <vector>

namespace twidget {

namespace {

constexpr double kTickSeconds = 1.0 / 60;
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

enum class Display { Block, Flex, Inline, Table, TableRowGroup, TableRow, TableCell, None };
enum class FlexDir { Row, Column };
enum class FlexWrap { NoWrap, Wrap };
enum class TextDecoration { None, Underline, LineThrough };
enum class Overflow { Visible, Hidden };
// Vertical alignment of an inline run relative to the line's baseline. Stored as
// a resolved CSS-px shift (negative = raise) so painting is a plain y offset.
enum class VAlign { Baseline, Shift };
enum class Position { Static, Relative, Absolute };
enum class Justify { Start, Center, End, Between };
enum class Align { Stretch, Start, Center, End };
enum class TextAlign { Left, Center, Right };

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
    // top/right/bottom/left offsets. -1 = unset (auto). Resolved against the
    // nearest positioned ancestor's padding box at layout time.
    double top = -1, right = -1, bottom = -1, left = -1;
    // Resolved CSS custom properties (--name -> value) visible on this node:
    // declared here or inherited from the nearest ancestor that declares them.
    // Populated during computeStyle, before var() references are substituted.
    std::map<std::string, std::string> custom_props;
};

// One CSS declaration set (inline style, a rule's block, or the UA defaults).
struct Decls { std::vector<std::pair<std::string, std::string>> kv; };
Decls parseDecls(const std::string& body);

void applyDecl(Style& st, const std::string& name, const std::string& value) {
    auto is = [&](const char* k) { return name == k; };
    if (is("display")) {
        if (value == "flex") st.display = Display::Flex;
        else if (value == "none") st.display = Display::None;
        else if (value.find("inline") != std::string::npos) st.display = Display::Inline;
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
    } else if (is("gap")) { double x; if (parseLenPx(value, x)) st.gap = x; }
    else if (is("padding")) { auto b = boxValues(value); for (int i = 0; i < 4 && i < (int)b.size(); i++) st.pad[i] = b[i]; }
    else if (is("margin")) { auto b = boxValues(value); for (int i = 0; i < 4 && i < (int)b.size(); i++) st.margin[i] = b[i]; }
    else if (is("width")) { double x; bool p; if (parseLenPx(value, x, p)) { st.width = x; st.width_pct = p ? x : -1; } }
    else if (is("height")) { double x; bool p; if (parseLenPx(value, x, p)) { st.height = x; st.height_pct = p ? x : -1; } }
    else if (is("min-height")) { double x; bool p; if (parseLenPx(value, x, p)) { st.min_height = x; st.min_height_pct = p ? x : -1; } }
    else if (is("background") || is("background-color")) { Rgba c; if (parseColor(value, c)) st.bg = c; }
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
    } else if (is("opacity")) { /* compositing not yet supported */ }
    else if (is("text-decoration")) {
        if (value.find("underline") != std::string::npos) st.text_decoration = TextDecoration::Underline;
        else if (value.find("line-through") != std::string::npos) st.text_decoration = TextDecoration::LineThrough;
        else st.text_decoration = TextDecoration::None;
    } else if (is("overflow")) {
        // hidden / clip / auto / scroll all clip content; visible does not.
        st.overflow = value.find("visible") != std::string::npos ? Overflow::Visible : Overflow::Hidden;
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
    } else if (is("top") || is("right") || is("bottom") || is("left")) {
        double x; if (parseLenPx(value, x)) {
            if (is("top")) st.top = x;
            else if (is("right")) st.right = x;
            else if (is("bottom")) st.bottom = x;
            else st.left = x;
        }
    }
}

// A stylesheet rule whose selector is a tag, a ".class" or a "#id" (the profile subset).
struct Rule { std::string tag, cls, id; Decls decls; bool disabled = false; bool is_root = false; };

void parseSheet(const std::string& css, std::vector<Rule>& out) {
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
        // Skip at-rules (@media, @keyframes) and selectors with combinators we
        // do not model; a descendant selector's last simple selector is applied
        // to every match, which is close enough for the profile's flat markup.
        if (!sel.empty() && sel[0] == '@') { i = end; continue; }
        for (char c : sel) if (c == '>' || c == '+' || c == '~') { i = end; goto next; }
        {
            // Trim, strip a trailing pseudo (:hover / :disabled handled separately).
            std::string s;
            for (char c : sel) if (!std::isspace((unsigned char)c)) s.push_back(c);
            // :root (optionally with a trailing pseudo) matches the document root.
            bool isRoot = s.compare(0, 5, ":root") == 0;
            size_t colon = s.find(':');
            bool hasDisabled = colon != std::string::npos && s.compare(colon, 9, ":disabled") == 0;
            if (colon != std::string::npos) s = s.substr(0, colon);
            Rule r;
            if (isRoot) r.is_root = true;
            else if (!s.empty() && s[0] == '.') r.cls = s.substr(1);
            else if (!s.empty() && s[0] == '#') r.id = s.substr(1);
            else r.tag = s;
            r.decls = parseDecls(body);
            r.disabled = hasDisabled;
            out.push_back(std::move(r));
        }
    next:
        i = end;
    }
}

// ───────────── document ─────────────

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
};

struct Doc {
    std::map<int64_t, Node> nodes;
    std::vector<Rule> sheet;
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
    for (const Rule& r : d.sheet) {
        bool match = r.is_root ? (n.tag == "body")
                   : ((!r.tag.empty() && r.tag == n.tag) || (!r.cls.empty() && hasClass(n, r.cls))
                   || (!r.id.empty() && getAttr(n, "id") == r.id));
        if (!match) continue;
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

    ~Impl() {
        if (ui) cui_destroy(ui);
        if (sw) cui_sw_destroy(sw);
    }

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
            while (ci < n.children.size()) {
                int64_t cid = n.children[ci];
                Node& c = doc.nodes[cid];
                if (c.kind == Node::Comment || c.style.display == Display::None) { ci++; continue; }
                if (outOfFlow(c)) { abs_nodes_.push_back(cid); ci++; continue; }
                bool c_inline = c.kind == Node::Text || c.style.display == Display::Inline;
                if (!c_inline) {
                    double child_avail = avail_inner;
                    double used_w = c.style.width >= 0 ? c.style.width : avail_inner;
                    // Record the block child's box top before laying it out —
                    // layoutNode sets x/w but not y, so without this every block
                    // sibling kept y=0 and stacked boxes overlapped.
                    c.y = cy + c.style.margin[0];
                    double h = layoutNode(cid, cx, cy, child_avail, -1, definite_content_h);
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
                double rh = layoutInlineRun(run, cx, cy, avail_inner);
                cy += rh;
                inner_h += rh;
            }
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

    // ───────────── paint ─────────────

    void drawTextDecoration(const Style& s, double x, double y_top, double width) {
        if (s.text_decoration == TextDecoration::None || width <= 0) return;
        float thickness = (float)std::max(1.0, s.font_size / 16.0);
        double ly = (s.text_decoration == TextDecoration::Underline)
                        ? y_top + s.font_size * 0.92
                        : y_top + s.font_size * 0.55;
        cui_color col{ (uint8_t)std::clamp(s.color.r * 255.f, 0.f, 255.f),
                       (uint8_t)std::clamp(s.color.g * 255.f, 0.f, 255.f),
                       (uint8_t)std::clamp(s.color.b * 255.f, 0.f, 255.f), 255 };
        cui_draw_line(ui, cui_vec2{ (float)x, (float)ly }, cui_vec2{ (float)(x + width), (float)ly },
                      thickness, col);
    }

    void paintNode(int64_t id) {
        Node& n = doc.nodes[id];
        if (n.kind == Node::Comment || n.style.display == Display::None) return;
        if (n.tag == "head" || !n.ever_rendered) { for (int64_t c : n.children) paintNode(c); return; }
        Style& s = n.style;
        // Draw in CSS logical points — cui_set_viewport(scale) scales them to
        // device px internally, so pre-multiplying here double-scales (the card
        // landed at 2x its CSS x). r/rad/border/font_size are all CSS units.
        cui_rect r{(float)n.x, (float)n.y, (float)n.w, (float)n.h};
#ifdef CUIW_LAYOUT_DEBUG
        if (n.tag == "div" || n.tag == "body")
            fprintf(stderr, "[paintgeo] tag=%s css_x=%g css_w=%g scale=%g\n",
                    n.tag.c_str(), n.x, n.w, scale);
#endif
        cui_corners rad = CUI_CORNERS((float)s.radius);
        if (s.bg.a > 0 && n.w > 0 && n.h > 0) {
#ifdef CUIW_LAYOUT_DEBUG
            if (n.tag == "div" || n.tag == "body")
                fprintf(stderr, "[paintrect] tag=%s cls=%s css=(%g,%g %gx%g)\n", n.tag.c_str(),
                        [&]{ std::string c; for (auto&a:n.attrs) if(a.first=="class") c=a.second; return c; }().c_str(),
                        r.x, r.y, r.w, r.h);
#endif
            cui_color bg{ (uint8_t)std::clamp(s.bg.r * 255.f, 0.f, 255.f),
                          (uint8_t)std::clamp(s.bg.g * 255.f, 0.f, 255.f),
                          (uint8_t)std::clamp(s.bg.b * 255.f, 0.f, 255.f),
                          (uint8_t)std::clamp(s.bg.a * 255.f, 0.f, 255.f) };
            if (s.border > 0)
                cui_draw_rect_border(ui, r, rad, bg,
                                     cui_color{ (uint8_t)std::clamp(s.border_color.r * 255.f, 0.f, 255.f),
                                                (uint8_t)std::clamp(s.border_color.g * 255.f, 0.f, 255.f),
                                                (uint8_t)std::clamp(s.border_color.b * 255.f, 0.f, 255.f),
                                                (uint8_t)std::clamp(s.border_color.a * 255.f, 0.f, 255.f) },
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
        // overflow: hidden|scroll|auto clips content (text + children) to the
        // padding box. Background/border above are painted unclipped, matching
        // CSS — only descendant content is cut at the rounded edge.
        const bool clips = (s.overflow == Overflow::Hidden && n.w > 0 && n.h > 0);
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
            cui_color col{ (uint8_t)std::clamp(s.color.r * 255.f, 0.f, 255.f),
                           (uint8_t)std::clamp(s.color.g * 255.f, 0.f, 255.f),
                           (uint8_t)std::clamp(s.color.b * 255.f, 0.f, 255.f), 255 };
            for (const auto& sg : n.segs)
                if (!sg.text.empty()) {
                    cui_draw_text(ui, fontFor(s), (float)s.font_size,
                                  cui_vec2{ (float)sg.x, (float)sg.y }, col, sg.text.c_str(), -1);
                    drawTextDecoration(s, sg.x, sg.y, sg.w);
                }
        }
        else if (n.kind == Node::Text && !n.lines.empty()) {
            cui_color col{ (uint8_t)std::clamp(s.color.r * 255.f, 0.f, 255.f),
                           (uint8_t)std::clamp(s.color.g * 255.f, 0.f, 255.f),
                           (uint8_t)std::clamp(s.color.b * 255.f, 0.f, 255.f), 255 };
            double ty = n.y;
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
        for (int64_t c : n.children) paintNode(c);
        if (clips) cui_pop_clip(ui);
    }

    cui_color bubbleColor() const {
        return doc.theme == "dark" ? cui_color{0x21, 0x21, 0x21, 0xFF} : cui_color{0xFF, 0xFF, 0xFF, 0xFF};
    }

    // ───────────── ops ─────────────

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
            n.svg = op["ns"].asString() == "svg";
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
            parseSheet(op["text"].asString(), doc.sheet);
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
        } else if (k == "prop") { /* scrollTop/scrollLeft: no scrolling yet */ }
        else if (k == "call") { /* scrollTo/setPointerCapture: no-ops here */ }
        else if (k == "canvas") { /* canvas 2D commands: not yet painted */ }
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
    impl_->paintNode(body);
    cui_end_frame(impl_->ui);
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
