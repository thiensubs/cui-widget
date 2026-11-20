# libcui-widget — capability matrix

The honest list of what the layout engine does and does not implement. This is
measured, not aspirational: every row below is the output of `cuiw_feature_matrix`
from the shipped `build/libcui-widget.a` (version `0.1.0`, CUI `1.4.0`).

Two rules govern this table:

1. **Query, don't assume.** Call `cuiw_has_feature(f)` / `cuiw_feature_matrix(...)`
   at runtime. Never hard-code an assumption about a feature.
2. **Unsupported degrades, never corrupts.** A feature the engine lacks is skipped
   or approximated; it does not silently mis-render or crash. The safe path is to
   design documents within the SUPPORTED set.

## The matrix

**Supported — 25**

| feature | enum | notes |
| --- | --- | --- |
| block layout | `CUIW_FEATURE_BLOCK` | vertical flow, margins, padding |
| flex layout | `CUIW_FEATURE_FLEX` | row & column, gaps, alignment, flex-grow |
| flex-wrap | `CUIW_FEATURE_FLEX_WRAP` | `wrap` breaks row items into lines that fit; per-line justify + cross-align |
| id selector | `CUIW_FEATURE_ID_SELECTOR` | `#id` |
| class selector | `CUIW_FEATURE_CLASS_SELECTOR` | `.class` |
| tag selector | `CUIW_FEATURE_TAG_SELECTOR` | element name |
| border-radius | `CUIW_FEATURE_BORDER_RADIUS` | rounded corners (drawn via libcui) |
| text-align | `CUIW_FEATURE_TEXT_ALIGN` | left / center / right |
| font-weight | `CUIW_FEATURE_FONT_WEIGHT` | normal / bold |
| min-height | `CUIW_FEATURE_MIN_HEIGHT` | per-node minimum block size |
| vertical-align | `CUIW_FEATURE_VERTICAL_ALIGN` | `super` / `sub` / `middle` / px length on inline runs; shifts the segment baseline, does not change line height |
| css-variables | `CUIW_FEATURE_CSS_VARIABLES` | `--name` custom properties on `:root`/any element, inherited; `var(--name)` and `var(--name, fallback)`; unresolved var() with no fallback drops the declaration |
| opacity-compositing | `CUIW_FEATURE_OPACITY_COMPOSITING` | group `opacity` 0.0-1.0; scales the alpha of every color the node paints (bg/border/text) and accumulates down the tree, so a faded container fades descendants |
| pseudo-classes | `CUIW_FEATURE_PSEUDO_CLASSES` | `:disabled`, plus structural `:first-child` / `:last-child` / `:nth-child(an+b)` (incl. `odd`/`even`); position counted among element siblings. `:hover`/`:focus` etc. are unsupported and never match |
| media-queries | `CUIW_FEATURE_MEDIA_QUERIES` | `@media` width constraints (`min-width` / `max-width`, combined with `and`) evaluated against the document's current CSS width; an unevaluable query (orientation, …) is dropped entirely |
| floats | `CUIW_FEATURE_FLOATS` | `float: left` / `right`; the box is taken out of flow, placed at the line's content edge, and following inline content + block siblings wrap around it; the container grows to contain floats. No `clear`, no nested-float stacking beyond side accumulation |
| percent-height | `CUIW_FEATURE_PERCENT_HEIGHT` | % width & height resolve against the containing block; % height needs a definite parent height (CSS 10.5) |
| inline layout | `CUIW_FEATURE_INLINE_LAYOUT` | runs of inline siblings (text, span/em/strong/a/…) flow on shared line boxes and wrap as a unit; per-segment color/weight |
| tables | `CUIW_FEATURE_TABLES` | `table`/`tr`/`td`/`th`, auto column widths, `colspan`, row heights, cell stretch; no `rowspan` yet |
| overflow-scroll | `CUIW_FEATURE_OVERFLOW_SCROLL` | `overflow: hidden`/`clip` clip descendant content (text + children) to the padding box; `visible` does not. `overflow: scroll`/`auto` make the box a scroll container: content is clipped to the box and translated up by its `scroll_top`, set by the host via `{"op":"prop","id":N,"name":"scrollTop","value":Y}` and clamped to `[0, content − box]` so it can't scroll past the end. Vertical only (`scrollLeft` not modelled); no scrollbar is drawn (the host owns input) |
| text-decoration | `CUIW_FEATURE_TEXT_DECORATION` | `underline` / `line-through`; propagates to descendant text like `color` |
| position: absolute | `CUIW_FEATURE_ABSOLUTE_POSITION` | out-of-flow box against the nearest positioned ancestor's padding box; `top`/`right`/`bottom`/`left` offsets, shrink-to-fit or explicit width. No `z-index` stacking contexts yet |
| grid | `CUIW_FEATURE_GRID` | `display: grid`; `grid-template-columns`/`grid-template-rows` (px/`%`/`fr`/auto), `gap`/`row-gap`/`column-gap`; auto-placement row-major, items stretch to row height. No explicit `grid-column`/`grid-row` placement, no spanning, no dense packing; `fr` rows treated as auto |
| inline-svg | `CUIW_FEATURE_INLINE_SVG` | an `<svg>` subtree paints geometrically through the rasterizer: `<rect>` (incl. `rx`/`ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, and `<path d>` with `M/L/H/V/C/Q/Z` (absolute + relative; curves flattened). `fill`/`stroke`/`stroke-width`/`fill-opacity`/`stroke-opacity` resolve through the CSS cascade and inherit; `viewBox` maps into the element box with `xMidYMid meet`. `<text>` draws its string at `x`/`y` (viewBox coords, `y` as the baseline) through the text renderer, honouring `fill`, `font-size`, `font-weight`, and `text-anchor` `middle`/`end`; labels paint after the shapes so they sit on top. Not modelled: gradients, `<transform>`, filters, nested `<svg>`, the arc (`A`) path command, `<tspan>`/`textLength`/per-glyph positioning |
| transitions | `CUIW_FEATURE_TRANSITIONS` | `transition` shorthand + the four longhands; timing `linear`/`ease`/`ease-in`/`ease-out`/`ease-in-out` (cubic-bezier); animates opacity, `color`/`background-color`/`border-color`, and px lengths (width/height/min-height/border-radius/border-width/font-size/gap/padding/margin/insets) on a per-frame anim clock. No `cubic-bezier()`/`steps()` author values (fall back to `ease`) |
| keyframes | `CUIW_FEATURE_KEYFRAMES` | `@keyframes name { from/to/% }` blocks + the `animation` shorthand (name, duration, timing, delay, iteration count, `infinite`) and `animation-name`/`-duration`/`-timing-function`/`-delay`/`-iteration-count` longhands. Interpolates the same property set as transitions (opacity, the three colors, px lengths) between bracketing stops, with the timing function applied per segment. A finite animation freezes at its 100% stop when done; `infinite` loops. Not modelled: `animation-direction`/`-fill-mode`/`-play-state` (parsed out of the shorthand, then ignored), `cubic-bezier()`/`steps()` (fall back to `ease`) |
| canvas | `CUIW_FEATURE_CANVAS` | a `<canvas>` element replays a recorded batch of 2D-context commands (the `canvas` op) into its box through the same rasterizer as inline SVG, clipped to the element. State: `fillStyle`, `strokeStyle`, `lineWidth`, `globalAlpha`. Shapes: `fillRect`, `strokeRect`, `clearRect` (repaints the element background), path ops `beginPath`/`moveTo`/`lineTo`/`rect`/`arc` (flattened to a polygon)/`closePath`/`fill`/`stroke`, `fillText`, and `drawImage` (a `data:` URL decoded once and cached by URL, drawn scaled to cover the dest rect). Unknown command names are skipped, not fatal. Not modelled: gradients/patterns, `getImageData`, transforms (`translate`/`rotate`/`scale`), `clip()`, composite modes, `strokeText`, text metrics |
| gradients | `CUIW_FEATURE_GRADIENTS` | `background-image: linear-gradient(<direction>, <color> [pos], ...)` with any number of stops (drawn as one top->bottom band per stop pair; the first band carries the top rounded corners and the last the bottom, so the rounded edge is preserved without a clip). A direction is optional (default `to bottom`); `to top` reverses the stops. A stop may carry an explicit position (`<color> 40%`); unpositioned stops are spaced evenly between their positioned neighbours, and out-of-order stops are clamped non-decreasing. Paints over `background-color` when both are set (CSS order). Not modelled: `to left`/`to right`/angle directions and `radial-gradient`/`repeating-*` (a direction other than `to top`/`to bottom` falls back to a top->bottom render), `url()`, colour-interpolation methods |
| box-shadow | `CUIW_FEATURE_BOX_SHADOW` | `box-shadow: <x> <y> <blur> <spread> <color>` painted behind the box (CUI's `cui_draw_shadow`), so only the blurred edge shows around it. Offsets required, blur/spread optional (default 0), color optional (default a soft black). Scaled by the element's group opacity. Not modelled: `inset` (recognised and skipped), multiple comma-separated shadows (only the first is applied) |
| images | `CUIW_FEATURE_IMAGES` | an `<img>` element decodes its `src` — which must be a `data:<mediatype>;base64,<payload>` URL (the only image form the host contract allows: no network, no file paths) — base64-decodes the payload and decodes the PNG/JPEG bytes with stb_image (vendored by CUI), uploads it to a texture once and reuses it across frames. The picture is drawn scaled to **cover** the element box, aspect ratio preserved and centred (CSS `object-fit: cover`), clipped to the box, with the element's `border-radius`. A decode failure draws nothing and is not retried. Not modelled: `http(s)`/file `src`, `srcset`/`sizes`, `alt` text, `object-fit` other than cover, SVG-in-`<img>` |

**Totals:** 30 tracked, 30 supported.

## How to check a document against this

```c
#include "cuiw.h"
#include <stdio.h>

static void report(void) {
    uint8_t m[CUIW_FEATURE__COUNT];
    size_t n = cuiw_feature_matrix(m, CUIW_FEATURE__COUNT);
    for (size_t i = 0; i < n; i++)
        printf("%-12s %s\n", m[i] ? "supported" : "unsupported",
               cuiw_feature_name((cuiw_feature)i));
}
```

Or from the shell, rebuild the probe the README links:

```sh
./build/cuiw-render "" cuiw-render.png   # a correct render is the end-to-end check
```

## Design guidance within the subset

- Lay out with **block, flex, or grid** (bounded auto-placement); avoid floats. Use `position: absolute` for badges and overlays.
- Size with **fixed / min-height**; do not rely on percent-height.
- Style with the supported properties; treat the rest as no-ops.
- Use the `CUIW_FEATURE_*` enum as your compile-time checklist when authoring
  templates: if a template needs an unsupported feature, either change the template
  or extend the engine (each feature is a bounded, testable unit).
