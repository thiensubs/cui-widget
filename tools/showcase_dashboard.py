#!/usr/bin/env python3
"""Generate a developer-dashboard widget as libcui-widget RenderOps.

Re-skinned against tools/tokens.py — every spacing, font size, radius and
colour is a design token read from one :root block, so the screen has a real
hierarchy (big numbers, dim labels, a clear reading order) instead of ad-hoc
hex values.

  * a 2x2 stat grid (display:grid) with oversized values and dim labels
  * a bar chart drawn on a <canvas> via translate + scale + gradient fills
  * a scrolling event log (overflow:scroll) with severity-coloured levels
  * wrapping service tags (display:flex; flex-wrap:wrap)
  * elevation (box-shadow) and a gradient brand accent throughout

Usage: python3 tools/showcase_dashboard.py > showcase_dashboard.json
Render: ./build/cuiw-render showcase_dashboard.json docs/showcase_dashboard.png 380 2 dark
"""
import json
import sys

from tokens import tokens_css


class Doc:
    """Accumulates CSS rules and RenderOps with unique ids."""

    def __init__(self):
        self.ops = []
        self.css = []
        self._id = 10

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
body{margin:0;padding:var(--s4);background:var(--bg);color:var(--ink);
  font-size:var(--t-md)}
.h{font-size:var(--t-2xl);font-weight:700;margin:0;color:var(--ink)}
.sub{font-size:var(--t-sm);color:var(--ink-dim);margin:var(--s1) 0 var(--s4) 0}

.stats{display:grid;grid-template-columns:1fr 1fr;gap:var(--s2);
  margin-bottom:var(--s3)}
.stat{background:var(--surface);border-radius:var(--r-md);padding:var(--s3);
  box-shadow:var(--shadow-md)}
.slabel{font-size:var(--t-xs);color:var(--ink-faint);text-transform:uppercase;
  margin-bottom:var(--s1)}
.sval{font-size:var(--t-xl);font-weight:700;color:var(--ink)}
.sdelta{font-size:var(--t-xs);margin-top:var(--s1)}
.up{color:var(--good)}.down{color:var(--bad)}.flat{color:var(--warn)}

.panel{background:var(--surface);border-radius:var(--r-md);padding:var(--s3);
  margin-bottom:var(--s2);box-shadow:var(--shadow-md)}
.ptitle{font-size:var(--t-sm);font-weight:700;color:var(--ink-dim);
  margin:0 0 var(--s2) 0}
.chart{width:100%;height:92px;display:block}
.log{height:66px;overflow:scroll;font-size:var(--t-xs);color:var(--ink-dim)}
.log p{margin:0 0 var(--s1) 0}
.lvl{font-weight:700}
.up{color:var(--good)}.warn{color:var(--warn)}.bad{color:var(--bad)}

.tags{display:flex;flex-wrap:wrap;gap:var(--s2)}
.tag{font-size:var(--t-xs);padding:var(--s1) var(--s2);border-radius:var(--r-pill);
  background:var(--grad-brand);color:#fff}
"""


def build(d):
    page = 2

    d.div("h", page, "deploys this week")
    d.div("sub", page, "a dashboard built from design tokens")

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

    panel = d.div("panel", page)
    d.div("ptitle", panel, "requests / hour")
    bars = []
    heights = (30, 46, 36, 58, 51, 66, 55)
    x = 6.0
    for h in heights:
        bars.append(["save"])
        bars.append(["translate", x, 84])
        bars.append(["scale", 1, h / 100.0])
        bars.append(["fillStyle", "linear-gradient(to top,#7c6cff,#ec4899)"])
        bars.append(["fillRect", 0, -84, 18, 84])
        bars.append(["restore"])
        x += 26.0
    d.canvas("chart", panel, "chart1", bars)

    logp = d.div("panel", page)
    d.div("ptitle", logp, "event log")
    log = d.div("log", logp)
    for lvl, cls, msg in (
        ("OK", "up", "deploy 4182 finished in 92s"),
        ("WARN", "warn", "cache miss rate 4.1%"),
        ("OK", "up", "health checks green"),
        ("ERR", "bad", "pod api-7f restarted"),
        ("OK", "up", "autoscaler added 2 nodes"),
    ):
        row = d.div(None, log)
        d.div("lvl " + cls, row, lvl + " ")
        t = d.nid()
        d.ops.append({"op": "text", "id": t, "data": msg})
        d.ops.append({"op": "ins", "parent": row, "id": t, "before": None})

    tagp = d.div("panel", page)
    d.div("ptitle", tagp, "services")
    tags = d.div("tags", tagp)
    for name in ("api", "worker", "cron", "web", "cache", "queue", "search"):
        d.div("tag", tags, name)


def emit(d):
    # The css op must come first: styles are applied when a node is created, so
    # a css op after the body leaves everything unstyled.
    head = [
        {"op": "css", "id": 0, "text": tokens_css() + "".join(d.css)},
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
    ]
    return head + d.ops


if __name__ == "__main__":
    d = Doc()
    d.rule(CSS)   # wire the layout rules in — without this only tokens ship
    build(d)
    json.dump(emit(d), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
