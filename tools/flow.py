#!/usr/bin/env python3
"""Generate an ANIMATED isometric pipeline diorama as libcui-widget RenderOps.

Same 3D trick as diorama.py (every cube is three shaded isometric polygons), but
the data flow is alive: a stream of glowing packets travels the source -> build
-> test -> deploy arcs. Each packet is an SVG dot whose opacity is driven by a
CSS @keyframes animation with a staggered `animation-delay`, so the bright point
sweeps along each arc in sequence — a real flowing pipeline, not abstract blocks.

The animation uses opacity only (applied at paint time), so it animates on the
engine's frame clock without a relayout.

Usage: python3 tools/flow.py > flow.json
Render a frozen mid-flow frame (now_ms picks where in the cycle):
  ./build/cuiw-render flow.json flow.png 620 2 dark 420
"""
import json
import math
import sys

S = 30.0                                   # tile edge, px
C30 = math.cos(math.radians(30))
S30 = math.sin(math.radians(30))
OX, OY = 250.0, 200.0                       # screen origin of grid (0,0,0)
VIEW_W, VIEW_H = 620.0, 380.0

BG = "#0f172a"
EDGE = "#0b1220"
INK = "#e2e8f0"
INK_DIM = "#94a3b8"

PACKET = "#7dd3fc"                          # the flowing light
PACKET_HOT = "#f0f9ff"                      # bright core


def iso(gx, gy, gz):
    x = (gx - gy) * C30 * S
    y = (gx + gy) * S30 * S - gz * S
    return (OX + x, OY + y)


def pts(points):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


class Doc:
    """Accumulates shapes back-to-front, then emits unique-id RenderOps."""

    def __init__(self):
        self.shapes = []          # (tag, points_str, fill, stroke, sw)
        self.labels = []          # (x, y, text)
        self.packets = []         # (x, y, delay_s)
        self._id = 100            # svg root is 10; never reuse an id

    def next_id(self):
        self._id += 1
        return self._id

    def box(self, x0, y0, x1, y1, z0, z1, top, left, right):
        def c(gx, gy, gz):
            return iso(gx, gy, gz)
        self.shapes.append(("polygon",
                            pts([c(x0, y0, z1), c(x1, y0, z1),
                                 c(x1, y1, z1), c(x0, y1, z1)]), top, EDGE, 0.6))
        self.shapes.append(("polygon",
                            pts([c(x0, y1, z1), c(x1, y1, z1),
                                 c(x1, y1, z0), c(x0, y1, z0)]), left, EDGE, 0.6))
        self.shapes.append(("polygon",
                            pts([c(x1, y0, z1), c(x1, y1, z1),
                                 c(x1, y1, z0), c(x1, y0, z0)]), right, EDGE, 0.6))

    def node(self, gx, gy, height, top, left, right):
        self.box(gx, gy, gx + 1, gy + 1, 1, 1 + height, top, left, right)

    def label(self, gx, gy, gz, text):
        x, y = iso(gx, gy, gz)
        self.labels.append((x, y, text))

    def arc_points(self, a, b, lift=1.1, steps=24):
        """Sample the quadratic arc a->b (screen coords), lifted over the boxes."""
        (ax, ay), (bx, by) = iso(*a), iso(*b)
        ay -= lift * S
        by -= lift * S
        mx, my = (ax + bx) / 2, (ay + by) / 2 - lift * S * 0.9
        def q(tn):
            x = (1 - tn) ** 2 * ax + 2 * (1 - tn) * tn * mx + tn ** 2 * bx
            y = (1 - tn) ** 2 * ay + 2 * (1 - tn) * tn * my + tn ** 2 * by
            return x, y
        return [q(i / steps) for i in range(steps + 1)]

    def packet_stream(self, a, b, count=9, lift=1.1):
        """Lay `count` glowing packets along the a->b arc, staggered in time."""
        samples = self.arc_points(a, b, lift)
        # even arc-length resample so packets move at constant speed
        seg = [0.0]
        for i in range(len(samples) - 1):
            seg.append(seg[-1] + math.hypot(samples[i + 1][0] - samples[i][0],
                                            samples[i + 1][1] - samples[i][1]))
        total = seg[-1]
        for k in range(count):
            target = total * k / (count - 1)
            i = max(j for j in range(len(seg)) if seg[j] <= target)
            i = min(i, len(seg) - 2)          # keep i+1 in range at the far end
            span = seg[i + 1] - seg[i] or 1.0
            f = (target - seg[i]) / span
            x = samples[i][0] + f * (samples[i + 1][0] - samples[i][0])
            y = samples[i][1] + f * (samples[i + 1][1] - samples[i][1])
            # stagger so the bright point travels a->b: later along the path,
            # later in the cycle (negative delay = already mid-cycle at t=0)
            delay = -(k / count) * CYCLE_S
            self.packets.append((x, y, delay))


