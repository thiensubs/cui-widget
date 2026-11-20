#!/usr/bin/env python3
"""Generate an isometric "diorama" widget document as libcui-widget RenderOps.

The engine is a 2D rasterizer, so the 3D look is computed here: every cube is
three shaded polygons (top / left / right faces) projected isometrically, drawn
back-to-front. A pipeline of stages sits on the platform and flow arrows connect
them left-to-right, so the scene reads as a real data/build flow, not abstract
blocks.

Usage: python3 tools/diorama.py > diorama.json
Render: ./build/cuiw-render diorama.json diorama.png 620 1 dark
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
        self.flows = []           # (path_d, color)
        self.labels = []          # (x, y, text)
        self._id = 100            # svg root is 10; never reuse an id

    def next_id(self):
        self._id += 1
        return self._id

    def box(self, x0, y0, x1, y1, z0, z1, top, left, right):
        """An axis-aligned box from (x0,y0,z0) to (x1,y1,z1), 3 visible faces."""

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
        """A pipeline stage: a 1x1 column of the given height on the platform."""
        self.box(gx, gy, gx + 1, gy + 1, 1, 1 + height, top, left, right)

    def arrow(self, a, b, color, lift=1.1):
        """A flow arrow arcing over the platform from stage a to stage b (3D).

        Lifted above the column tops and arced so it reads as a hand-off between
        stages rather than a line cutting through the boxes."""
        pa, pb = iso(*a), iso(*b)
        (ax, ay), (bx, by) = pa, pb
        # raise both ends, then bow the midpoint upward for an arc
        ay -= lift * S
        by -= lift * S
        mx, my = (ax + bx) / 2, (ay + by) / 2 - lift * S * 0.9
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        if length < 1:
            return
        ux, uy = dx / length, dy / length
        seg, gap = 8.0, 6.0
        d = []
        t = 0.0
        # sample the quadratic arc (a -> m -> b) and lay dashes along it
        def q(tn):
            x = (1 - tn) ** 2 * ax + 2 * (1 - tn) * tn * mx + tn ** 2 * bx
            y = (1 - tn) ** 2 * ay + 2 * (1 - tn) * tn * my + tn ** 2 * by
            return x, y
        arc_len = 0.0
        steps = 24
        samples = [q(i / steps) for i in range(steps + 1)]
        for i in range(steps):
            arc_len += math.hypot(samples[i + 1][0] - samples[i][0],
                                  samples[i + 1][1] - samples[i][1])
        travelled = 0.0
        on = True
        run_start = samples[0]
        for i in range(steps):
            seg_len = math.hypot(samples[i + 1][0] - samples[i][0],
                                 samples[i + 1][1] - samples[i][1])
            travelled += seg_len
            if on and travelled >= seg:
                d.append(f"M{run_start[0]:.1f},{run_start[1]:.1f} "
                         f"L{samples[i + 1][0]:.1f},{samples[i + 1][1]:.1f}")
                on = False
                travelled = 0.0
            elif not on and travelled >= gap:
                on = True
                run_start = samples[i + 1]
                travelled = 0.0
        if on:
            d.append(f"M{run_start[0]:.1f},{run_start[1]:.1f} "
                     f"L{bx:.1f},{by:.1f}")
        # arrowhead at the tip, oriented along the end tangent
        tx, ty = bx - samples[-2][0], by - samples[-2][1]
        tl = math.hypot(tx, ty) or 1.0
        tx, ty = tx / tl, ty / tl
        hx, hy = 8.0, 4.5
        d.append(f"M{bx:.1f},{by:.1f} "
                 f"L{bx - tx * 10 - ty * hy:.1f},{by - ty * 10 + tx * hy:.1f}")
        d.append(f"M{bx:.1f},{by:.1f} "
                 f"L{bx - tx * 10 + ty * hy:.1f},{by - ty * 10 - tx * hy:.1f}")
        self.flows.append((" ".join(d), color))

    def label(self, gx, gy, gz, text):
        x, y = iso(gx, gy, gz)
        self.labels.append((x, y, text))


STAGES = [
    # (gx, gy, height, top, left, right, label)
    (0, 3, 1.2, "#fde68a", "#f59e0b", "#d97706", "source"),
    (2, 2, 1.6, "#bfdbfe", "#60a5fa", "#2563eb", "build"),
    (4, 1, 1.4, "#a7f3d0", "#34d399", "#0ea371", "test"),
    (6, 0, 1.8, "#fecaca", "#f87171", "#dc2626", "deploy"),
]
FLOW = "#f8fafc"


def build():
    d = Doc()
    # ground platform the whole scene sits on (gz 0..1)
    d.box(0, 0, 8, 4, 0, 1, "#1e293b", "#334155", "#475569")

    # stages, drawn far-to-near so nearer columns overlap the farther ones
    for gx, gy, h, top, left, right, label in sorted(STAGES,
                                                     key=lambda s: -(s[0] + s[1])):
        d.node(gx, gy, h, top, left, right)
        d.label(gx + 0.5, gy + 0.5, 1 + h + 0.45, label)

    # flow arrows hop across the platform top from one stage to the next
    for i in range(len(STAGES) - 1):
        gx0, gy0, h0 = STAGES[i][0], STAGES[i][1], STAGES[i][2]
        gx1, gy1, h1 = STAGES[i + 1][0], STAGES[i + 1][1], STAGES[i + 1][2]
        d.arrow((gx0 + 1, gy0 + 1, 1 + h0 + 0.2),
                (gx1, gy1, 1 + h1 + 0.2), FLOW)

    return d


def emit(d):
    ops = [
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
        {"op": "css", "id": 0, "text":
            "body{margin:0;padding:0;background:" + BG + "}"
            ".cap{position:absolute;left:16px;top:12px;font-size:14px;"
            "font-weight:700;color:" + INK + "}"
            ".sub{position:absolute;left:16px;top:32px;font-size:11px;"
            "color:" + INK_DIM + "}"},
        {"op": "text", "id": 3, "data": "libcui-widget \u00b7 inline SVG"},
        {"op": "ins", "parent": 2, "id": 3, "before": None},
        {"op": "text", "id": 4, "data":
            "isometric pipeline \u2014 every shape is a 2D vector"},
        {"op": "ins", "parent": 2, "id": 4, "before": None},
        {"op": "el", "id": 10, "tag": "svg", "ns": "svg", "attrs": [
            ["viewBox", f"0 0 {VIEW_W:.0f} {VIEW_H:.0f}"],
            ["width", f"{VIEW_W:.0f}"], ["height", f"{VIEW_H:.0f}"]],
            "adopt": "body"},
        {"op": "ins", "parent": 2, "id": 10, "before": None},
    ]
    # Advance past every id build() already handed to a shape, so emit's own
    # element ids never collide with them (a collision silently overwrites the
    # earlier node and the whole scene fails to paint).
    d.next_id()

    # class the two caption texts (ids 3 and 4)
    ops.append({"op": "attr", "id": 3, "name": "class", "value": "cap"})
    ops.append({"op": "attr", "id": 4, "name": "class", "value": "sub"})

    # flow arrows first (they sit on the platform, under the stage columns)
    for path_d, color in d.flows:
        i = d.next_id()
        ops.append({"op": "el", "id": i, "tag": "path", "ns": "svg", "attrs": [
            ["d", path_d], ["fill", "none"], ["stroke", color],
            ["stroke-width", "1.6"], ["stroke-linecap", "round"]],
            "adopt": "body"})
        ops.append({"op": "ins", "parent": 10, "id": i, "before": None})

    # stage columns
    for tag, points, fill, stroke, sw in d.shapes:
        i = d.next_id()
        ops.append({"op": "el", "id": i, "tag": tag, "ns": "svg", "attrs": [
            ["points", points], ["fill", fill], ["stroke", stroke],
            ["stroke-width", f"{sw}"]], "adopt": "body"})
        ops.append({"op": "ins", "parent": 10, "id": i, "before": None})

    # labels: a positioned <text> element with a nested text run
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
    # cuiw-render takes the bare ops array (the {"type":"ops"} envelope is the
    # C-ABI cuiw_send form, not the file form).
    json.dump(emit(build()), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")