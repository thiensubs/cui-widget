# libcui-widget

A standalone C library that renders a **widget document** into pixels, painting
through CUI's software rasterizer. It is the "extra handle" on top of CUI: CUI
supplies the drawing primitives (rects, borders, text, the rasterizer); libcui-widget
supplies the widget layer above it — a DOM tree, CSS cascade and layout,
hit-testing and pointer events.

It speaks the **same RenderOps document contract** the Telegram widget host
speaks. An application that already drives CUI can embed this library and render
widgets itself, with **no ceiling but the host's own resources**.

```
┌─────────────────────────────────────────────────────────┐
│  your application (trusted)                             │
│    ├─ owns the RenderOps document                       │
│    ├─ calls cuiw_send(ops json) / cuiw_touch / cuiw_paint│
│    └─ draws the returned RGBA frame                     │
├─────────────────────────────────────────────────────────┤
│  libcui-widget            (this library, C ABI)         │
│    document → CSS cascade → layout → widget paint       │
│    (block + flex, hit-testing, frame clock)             │
├─────────────────────────────────────────────────────────┤
│  libcui                   (drawing primitives, raster)  │
└─────────────────────────────────────────────────────────┘
```

## Isolation — the whole point

libcui-widget **links only libcui and the C/C++ runtime**. It does **not** link
the widget sandbox: no QuickJS, no spec validator, no CPU/memory caps, no network,
no WebView. The trusted host drives everything and owns the document.

This is not an assertion — it is buildable. `examples/render_png.c` renders a full
widget to a PNG over the C ABI alone, and links **nothing** from the sandbox.
`nm` over both the static archive and that binary shows **zero** references to any
sandbox symbol (`quickjs`, `JS_*`, `validator`, `tw_*`, `measure_ops`,
`validate_ops`) — the only external references are libc++, libc, and `cui_*`.
Run it: `./build/cuiw-render "" cuiw-render.png`.

Trade-off, stated plainly: because there is no sandbox, **the host is trusted**.
This library does not defend against hostile input the way the sandboxed host
does — it assumes the RenderOps come from a source you already trust.

## Build

Requires CMake ≥ 3.20, a C++20 compiler, and a **built** CUI checkout
(`build/libcui.a`). On macOS the Apple frameworks (Cocoa, Metal, …) are linked
for you.

```sh
# CUI_ROOT defaults to ../CUI/vendor/CUI; override with -DCUI_ROOT=/path/to/CUI
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Produces:

| artifact | what it is |
| --- | --- |
| `build/libcui-widget.a` | the static library |
| `build/cuiw-render` | the isolation-proof PNG example |
| `build/cuiw-probe` | pixel probe (edge scan, no image viewer needed) |

Then:

```sh
./build/cuiw-render "" cuiw-render.png   # built-in sample → PNG
./build/cuiw-probe                       # print actual RGBA edges
```

## Drop-in, no CMake (amalgamation)

CUI ships as *"two files, `cui.h` and `cui.c`, dropped into your project."* This
library can't be dependency-free — its whole job is to draw through libcui — but it
matches the **drop-in** half. Run the amalgamator:

```sh
python3 tools/amalgamate.py            # → build/amalgamation/
```

That emits two files:

| file | what it is |
| --- | --- |
| `cuiw.h` | the public C ABI, self-contained |
| `cuiw.cpp` | every internal header + both sources inlined into one translation unit |

Drop all **four** files into one directory and compile. `cui.c` is C (compile it
once to an object), the rest are C++:

```sh
# 1. from CUI's checkout:  python3 tools/amalgamate.py dist   →  dist/cui.h + dist/cui.c
# 2. from this checkout:   python3 tools/amalgamate.py        →  build/amalgamation/cuiw.{h,cpp}
#
# 3. put cui.h cui.c cuiw.h cuiw.cpp (+ your app) in one directory, then:
clang -std=c99 -O2 -I. -DCUI_NO_APP -c cui.c -o cui.o        # CUI once, as C
clang++ -std=c++20 -O2 -I. -DCUI_DROPIN -c cuiw.cpp -o cuiw.o
clang++ -std=c++20 -O2 -I. -DCUI_DROPIN my_app.cpp cuiw.o cui.o -o my_app
```

`-DCUI_NO_APP` drops CUI's window layer — correct here, because this library is a
headless renderer that draws through CUI's primitives and brings no window of its
own. Link your own window/surface as usual.

Verified: this exact four-file path compiles standalone (no CMake, no `libcui.a`)
and renders a PNG **byte-identical** to the CMake build (same md5,
`ad5c900c…`). The honest line is *"four files, two of them CUI's, no build
system, needs a C++20 compiler"* — **not** "no dependencies."

Two macOS/CUI caveats found while verifying, both upstream:

- CUI's note to compile `cui.c` as Objective-C on macOS **fails**: the window layer
  pulls in Cocoa → `netinet/ip.h`, whose `u_int`/`u_short` are undefined in ObjC.
  CUI's own amalgamation CI only runs on ubuntu, so that path is untested. `-DCUI_NO_APP`
  sidesteps it entirely (and is what a headless renderer wants anyway).
- `-DCUI_NO_DEFAULT_FONTS` drops `cui_inflate.c` from CUI's amalgamation, but the
  emoji picker calls `_cui__inflate` unconditionally → link error. Leave fonts on.

## Using it (C ABI — `include/cuiw.h`)

```c
#include "cuiw.h"

cuiw_renderer_t r = cuiw_create("dark", 400.0, 2.0);   // theme, width_css, dpr

