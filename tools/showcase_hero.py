#!/usr/bin/env python3
"""Generate a premium analytics hero widget as libcui-widget RenderOps.

This is the "chase gold" showcase — it pushes the engine's ceiling for visual
craft, all built from tools/tokens.py design tokens:

  * a gradient HERO card with a big metric, a delta pill, and a sparkline
  * three KPI cards, each with a real inline-SVG icon (vector paths, not text)
  * a smooth area chart on a <canvas> (bezier curve + gradient fill under it)
  * a live activity feed with severity dots
  * staggered entry animation (@keyframes + per-card animation-delay)

Every icon is a hand-written SVG path so it is crisp at any DPR and recolours
from a token. No bitmap assets.

Usage: python3 tools/showcase_hero.py > showcase_hero.json
Render: ./build/cuiw-render showcase_hero.json docs/showcase_hero.png 400 2 dark
"""
import json
import sys

from tokens import tokens_css


class Doc:
    def __init__(self):
        self.ops = []
        self.css = []
        self._id = 10

    def nid(self):
        self._id += 1
        return self._id

    def rule(self, text):
        self.css.append(text)

    def el(self, tag, attrs, parent, ns=None):
        i = self.nid()
        op = {"op": "el", "id": i, "tag": tag, "attrs": attrs, "adopt": "body"}
        if ns:
            op["ns"] = ns
        self.ops.append(op)
        self.ops.append({"op": "ins", "parent": parent, "id": i, "before": None})
        return i

    def div(self, cls, parent, text=None, style=None):
        a = [["class", cls]]
        if style:
            a.append(["style", style])
        i = self.el("div", a, parent)
        if text is not None:
            t = self.nid()
            self.ops.append({"op": "text", "id": t, "data": text})
            self.ops.append({"op": "ins", "parent": i, "id": t, "before": None})
        return i

    def canvas(self, cls, parent, cid, cmds):
        c = self.el("canvas", [["class", cls], ["id", cid]], parent)
        self.ops.append({"op": "canvas", "id": c, "cmds": cmds})
        return c

    def svg(self, cls, parent, w, h, vb, inner):
        """An <svg> whose children are pre-built ops (added by caller)."""
        return self.el("svg", [["class", cls], ["viewBox", f"0 0 {vb} {vb}"],
                               ["width", str(w)], ["height", str(h)]], parent, ns="svg")

    def icon(self, parent, path_d, color, size=22, vb=24, sw=2.0):
        """A single-path stroke icon (Feather-style). fill/stroke must ride in a
        style ATTRIBUTE: collectSvgShapes paints each shape from its
        cascade-resolved style, and presentation attributes (stroke="…") are not
        wired into that cascade — only inline style is."""
        s = self.el("svg", [["width", str(size)], ["height", str(size)],
                            ["viewBox", f"0 0 {vb} {vb}"]], parent, ns="svg")
        self.el("path", [["d", path_d],
                         ["style", "fill:none;stroke:%s;stroke-width:%s;"
                                   "stroke-linecap:round;stroke-linejoin:round"
                                   % (color, sw)]], s)
        return s


# Feather-style 24x24 icon paths.
ICONS = {
    "trend": "M23 6l-9.5 9.5-5-5L1 18",
    "trend_down": "M23 18l-9.5-9.5-5 5L1 6",
    "users": "M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2M9 11a4 4 0 1 0 0-8 "
             "4 4 0 0 0 0 8M23 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75",
    "alert": "M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3"
             "L13.71 3.86a2 2 0 0 0-3.42 0zM12 9v4M12 17h.01",
    "dollar": "M12 1v22M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6",
    "zap": "M13 2L3 14h9l-1 8 10-12h-9l1-8z",
}

