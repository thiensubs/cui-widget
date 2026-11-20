#!/usr/bin/env python3
"""Generate a gradient-gallery widget as libcui-widget RenderOps.

A design-system style showcase that exercises every gradient power the engine
has, the way a developer would use them in a real app:

  * angle linear gradients (45deg, 120deg, to right, to top)
  * a radial gradient (glowing centre)
  * repeating linear gradients (stripes)
  * multi-stop gradients (3-4 stops with explicit positions)
  * text and cards layered on top, with rounded corners and shadows

Usage: python3 tools/gradient_gallery.py > gradient_gallery.json
Render: ./build/cuiw-render gradient_gallery.json gradient_gallery.png 380 2 dark
"""
import json
import sys

W = 380.0


class Doc:
    """Accumulates CSS rules and RenderOps with unique ids."""

    def __init__(self):
        self.ops = []
        self.css = []
        self._id = 10   # body is 2, css op is 0

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


CSS = """
body{margin:0;padding:18px;background:#0b0f14;font-size:13px;color:#e6edf5}
h1{font-size:20px;font-weight:700;color:#f5f7fc;margin:0 0 4px 0}
.sub{font-size:11px;color:#8494ab;margin:0 0 14px 0}
.sect{font-size:11px;font-weight:700;color:#7d8ea8;margin:0 0 8px 0;
  text-transform:uppercase}
.card{border-radius:16px;padding:14px;margin-bottom:14px;
  box-shadow:0 12px 28px rgba(0,0,0,0.5)}
.sw{height:88px;border-radius:12px;margin-bottom:8px}
.cap{font-size:11px;color:#a9bad1;margin:0 0 3px 0}
.code{font-size:10px;color:#6f8198;margin:0 0 12px 0}
.hero{height:120px;border-radius:16px;display:flex;align-items:flex-end;
  padding:14px;box-shadow:0 14px 30px rgba(124,58,237,0.45);margin-bottom:14px}
.herot{font-size:18px;font-weight:700;color:#ffffff}
.heroa{font-size:11px;color:#e8ddff}
.g45{background:linear-gradient(45deg,#ff4d6d,#4d79ff)}
.g120{background:linear-gradient(120deg,#f72585,#7209b7,#3a0ca3)}
.gtor{background:linear-gradient(to right,#06d6a0,#118ab2)}
.gtot{background:linear-gradient(to top,#ffbe0b,#fb5607)}
.rad{background:radial-gradient(circle,#ffdd00,#ff8800 55%,#cc0000)}
.r45{background:repeating-linear-gradient(45deg,#1b998b,#1b998b 12px,#0b6e63 12px,#0b6e63 24px)}
.rv{background:repeating-linear-gradient(to bottom,#e63946 0%,#e63946 14px,#1d3557 14px,#1d3557 28px)}
.multi{background:linear-gradient(to bottom,#240046,#7b2cbf 45%,#c77dff 80%,#f0c9ff)}
.hero-bg{background:linear-gradient(135deg,#7b2cbf,#f72585)}
"""


def build(d):
    page = 2   # body: content sits directly in the document, no scroll wrapper

    d.div("h1", page, "Gradients")
    d.div("sub", page, "Every gradient the engine can draw")

    # Hero: a 135deg two-stop with text layered on the bottom.
    hero = d.div("hero hero-bg", page)
    col = d.div("tcol", hero)
    d.div("herot", col, "Sunset Bloom")
    d.div("heroa", col, "linear-gradient 135deg")

    # Angle section.
    d.div("sect", page, "Linear · by angle")
    for cls, cap, code in (
        ("g45", "45 degrees", "linear-gradient(45deg, #ff4d6d, #4d79ff)"),
        ("g120", "120 degrees, three stops",
         "linear-gradient(120deg, #f72585, #7209b7 50%, #3a0ca3)"),
        ("gtor", "to right", "linear-gradient(to right, #06d6a0, #118ab2)"),
        ("gtot", "to top", "linear-gradient(to top, #ffbe0b, #fb5607)"),
    ):
        d.div("sw " + cls, page)
        d.div("cap", page, cap)
        d.div("code", page, code)

    # Radial section.
    d.div("sect", page, "Radial")
    d.div("sw rad", page)
    d.div("cap", page, "circle, yellow centre to red edge")
    d.div("code", page, "radial-gradient(circle, #ffdd00, #ff8800 55%, #cc0000)")

    # Repeating section.
    d.div("sect", page, "Repeating")
    d.div("sw r45", page)
    d.div("cap", page, "45deg stripes")
    d.div("code", page,
          "repeating-linear-gradient(45deg, #1b998b 12px, #0b6e63 24px)")
    d.div("sw rv", page)
    d.div("cap", page, "vertical stripes")
    d.div("code", page,
          "repeating-linear-gradient(to bottom, #e63946 14px, #1d3557 28px)")

    # Multi-stop section.
    d.div("sect", page, "Multi-stop with positions")
    d.div("sw multi", page)
    d.div("cap", page, "four stops with explicit positions")
    d.div("code", page,
          "linear-gradient(to bottom, #240046, #7b2cbf 45%, "
          "#c77dff 80%, #f0c9ff)")


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
    # cuiw-render takes the bare ops array, not the {"type":"ops"} envelope.
    json.dump(emit(d), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
