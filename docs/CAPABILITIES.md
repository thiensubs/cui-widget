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
| overflow-scroll | `CUIW_FEATURE_OVERFLOW_SCROLL` | `overflow: hidden` clips descendant content (text + children) to the padding box; `visible` does not. Content is clipped, not scrolled |
| text-decoration | `CUIW_FEATURE_TEXT_DECORATION` | `underline` / `line-through`; propagates to descendant text like `color` |
| position: absolute | `CUIW_FEATURE_ABSOLUTE_POSITION` | out-of-flow box against the nearest positioned ancestor's padding box; `top`/`right`/`bottom`/`left` offsets, shrink-to-fit or explicit width. No `z-index` stacking contexts yet |
| grid | `CUIW_FEATURE_GRID` | `display: grid`; `grid-template-columns`/`grid-template-rows` (px/`%`/`fr`/auto), `gap`/`row-gap`/`column-gap`; auto-placement row-major, items stretch to row height. No explicit `grid-column`/`grid-row` placement, no spanning, no dense packing; `fr` rows treated as auto |
| inline-svg | `CUIW_FEATURE_INLINE_SVG` | an `<svg>` subtree paints geometrically through the rasterizer: `<rect>` (incl. `rx`/`ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, and `<path d>` with `M/L/H/V/C/Q/Z` (absolute + relative; curves flattened). `fill`/`stroke`/`stroke-width`/`fill-opacity`/`stroke-opacity` resolve through the CSS cascade and inherit; `viewBox` maps into the element box with `xMidYMid meet`. Not modelled: `<text>` inside SVG (labels use the host's HTML text), gradients, `<transform>`, filters, nested `<svg>`, the arc (`A`) path command |
| transitions | `CUIW_FEATURE_TRANSITIONS` | `transition` shorthand + the four longhands; timing `linear`/`ease`/`ease-in`/`ease-out`/`ease-in-out` (cubic-bezier); animates opacity, `color`/`background-color`/`border-color`, and px lengths (width/height/min-height/border-radius/border-width/font-size/gap/padding/margin/insets) on a per-frame anim clock. No `cubic-bezier()`/`steps()` author values (fall back to `ease`) |
| keyframes | `CUIW_FEATURE_KEYFRAMES` | `@keyframes name { from/to/% }` blocks + the `animation` shorthand (name, duration, timing, delay, iteration count, `infinite`) and `animation-name`/`-duration`/`-timing-function`/`-delay`/`-iteration-count` longhands. Interpolates the same property set as transitions (opacity, the three colors, px lengths) between bracketing stops, with the timing function applied per segment. A finite animation freezes at its 100% stop when done; `infinite` loops. Not modelled: `animation-direction`/`-fill-mode`/`-play-state` (parsed out of the shorthand, then ignored), `cubic-bezier()`/`steps()` (fall back to `ease`) |

**Not yet — 1**

| feature | enum | status |
| --- | --- | --- |
| canvas | `CUIW_FEATURE_CANVAS` | no 2D canvas |

**Totals:** 27 tracked, 26 supported, 1 not yet.

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
