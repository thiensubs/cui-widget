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

**Supported — 12**

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
| percent-height | `CUIW_FEATURE_PERCENT_HEIGHT` | % width & height resolve against the containing block; % height needs a definite parent height (CSS 10.5) |
| inline layout | `CUIW_FEATURE_INLINE_LAYOUT` | runs of inline siblings (text, span/em/strong/a/…) flow on shared line boxes and wrap as a unit; per-segment color/weight |
| tables | `CUIW_FEATURE_TABLES` | `table`/`tr`/`td`/`th`, auto column widths, `colspan`, row heights, cell stretch; no `rowspan` yet |
| overflow-scroll | `CUIW_FEATURE_OVERFLOW_SCROLL` | `overflow: hidden` clips descendant content (text + children) to the padding box; `visible` does not. Content is clipped, not scrolled |
| text-decoration | `CUIW_FEATURE_TEXT_DECORATION` | `underline` / `line-through`; propagates to descendant text like `color` |

**Not yet — 14**

| feature | enum | status |
| --- | --- | --- |
| position: absolute | `CUIW_FEATURE_ABSOLUTE_POSITION` | not implemented |
| grid | `CUIW_FEATURE_GRID` | no CSS grid |
| floats | `CUIW_FEATURE_FLOATS` | no float |
| css-variables | `CUIW_FEATURE_CSS_VARIABLES` | `var()` not resolved |
| media-queries | `CUIW_FEATURE_MEDIA_QUERIES` | none |
| transitions | `CUIW_FEATURE_TRANSITIONS` | static styles only |
| keyframes | `CUIW_FEATURE_KEYFRAMES` | no animation |
| opacity-compositing | `CUIW_FEATURE_OPACITY_COMPOSITING` | alpha not composited per-node |
| pseudo-classes | `CUIW_FEATURE_PSEUDO_CLASSES` | `:disabled` only |
| canvas | `CUIW_FEATURE_CANVAS` | no 2D canvas |
| inline-svg | `CUIW_FEATURE_INLINE_SVG` | no SVG |

**Totals:** 26 tracked, 9 supported, 17 not yet.

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

- Lay out with **block and flex**; avoid grid, floats, absolute. Tables work for simple data grids.
- Size with **fixed / min-height**; do not rely on percent-height.
- Style with the supported properties; treat the rest as no-ops.
- Use the `CUIW_FEATURE_*` enum as your compile-time checklist when authoring
  templates: if a template needs an unsupported feature, either change the template
  or extend the engine (each feature is a bounded, testable unit).