CSS = """
body{margin:0;padding:var(--s4);background:var(--bg);color:var(--ink);
  font-size:var(--t-md)}
@keyframes rise{0%{opacity:0}100%{opacity:1}}

.hero{background:linear-gradient(135deg,#7c6cff,#ec4899);border-radius:var(--r-lg);padding:var(--s4);
  box-shadow:var(--shadow-lg);margin-bottom:var(--s3);animation:rise .5s ease-out}
.hero-top{display:flex;justify-content:space-between;align-items:flex-start}
.hero-label{font-size:var(--t-sm);color:rgba(255,255,255,0.82)}
.hero-value{font-size:38px;font-weight:700;color:#fff;margin:var(--s1) 0 0 0}
.pill{display:flex;align-items:center;gap:4px;background:rgba(255,255,255,0.2);
  border-radius:var(--r-pill);padding:var(--s1) var(--s2);
  font-size:var(--t-xs);font-weight:700;color:#fff}
.spark{width:100%;height:56px;display:block;margin-top:var(--s2)}

.kpis{display:grid;grid-template-columns:1fr 1fr 1fr;gap:var(--s2);
  margin-bottom:var(--s3)}
.kpi{background:var(--surface);border-radius:var(--r-md);padding:var(--s3);
  box-shadow:var(--shadow-sm)}
.kico{width:30px;height:30px;border-radius:var(--r-sm);display:flex;
  align-items:center;justify-content:center;margin-bottom:var(--s2)}
.klabel{font-size:var(--t-xs);color:var(--ink-faint);margin-bottom:var(--s1)}
.kval{font-size:var(--t-lg);font-weight:700;color:var(--ink)}
.kdelta{font-size:var(--t-xs);margin-top:var(--s1);
  display:flex;align-items:center;gap:3px}

.panel{background:var(--surface);border-radius:var(--r-md);padding:var(--s3);
  box-shadow:var(--shadow-sm);animation:rise .5s ease-out}
.ptitle{font-size:var(--t-sm);font-weight:700;color:var(--ink-dim);
  margin:0 0 var(--s2) 0}
.area{width:100%;height:110px;display:block}
.feed{margin:0;padding:0}
.frow{display:flex;gap:var(--s2);align-items:flex-start;
  padding:var(--s2) 0;border-bottom:1px solid var(--border)}
.frow:last-child{border-bottom:none}
.fdot{width:8px;height:8px;border-radius:var(--r-pill);flex:none;margin-top:4px}
.fmsg{font-size:var(--t-sm);color:var(--ink)}
.ftime{font-size:var(--t-xs);color:var(--ink-faint);margin-left:auto;flex:none}
"""