STAGES = [
    (0, 3, 1.2, "#fde68a", "#f59e0b", "#d97706", "source"),
    (2, 2, 1.6, "#bfdbfe", "#60a5fa", "#2563eb", "build"),
    (4, 1, 1.4, "#a7f3d0", "#34d399", "#0ea371", "test"),
    (6, 0, 1.8, "#fecaca", "#f87171", "#dc2626", "deploy"),
]
CYCLE_S = 2.4                                # one packet traversal


def build():
    d = Doc()
    d.box(0, 0, 8, 4, 0, 1, "#1e293b", "#334155", "#475569")

    for gx, gy, h, top, left, right, label in sorted(STAGES,
                                                     key=lambda s: -(s[0] + s[1])):
        d.node(gx, gy, h, top, left, right)
        d.label(gx + 0.5, gy + 0.5, 1 + h + 0.45, label)

    # a flowing stream of packets hops across the platform top, stage to stage
    for i in range(len(STAGES) - 1):
        gx0, gy0, h0 = STAGES[i][0], STAGES[i][1], STAGES[i][2]
        gx1, gy1, h1 = STAGES[i + 1][0], STAGES[i + 1][1], STAGES[i + 1][2]
        d.packet_stream((gx0 + 1, gy0 + 1, 1 + h0 + 0.2),
                        (gx1, gy1, 1 + h1 + 0.2))

    return d


def emit(d):
    # The @keyframes that drives the flowing light. Opacity-only, applied at
    # paint, so it animates on the frame clock without a relayout.
    css = (
        "body{margin:0;padding:0;background:" + BG + "}"
        ".cap{position:absolute;left:16px;top:12px;font-size:14px;"
        "font-weight:700;color:" + INK + "}"
        ".sub{position:absolute;left:16px;top:32px;font-size:11px;"
        "color:" + INK_DIM + "}"
        "@keyframes flow{0%{opacity:0.08}45%{opacity:1}100%{opacity:0.08}}"
        ".pk{animation:flow " + f"{CYCLE_S}" + "s ease-in-out infinite}"
    )
    ops = [
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
        {"op": "css", "id": 0, "text": css},
        {"op": "text", "id": 3, "data": "libcui-widget \u00b7 @keyframes"},
        {"op": "ins", "parent": 2, "id": 3, "before": None},
        {"op": "text", "id": 4, "data":
            "isometric pipeline \u2014 animated data flow"},
        {"op": "ins", "parent": 2, "id": 4, "before": None},
        {"op": "el", "id": 10, "tag": "svg", "ns": "svg", "attrs": [
            ["viewBox", f"0 0 {VIEW_W:.0f} {VIEW_H:.0f}"],
            ["width", f"{VIEW_W:.0f}"], ["height", f"{VIEW_H:.0f}"]],
            "adopt": "body"},
        {"op": "ins", "parent": 2, "id": 10, "before": None},
    ]
    d.next_id()   # advance past every id build() handed to a shape

    ops.append({"op": "attr", "id": 3, "name": "class", "value": "cap"})
    ops.append({"op": "attr", "id": 4, "name": "class", "value": "sub"})

    # stage columns
    for tag, points, fill, stroke, sw in d.shapes:
        i = d.next_id()
        ops.append({"op": "el", "id": i, "tag": tag, "ns": "svg", "attrs": [
            ["points", points], ["fill", fill], ["stroke", stroke],
            ["stroke-width", f"{sw}"]], "adopt": "body"})
        ops.append({"op": "ins", "parent": 10, "id": i, "before": None})

    # flowing packets: a soft halo + a bright core, both pulsing on the cycle.
    # The halo is drawn first so the core sits on top.
    for x, y, delay in d.packets:
        for r, color in ((7.5, PACKET), (3.0, PACKET_HOT)):
            i = d.next_id()
            ops.append({"op": "el", "id": i, "tag": "circle", "ns": "svg", "attrs": [
                ["cx", f"{x:.1f}"], ["cy", f"{y:.1f}"], ["r", f"{r}"],
                ["fill", color], ["class", "pk"],
                ["style", f"animation-delay:{delay:.2f}s"]],
                "adopt": "body"})
            ops.append({"op": "ins", "parent": 10, "id": i, "before": None})

    # labels on top
    for x, y, text in d.labels:
        i = d.next_id()
        ops.append({"op": "el", "id": i, "tag": "text", "ns": "svg", "attrs": [
            ["x", f"{x:.1f}"], ["y", f"{y:.1f}"], ["fill", INK],
            ["font-size", "10"], ["text-anchor", "middle"]],
            "adopt": "body"})
        ops.append({"op": "ins", "parent": 10, "id": i, "before": None})
        run = d.next_id()
        ops.append({"op": "text", "id": run, "data": text, "adopt": "body"})
        ops.append({"op": "ins", "parent": i, "id": run, "before": None})

    return ops


if __name__ == "__main__":
    # cuiw-render takes the bare ops array, not the {"type":"ops"} envelope.
    json.dump(emit(build()), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
