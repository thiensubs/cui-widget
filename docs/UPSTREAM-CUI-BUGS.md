# Upstream CUI bugs found while building libcui-widget

Both reproduced against CUI **v1.4.0**. They live in CUI itself, not libcui-widget, so
they belong upstream (`yappologistic/CUI`). Each has a repro and a suggested fix. Neither
blocks libcui-widget — the workarounds below are what we ship with.

---

## Bug 1 — Compiling `cui.c` as Objective-C fails on macOS

**Where:** CUI's amalgamation / drop-in build notes say `cui.c` can be compiled as
Objective-C on macOS (so the window layer can call Cocoa). That path does not compile.

**Repro** (macOS, Apple clang):
```sh
clang -x objective-c -c cui.c -o /dev/null
```
The window layer pulls in Cocoa, which transitively includes `<netinet/ip.h>`, which uses
`u_int` / `u_short`. Those typedefs are not visible when the translation unit is compiled
as Objective-C, so the build fails with `use of undeclared identifier 'u_int'` (and
`u_short`).

**Why it went unnoticed:** CUI's own amalgamation CI runs only on Ubuntu, so the
macOS-Objective-C note is never exercised.

**Suggested fix:** either
- include `<sys/types.h>` before the netinet headers in the window layer (it defines
  `u_int` / `u_short` on Darwin), or
- drop the "compile as Objective-C" claim from the drop-in notes and keep the window
  layer as plain C / a separate ObjC shim.

**Our workaround:** headless consumers compile with `-DCUI_NO_APP`, which omits the
window layer entirely — which is what a software rasterizer wants anyway.

---

## Bug 2 — `-DCUI_NO_DEFAULT_FONTS` breaks the link (emoji picker needs `inflate`)

**Where:** building CUI with `-DCUI_NO_DEFAULT_FONTS` drops `cui_inflate.c` from the
sources, but the emoji picker calls the internal `_cui__inflate` unconditionally.

**Repro:**
```sh
# configure with -DCUI_NO_DEFAULT_FONTS, build, link any consumer:
undefined reference to `_cui__inflate'
```
The inflate symbol is gated behind the same flag as the fonts, but the emoji picker (which
is *not* gated) references it unconditionally, so a no-default-fonts build does not link.

**Suggested fix:** gate the emoji picker's use of `_cui__inflate` behind
`CUI_NO_DEFAULT_FONTS` (or always compile `cui_inflate.c` — PNG decoding needs it
independently of the bundled fonts).

**Our workaround:** leave the default fonts on. libcui-widget renders glyphs through CUI's
bundled `stb_truetype` path, so we never build with the fonts off.
