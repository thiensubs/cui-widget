#!/usr/bin/env python3
"""Generate a branching DECISION-flow diagram as libcui-widget RenderOps.

Not a linear pipeline — a real flowchart with decision nodes and yes/no
branches, the kind you'd sketch in a design tool:

    deploy -> tests pass? --no--> roll back
                 | yes
                 v
           prod? --no--> stage
              | yes
              v
            canary 10% -> promote

Each box is a styled card; each decision is an inline-SVG diamond; each arrow
is an SVG line + a label. Because it is DATA (CSS + shapes), adding a node or
re-routing an arrow is a one-line change — that is the whole point, and the
README counts how much code the same diagram costs in raw CUI.

Usage: python3 tools/showcase_flow.py > showcase_flow.json
Render: ./build/cuiw-render showcase_flow.json docs/showcase_flow.png 360 2 dark
"""
import json
import sys

W = 360.0


class Doc:
    def __init__(self):
        self.ops = []
        self.css = []
        self._id = 10
        self.svg = None

    def nid(self):
        self._id += 1
        return self._id

    def rule(self, text):
        self.css.append(text)

    def el(self, tag, attrs, parent, ns=None):
        i = self.nid()
        a = [["class", cls] if False else a for a in attrs]
        op = {"op": "el", "id": i, "tag": tag, "attrs": a, "adopt": "body"}
        if ns:
            op["ns"] = ns
        self.ops.append(op)
        self.ops.append({"op": "ins", "parent": parent, "id": i, "before": None})
        return i

    def div(self, cls, parent, text=None):
        i = self.el("div", [["class", cls]], parent)
        if text is not None:
            t = self.nid()
            self.ops.append({"op": "text", "id": t, "data": text})
            self.ops.append({"op": "ins", "parent": i, "id": t, "before": None})
        return i

    def svg_open(self, w, h):
        self.svg = self.el("svg", [["viewBox", f"0 0 {w} {h}"],
                                   ["width", f"{w}"], ["height", f"{h}"]],
                           2, ns="svg")

    def shape(self, tag, attrs):
        return self.el(tag, attrs, self.svg, ns="svg")

    def label(self, x, y, text, fill="#e6edf5", size=11, anchor="middle",
              bold=False):
        i = self.shape("text", [["x", f"{x:.0f}"], ["y", f"{y:.0f}"],
                                ["fill", fill], ["font-size", str(size)],
                                ["text-anchor", anchor]] +
                       ([["font-weight", "700"]] if bold else []))
        t = self.nid()
        self.ops.append({"op": "text", "id": t, "data": text})
        self.ops.append({"op": "ins", "parent": i, "id": t, "before": None})
        return i


CSS = """
body{margin:0;padding:14px;background:#0b0f14}
.h{font-size:15px;font-weight:700;color:#e6edf5;margin:0 0 2px 0}
.hs{font-size:11px;color:#8494ab;margin:0 0 8px 0}
"""


def build():
    d = Doc()
    d.rule(CSS)
    # body + heading
    d.div("h", 2, "deploy flow")
    d.div("hs", 2, "branching decisions, drawn as data")

    d.svg_open(W, 330)

    BOX = "#1e293b"; EDGE = "#475569"; INK = "#e6edf5"; DIM = "#8494ab"
    GOOD = "#34d399"; BAD = "#f87171"; WARN = "#fbbf24"; DEC = "#312e81"

    def box(cx, cy, w, h, title, sub, edge=EDGE):
        d.shape("rect", [["x", f"{cx - w/2:.0f}"], ["y", f"{cy - h/2:.0f}"],
                         ["width", f"{w}"], ["height", f"{h}"],
                         ["rx", "9"], ["fill", BOX], ["stroke", edge],
                         ["stroke-width", "1.5"]])
        d.label(cx, cy - 2, title, INK, 11, bold=True)
        d.label(cx, cy + 13, sub, DIM, 9)

    def diamond(cx, cy, w, h, title):
        pts = f"{cx},{cy - h/2} {cx + w/2},{cy} {cx},{cy + h/2} {cx - w/2},{cy}"
        d.shape("polygon", [["points", pts], ["fill", DEC],
                            ["stroke", "#818cf8"], ["stroke-width", "1.5"]])
        d.label(cx, cy + 4, title, "#c7d2fe", 10, bold=True)

    def arrow(x0, y0, x1, y1):
        d.shape("line", [["x1", f"{x0:.0f}"], ["y1", f"{y0:.0f}"],
                         ["x2", f"{x1:.0f}"], ["y2", f"{y1:.0f}"],
                         ["stroke", "#475569"], ["stroke-width", "1.5"]])

    def ylabel(x, y, text, fill):
        d.label(x, y, text, fill, 9)

    # main column centre x, rows top->bottom
    cx = 120
    box(cx, 34, 120, 40, "deploy", "build #4182")
    arrow(cx, 54, cx, 74)
    diamond(cx, 96, 104, 46, "tests pass?")
    # no -> roll back (right branch)
    arrow(cx + 52, 96, 250, 96)
    box(300, 96, 92, 40, "roll back", "revert #4181", edge=BAD)
    ylabel(cx + 58, 90, "no", BAD)
    # yes -> down to prod?
    arrow(cx, 119, cx, 140)
    ylabel(cx + 8, 133, "yes", GOOD)
    diamond(cx, 162, 104, 46, "prod?")
    # no -> stage (right branch)
    arrow(cx + 52, 162, 250, 162)
    box(300, 162, 92, 40, "stage", "internal", edge=WARN)
    ylabel(cx + 58, 156, "no", WARN)
    # yes -> down to canary
    arrow(cx, 185, cx, 206)
    ylabel(cx + 8, 199, "yes", GOOD)
    box(cx, 228, 120, 40, "canary 10%", "watch 30m")
    arrow(cx, 248, cx, 268)
    box(cx, 288, 120, 40, "promote", "100% rollout", edge=GOOD)

    return d


def emit(d):
    head = [
        {"op": "css", "id": 0, "text": "".join(d.css)},
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
    ]
    return head + d.ops


if __name__ == "__main__":
    json.dump(emit(build()), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