def build(d):
    page = 2

    # ---- HERO: gradient card, big metric, delta pill, sparkline ----
    hero = d.div("hero", page)
    top = d.div("hero-top", hero)
    d.div("hero-label", top, "Total revenue")
    # delta pill with an up-trend icon
    pill = d.el("div", [["class", "pill"]], top)
    d.icon(pill, ICONS["trend"], "#ffffff", size=12, sw=2.5)
    t = d.nid()
    d.ops.append({"op": "text", "id": t, "data": "12.4%"})
    d.ops.append({"op": "ins", "parent": pill, "id": t, "before": None})
    d.div("hero-value", hero, "$48,210")
    # sparkline: a smooth-ish polyline across the hero
    pts = "0,44 26,40 52,46 78,30 104,34 130,20 156,26 182,12"
    sp = d.el("svg", [["class", "spark"], ["viewBox", "0 0 182 56"],
                      ["width", "100%"], ["height", "56"],
                      ["preserveAspectRatio", "none"]], hero, ns="svg")
    # soft gradient fill under the line
    d.el("polygon", [["points", f"{pts} 182,56 0,56"],
                     ["fill", "rgba(255,255,255,0.16)"]], sp)
    d.el("polyline", [["points", pts], ["fill", "none"],
                      ["stroke", "#ffffff"], ["stroke-width", "2.5"],
                      ["stroke-linecap", "round"],
                      ["stroke-linejoin", "round"]], sp)

    # ---- KPI cards, each with a vector icon on a tinted tile ----
    kpis = d.div("kpis", page)
    for i, (icd, tint, icol, label, val, delta, dicd, dcls) in enumerate((
        ("dollar", "rgba(124,108,255,0.18)", "#a78bfa", "Revenue", "$12.4k",
         "+8.2%", "trend", "up"),
        ("users", "rgba(52,211,153,0.18)", "#34d399", "Users", "3,842",
         "+3.1%", "trend", "up"),
        ("alert", "rgba(248,113,113,0.18)", "#f87171", "Errors", "0.42%",
         "+0.1%", "trend_down", "down"),
    )):
        delay = -(i * 0.12)
        card = d.div("kpi", kpis, style=f"animation:rise .5s ease-out "
                                         f"both;animation-delay:{delay}s")
        tile = d.div("kico", card, style=f"background:{tint}")
        d.icon(tile, ICONS[icd], icol, size=22, sw=2.0)
        d.div("klabel", card, label)
        d.div("kval", card, val)
        kr = d.div("kdelta " + dcls, card)
        d.icon(kr, ICONS[dicd], ("var(--good)" if dcls == "up" else
                                 "var(--bad)"), size=11, sw=2.5)
        t = d.nid()
        d.ops.append({"op": "text", "id": t, "data": delta})
        d.ops.append({"op": "ins", "parent": kr, "id": t, "before": None})

    # ---- AREA CHART on canvas: bezier-smoothed line + gradient fill ----
    panel = d.div("panel", page)
    d.div("ptitle", panel, "Revenue over time")
    series = (18, 26, 22, 34, 30, 44, 40, 56, 50, 64)
    w, h = 340.0, 100.0
    n = len(series)
    step = w / (n - 1)

    def at(i):
        return (i * step, h - series[i])

    # Flatten a quadratic bezier through midpoints into lineTo points (the
    # canvas command set has no quadraticCurveTo, which would be skipped).
    def smooth():
        pts_ = [at(0)]
        for i in range(1, n):
            px, py = at(i - 1)
            qx, qy = at(i)
            mx, my = (px + qx) / 2, (py + qy) / 2
            steps = 6
            for s in range(1, steps + 1):
                tt = s / steps
                xx = (1 - tt) ** 2 * px + 2 * (1 - tt) * tt * mx + tt * tt * qx
                yy = (1 - tt) ** 2 * py + 2 * (1 - tt) * tt * my + tt * tt * qy
                pts_.append((xx, yy))
        return pts_

    line_pts = smooth()
    cmds = []
    # gradient fill under the curve
    cmds.append(["beginPath"])
    cmds.append(["moveTo", line_pts[0][0], line_pts[0][1]])
    for xx, yy in line_pts[1:]:
        cmds.append(["lineTo", xx, yy])
    cmds.append(["lineTo", w, h])
    cmds.append(["lineTo", 0, h])
    cmds.append(["closePath"])
    cmds.append(["fillStyle", "rgba(124,108,255,0.28)"])
    cmds.append(["fill"])
    # the line itself
    cmds.append(["beginPath"])
    cmds.append(["moveTo", line_pts[0][0], line_pts[0][1]])
    for xx, yy in line_pts[1:]:
        cmds.append(["lineTo", xx, yy])
    cmds.append(["strokeStyle", "#a78bfa"])
    cmds.append(["lineWidth", 2.5])
    cmds.append(["stroke"])
    d.canvas("area", panel, "area1", cmds)

    # ---- ACTIVITY FEED with severity dots ----
    feedp = d.div("panel", page, style="animation:rise .5s ease-out .2s both")
    d.div("ptitle", feedp, "Live activity")
    feed = d.div("feed", feedp)
    for color, msg, tm in (
        ("var(--good)", "Payment of $1,240 received", "2m"),
        ("var(--brand)", "New customer signed up", "9m"),
        ("var(--warn)", "Checkout latency above target", "18m"),
        ("var(--good)", "Deploy 4182 promoted to prod", "34m"),
    ):
        row = d.div("frow", feed)
        d.el("div", [["class", "fdot"], ["style", f"background:{color}"]], row)
        d.div("fmsg", row, msg)
        d.div("ftime", row, tm)


def emit(d):
    head = [
        {"op": "css", "id": 0, "text": tokens_css() + "".join(d.css)},
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
    ]
    return head + d.ops


if __name__ == "__main__":
    d = Doc()
    d.rule(CSS)
    build(d)
    json.dump(emit(d), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