cuiw_send(r, "{\"type\":\"ops\",\"ops\":[...]}");       // build the document
cuiw_frame f = cuiw_paint(r);                          // rasterize now
// f.rgba is premultiplied RGBA8, f.width×f.height physical px, f.stride = width*4
// ... blit f.rgba to your surface ...
cuiw_frame_free(&f);

cuiw_click(r, 120.0, 300.0);                           // pointer in CSS px
cuiw_destroy(r);
```

Key points, all enforced by the header contract:

- **Ownership.** `cuiw_paint` hands you a frame you own → `cuiw_frame_free`.
  Strings passed *in* are borrowed; strings passed *out* are borrowed and valid
  until the next call on the handle.
- **Threads.** Every call on a handle is serialized by an internal mutex, so calls
  may come from any thread; paint from one thread. The optional frame clock
  (`cuiw_set_frame_callback`) runs on its own thread and invokes callbacks there —
  copy what you need inside the callback.
- **Replies** (query results, events, ready, reported document size) come back
  through the message callback, as JSON, on the same message contract the host
  speaks.
- **Capabilities are queryable.** Call `cuiw_has_feature` /
  `cuiw_feature_matrix` rather than assuming. An unsupported feature **degrades
  gracefully** — it does not silently mis-render. See
  [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md).

## Reference app + pixel-regression gate

`examples/app/widget_app.c` is the reference application: a representative widget
document (a profile card) that exercises **every supported capability** — block +
flex layout, id/class/tag selectors, border-radius, text-align, font-weight and
min-height. It renders headless to a light and a dark PNG.

```sh
cmake --build build --target widget-app pxdiff
./build/widget-app actual        # → actual/widget-light.png, actual/widget-dark.png
./build/pxdiff tests/golden/widget-light.png actual/widget-light.png
```

`tests/golden/widget-{light,dark}.png` are committed golden images. `tools/pxdiff.c`
compares the fresh render against them **pixel by pixel** and exits non-zero on any
difference, so a layout or paint regression fails the build instead of shipping. It
decodes both PNGs first and compares pixels (not file bytes), so a change in CUI's
PNG encoder doesn't raise a false alarm — only a real pixel change does. Pass
`--diff out.png` to get a red-highlighted visualization of what moved.

```sh
./build/pxdiff tests/golden/widget-light.png actual/widget-light.png --diff diff.png
# pixels 473040   differing 0 (0.0000%)   max channel delta 0   tol 0
# PASS
```

### Why the pixels are identical on every OS

The gate works across Linux, macOS and Windows only because the render is
**deterministic**. Three CUI properties make it so: text is rasterized by CUI's own
vendored `stb_truetype` (not the OS text stack), the default fonts (Inter, Geist) are
**bundled as byte-identical C arrays**, and the document is static (no animation,
clock or external input). Same engine + same font bytes + same document = same
pixels, so **one set of goldens gates every platform**.

`.github/workflows/ci.yml` runs this gate on `ubuntu-24.04` (gcc + clang),
`macos-latest` and `windows-2022` (MSVC) on every push. Each job pins the exact CUI
release (`CUI_REF`), builds CUI → libcui-widget, renders the reference app headless
(software rasterizer — no window, GPU or display needed), and fails if any pixel
differs from golden. A deliberate change to layout or paint is a **deliberate golden
update**: regenerate with `./build/widget-app tests/golden` and commit the new
images together with the code change.

## Capability matrix (summary)

The layout engine implements a **block + flexbox subset** of CSS. Supported today:
block & flex layout, id/class/tag selectors, border-radius, text-align,
font-weight, min-height — **9 of 26** tracked features.

Not yet: flex-wrap, absolute positioning, percent-height, inline layout, tables,
grid, floats, CSS variables, media queries, transitions, keyframes, overflow
scroll, text-decoration, opacity compositing, pseudo-classes (beyond `:disabled`),
canvas, inline SVG. The full table lives in
[`docs/CAPABILITIES.md`](docs/CAPABILITIES.md).

> **Honest limitation.** This is a subset engine, not a full browser layout
> engine. Documents that lean on the unsupported features above will lay out
> differently than in the sandboxed host. Query the matrix and design within it.

## Layout in the tree

| path | role |
| --- | --- |
| `include/cuiw.h` | the public C ABI — the only header an app includes |
| `include/twidget/*.h` | internal seam headers (stdlib-only) the lib is built from |
| `src/cuiw.cpp` | the C ABI implementation over the renderer |
| `src/cui_renderer.cpp` | document → CSS → layout → paint, through libcui |
| `examples/render_png.c` | isolation proof: full widget → PNG, C ABI only |
| `examples/probe.c` | pixel probe for layout checks |
| `examples/app/widget_app.c` | the reference app: exercises every supported feature |
| `tools/pxdiff.c` | pixel-by-pixel regression gate (golden vs render) |
| `tools/amalgamate.py` | emits the drop-in `cuiw.h` + `cuiw.cpp` pair |
| `tests/golden/*.png` | committed golden images the CI gate compares against |
| `.github/workflows/ci.yml` | Linux/macOS/Windows build + pixel-regression gate |

## Status

- Version `0.1.0`, built against CUI `1.4.0`. `cuiw_cui_version()` reads CUI's own
  `CUI_VERSION_STRING`, so the reported version can never drift from the linked lib.
- Library builds clean; the reference app renders correct light/dark PNGs and the
  pixel gate passes byte-exact against golden (and correctly fails on a change).
- CI builds and pixel-tests on Linux (gcc, clang), macOS and Windows (MSVC).
