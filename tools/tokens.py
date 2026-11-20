"""Design tokens for libcui-widget showcases — the single source of truth.

A "token" is a named value (a spacing, a font size, a colour) that every
showcase reads from one place, so they all look like the same product. Change a
token here and every screen updates together.

These are plain CSS custom properties on :root (the engine resolves var() from
:root and rule blocks). Kept deliberately small: a scale you can hold in your
head beats an exhaustive one nobody follows.

Spacing follows a 4px base (4/8/12/16/24/32) — everything is a multiple, so
paddings and gaps never drift to odd values. Type is a 1.2 ratio scale.
Colours are a dark-theme ramp built from one hue, plus semantic accents.
"""

TOKENS = """
:root{
  /* ---- spacing (4px base) ---- */
  --s1:4px; --s2:8px; --s3:12px; --s4:16px; --s6:24px; --s8:32px;

  /* ---- radius ---- */
  --r-sm:8px; --r-md:12px; --r-lg:18px; --r-pill:999px;

  /* ---- type scale (1.2 ratio, 13px body) ---- */
  --t-xs:10px; --t-sm:11px; --t-md:13px; --t-lg:16px; --t-xl:20px; --t-2xl:26px;

  /* ---- neutral ramp (cool dark) ---- */
  --bg:#0a0e14;        /* page */
  --surface:#111823;   /* card */
  --surface-2:#18212f; /* raised card / inset */
  --border:#232f42;    /* hairline */
  --ink:#e8eef7;       /* primary text */
  --ink-dim:#93a3ba;   /* secondary text */
  --ink-faint:#5d6d85; /* tertiary / meta */

  /* ---- brand + semantic accents ---- */
  --brand:#7c6cff;     /* primary violet */
  --brand-2:#ec4899;   /* secondary pink (gradient partner) */
  --good:#34d399;
  --warn:#fbbf24;
  --bad:#f87171;

  /* ---- elevation ---- */
  --shadow-sm:0 2px 8px rgba(0,0,0,0.35);
  --shadow-md:0 10px 24px rgba(0,0,0,0.45);
  --shadow-lg:0 18px 40px rgba(0,0,0,0.55);

  /* ---- gradient ---- */
  --grad-brand:linear-gradient(135deg,var(--brand),var(--brand-2));
}
"""


import re

_TOKEN_COMMENT = re.compile(r"/\*.*?\*/", re.S)


def tokens_css():
    # The engine's CSS parser does not strip /* */ comments — it treats the text
    # before a "{" as the selector, so a comment inside a block corrupts the whole
    # sheet (no tokens resolve, nothing lays out). Strip them before emitting.
    return _TOKEN_COMMENT.sub("", TOKENS)
