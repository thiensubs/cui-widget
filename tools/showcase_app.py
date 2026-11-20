#!/usr/bin/env python3
"""Generate a realistic music-player widget as libcui-widget RenderOps.

A deliberately complex, real-world screen that exercises many engine features
at once the way an actual app would:
  * a card with rounded corners, a gradient background and a box-shadow
  * an <img> album cover (a data: URL) with rounded corners
  * a gradient progress bar
  * SVG controls (prev / play / next) drawn as vectors
  * a scrolling playlist (overflow: scroll) with a highlighted current track

Text is always wrapped in a block element (<div>) with a class — the cascade
matches class selectors on elements, not bare text nodes.

Usage: python3 tools/showcase_app.py > showcase_app.json
Render: ./build/cuiw-render showcase_app.json docs/showcase_app.png 380 2 dark
"""
import base64
import json
import sys


def img_data_url(path="/tmp/art.png"):
    try:
        with open(path, "rb") as f:
            return "data:image/png;base64," + base64.b64encode(f.read()).decode()
    except OSError:
        return ""


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
        """A block element with a class; optional wrapped text child."""
        i = self.el("div", [["class", cls]], parent)
        if text is not None:
            t = self.nid()
            self.ops.append({"op": "text", "id": t, "data": text})
            self.ops.append({"op": "ins", "parent": i, "id": t, "before": None})
        return i


CSS = """
body{margin:0;padding:18px;background:#0b0f14;font-size:14px;color:#e6edf5}
.card{border-radius:20px;overflow:scroll;height:620px;
  background:linear-gradient(to bottom,#2f4066 0%,#1a2338 50%,#0e1420 100%);
  box-shadow:0 20px 44px rgba(0,0,0,0.6)}
.hero{display:flex;gap:16px;padding:18px;align-items:center}
.cover{width:104px;height:104px;border-radius:14px;flex:none;
  box-shadow:0 10px 22px rgba(124,58,237,0.5)}
.mcol{display:flex;flex-direction:column;gap:7px}
.title{font-size:19px;font-weight:700;color:#f5f7fc}
.artist{font-size:13px;color:#a9bad1}
.album{font-size:11px;color:#74869f}
.progress{margin:2px 18px 0;height:6px;border-radius:3px;background:#2a3450;
  overflow:hidden}
.pfill{height:6px;width:62%;border-radius:3px;
  background:linear-gradient(to bottom,#a855f7,#ec4899)}
.times{display:flex;justify-content:space-between;padding:7px 18px 0;
  font-size:10px;color:#74869f}
.tl{font-size:10px;color:#74869f}
.controls{display:flex;gap:24px;padding:16px 18px 10px;align-items:center;
  justify-content:center}
.cbtn{width:44px;height:44px;text-align:center;font-size:22px;
  line-height:44px;color:#a9bad1}
.play{width:56px;height:56px;border-radius:28px;
  background:linear-gradient(to bottom,#a855f7,#ec4899);
  box-shadow:0 10px 22px rgba(236,72,153,0.45);text-align:center;
  font-size:22px;line-height:56px;color:#ffffff}
.plhead{padding:12px 18px 4px;font-size:11px;font-weight:700;color:#74869f}
.row{display:flex;gap:12px;padding:10px 18px;align-items:center}
.rowon{display:flex;gap:12px;padding:10px 18px;align-items:center;
  background:#1b2540;border-radius:10px}
.num{width:16px;font-size:11px;color:#5d6e85;text-align:right}
.tcol{display:flex;flex-direction:column;gap:3px}
.t{font-size:13px;color:#e2e9f4}
.a{font-size:11px;color:#7d8ea8}
.dur{font-size:11px;color:#5d6e85;margin-left:auto}
"""


def build(d):
    body = 2
    card = d.div("card", body)

    # Hero: cover art + title / artist / album.
    hero = d.div("hero", card)
    art = img_data_url()
    if art:
        d.el("img", [["src", art], ["class", "cover"]], hero)
    meta = d.div("mcol", hero)
    d.div("title", meta, "Neon Skyline")
    d.div("artist", meta, "Aurora Vale")
    d.div("album", meta, "Album · Midnight Circuits")

    # Progress bar (gradient fill) + elapsed / total.
    prog = d.div("progress", card)
    d.div("pfill", prog)
    times = d.div("times", card)
    d.div("tl", times, "1:24")
    d.div("tl", times, "3:41")

    # Controls: prev / play / next. Play is a gradient circle (reads as the play
    # button between the skip controls); prev/next are plain ASCII.
    ctl = d.div("controls", card)
    d.div("cbtn", ctl, "<")
    d.div("play", ctl)
    d.div("cbtn", ctl, ">")

    # Playlist header + rows (the card scrolls to reveal them).
    d.div("plhead", card, "UP NEXT")
    tracks = [
        ("1", "Neon Skyline", "Aurora Vale", "3:41", True),
        ("2", "Glass Harbour", "Aurora Vale", "4:02", False),
        ("3", "Paper Satellites", "Ivy Lumen", "3:18", False),
        ("4", "Slow Orbit", "Ivy Lumen", "5:07", False),
        ("5", "Cobalt Rain", "The Halcyon", "3:55", False),
        ("6", "Afterglow Drive", "The Halcyon", "4:24", False),
        ("7", "Static Bloom", "Nova Pine", "2:58", False),
        ("8", "Velvet Circuit", "Nova Pine", "4:11", False),
    ]
    for num, t, a, dur, on in tracks:
        row = d.div("rowon" if on else "row", card)
        d.div("num", row, num)
        col = d.div("tcol", row)
        d.div("t", col, t)
        d.div("a", col, a)
        d.div("dur", row, dur)


def emit(d):
    head = [
        {"op": "el", "id": 2, "tag": "body", "attrs": [], "adopt": "body"},
        {"op": "css", "id": 0, "text": CSS},
    ]
    return head + d.ops


if __name__ == "__main__":
    d = Doc()
    build(d)
    # cuiw-render takes the bare ops array, not the {"type":"ops"} envelope.
    json.dump(emit(d), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
