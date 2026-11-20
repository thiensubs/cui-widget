#!/usr/bin/env python3
"""Generate a developer-dashboard widget as libcui-widget RenderOps.

Every element on this screen is something the raw CUI brush cannot do by
itself — the point of the showcase is to make the "brain" visible:

  * a grid of stat cards (display:grid) — layout the brush has no idea about
  * flex rows that wrap and space themselves (display:flex, justify-content)
  * gradient text (background-clip) and gradient fills
  * a soft box-shadow behind each card
  * a bar chart drawn on a <canvas>, bars rotated/scaled via transforms
  * a log panel that scrolls (overflow:scroll)
  * CSS variables driving the whole colour theme from one :root block

Usage: python3 tools/showcase_dashboard.py > showcase_dashboard.json
Render: ./build/cuiw-render showcase_dashboard.json docs/showcase_dashboard.png 380 2 dark
"""
import json
import sys


class Doc:
    """Accumulates CSS rules and RenderOps with unique ids."""

    def __init__(self):
        self.ops = []
        self.css = []
        self._id = 10   # body is 2, the css op is 0; elements start at 11

    def nid(self):
        self._id += 1
        return self._id

    def rule(self, text):
        self.css.append(text)

    def el(self, tag, attrs, parent):
        i = self.nid()
        self.ops.append({"op": "el", "id": i, "tag": tag,
                         "attrs": attrs, "adopt": "body"})
        self.ops.append({"op": "ins", "parent": parent, "id": i, "before": None})
        return i

    def div(self, cls, parent, text=None):
        i = self.el("div", [["class", cls]], parent)
        if text is not None:
            t = self.nid()
            self.ops.append({"op": "text", "id": t, "data": text})
            self.ops.append({"op": "ins", "parent": i, "id": t, "before": None})
        return i

    def canvas(self, cls, parent, cid, cmds):
        c = self.el("canvas", [["class", cls], ["id", cid]], parent)
        self.ops.append({"op": "canvas", "id": c, "cmds": cmds})
        return c


CSS = """
:root{--bg:#0b0f14;--card:#151c28;--ink:#e6edf5;--dim:#8494ab;
  --accent:#7c3aed;--good:#22c55e;--warn:#f59e0b;--bad:#ef4444}
body{margin:0;padding:14px;background:var(--bg);font-size:13px;color:var(--ink);
  font-family:sans-serif}
h1{font-size:19px;font-weight:700;margin:0 0 2px 0;color:#c4b5fd}
.sub{font-size:11px;color:var(--dim);margin:0 0 10px 0}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}
.stat{background:var(--card);border-radius:12px;padding:9px 11px;
  box-shadow:0 8px 20px rgba(0,0,0,0.45)}
.slabel{font-size:10px;color:var(--dim);text-transform:uppercase;margin-bottom:3px}
.sval{font-size:20px;font-weight:700}
.sdelta{font-size:10px;margin-top:1px}
.up{color:var(--good)}.down{color:var(--bad)}.flat{color:var(--warn)}
.panel{background:var(--card);border-radius:12px;padding:10px 12px;margin-bottom:8px;
  box-shadow:0 8px 20px rgba(0,0,0,0.45)}
.ptitle{font-size:12px;font-weight:700;margin:0 0 6px 0}
.chart{width:100%;height:96px;display:block}
.log{height:64px;overflow:scroll;font-size:10px;color:var(--dim)}
.log p{margin:0 0 4px 0}
.lvl{font-weight:700}
.tags{display:flex;flex-wrap:wrap;gap:6px}
.tag{font-size:10px;padding:3px 8px;border-radius:999px;
  background:linear-gradient(90deg,rgba(124,58,237,0.35),rgba(236,72,153,0.35));
  color:var(--ink)}
"""


def build(d):
    page = 2   # body

    d.div("h1", page, "deploys this week")
    d.div("sub", page, "a dashboard the raw brush cannot lay out")

    # Stat grid: 4 cards in a 2x2 grid — pure layout the brush has no idea about.
    grid = d.div("stats", page)
    for label, val, delta, cls in (
        ("deploys", "42", "+12%", "up"),
        ("p95 latency", "218ms", "-8%", "up"),
        ("error rate", "0.42%", "+0.1%", "down"),
        ("uptime", "99.98%", "steady", "flat"),
    ):
        card = d.div("stat", grid)
        d.div("slabel", card, label)
        d.div("sval", card, val)
        d.div("sdelta " + cls, card, delta)

    # Bar chart on a canvas: bars drawn with rotate/scale transforms.
    panel = d.div("panel", page)
    d.div("ptitle", panel, "requests / hour")
    bars = []
    heights = (30, 46, 36, 58, 51, 66, 55)
    x = 6.0
    for h in heights:
        # translate to the bar's base, scale its height, fill upward.
        bars.append(["save"])
        bars.append(["translate", x, 88])
        bars.append(["scale", 1, h / 100.0])
        bars.append(["fillStyle", "linear-gradient(to top,#7c3aed,#ec4899)"])
        bars.append(["fillRect", 0, -88, 18, 88])
        bars.append(["restore"])
        x += 26.0
    d.canvas("chart", panel, "chart1", bars)

    # Scrolling log: overflow:scroll, content taller than the box.
    logp = d.div("panel", page)
    d.div("ptitle", logp, "event log")
    log = d.div("log", logp)
    for lvl, msg in (
        ("OK", "deploy 4182 finished in 92s"),
        ("WARN", "cache miss rate 4.1%"),
        ("OK", "health checks green"),
        ("ERR", "pod api-7f restarted"),
        ("OK", "autoscaler added 2 nodes"),
    ):
        row = d.div(None, log)
        d.div("lvl " + ("down" if lvl == "ERR" else
                        "flat" if lvl == "WARN" else "up"), row, lvl + " ")
        t = d.nid()
        d.ops.append({"op": "text", "id": t, "data": msg})
        d.ops.append({"op": "ins", "parent": row, "id": t, "before": None})

    # Flex-wrap tags: they wrap on their own — no hand-counted positions.
    tagp = d.div("panel", page)
    d.div("ptitle", tagp, "services")
    tags = d.div("tags", tagp)
    for name in ("api", "worker", "cron", "web", "cache", "queue", "search"):
        d.div("tag", tags, name)


def emit(d):
    head = [
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
        {"op": "css", "id": 0, "text": "".join(d.css)},
    ]
    return head + d.ops


if __name__ == "__main__":
    d = Doc()
    d.rule(CSS)
    build(d)
    json.dump(emit(d), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
