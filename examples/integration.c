/* cuiw-integration — the CUI + libcui-widget integration demo.
 *
 * A split-pane window that proves the whole story:
 *
 *   LEFT   the widget's DATA — the RenderOps document, as JSON
 *   RIGHT  the RENDERED IMAGE — libcui-widget paints the document into an
 *          RGBA buffer, which becomes a CUI texture drawn in this window
 *   BUTTON "Copy image" — writes that RGBA buffer out as a PNG (the bytes
 *          you would hand to the platform clipboard; CUI's own clipboard is
 *          text-only, so the image bytes go to a file here)
 *
 * The point: CUI draws its own ~210 components, but it cannot draw a WIDGET
 * from data. libcui-widget is the brain that turns a widget document into
 * pixels — and those pixels drop straight into a CUI window as a texture.
 *
 * Usage: cuiw-integration [widget.json] [out.png]
 *   widget.json  a RenderOps document (default: docs/showcase_hero.json)
 *   out.png      where "Copy image" writes the PNG (default: widget.png)
 */
#include <cui/cui.h>
#include <cui/cui_app.h>
#include <cui/cui_render.h>
#include <cuiw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reads a whole file into a NUL-terminated buffer. Caller frees. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = '\0';
    fclose(f);
    if (len_out) *len_out = (size_t)n;
    return buf;
}

/* The widget document, the rendered image, and the copy target. */
struct demo {
    cuiw_renderer_t r;        /* libcui-widget renderer (owns the document) */
    char *json;               /* the widget DATA shown on the left */
    const char *out_png;      /* where "Copy image" writes */
    cui_texture tex;          /* the rendered widget, as a CUI texture */
    int tex_w, tex_h;
    bool tex_valid;
    bool copied;              /* flips the button to a check */
    bool copy_once;           /* --copy-once: write the PNG, then quit */
};

/* Paint the widget document and upload the frame as a CUI texture. */
static void render_widget(struct demo *d, cui_ctx *ui) {
    cuiw_frame f = cuiw_paint(d->r);
    if (!f.rgba) return;
    /* Drop the previous texture; the frame is the fresh source of truth. */
    if (d->tex_valid) cui_image_destroy(ui, d->tex);
    d->tex = cui_image_create(ui, (int)f.width, (int)f.height, f.rgba);
    d->tex_w = (int)f.width;
    d->tex_h = (int)f.height;
    d->tex_valid = d->tex != 0;
    cuiw_frame_free(&f);
}

/* "Copy image": write the rendered widget out as PNG bytes. A real host would
 * hand these bytes to the platform image clipboard; CUI's clipboard is
 * text-only, so we write the file to prove the path end to end. */
static void copy_image(struct demo *d) {
    cuiw_frame f = cuiw_paint(d->r);
    if (!f.rgba) return;
    if (cui_write_png(d->out_png, f.rgba, (int)f.width, (int)f.height,
                      (int)f.stride))
        d->copied = true;
    else
        fprintf(stderr, "cui_write_png failed for %s\n", d->out_png);
    cuiw_frame_free(&f);
}

static void build_ui(cui_ctx *ui, void *user) {
    struct demo *d = (struct demo *)user;

    /* --copy-once: render + write the PNG on the first frame, then quit. Lets
     * the render->copy path be verified headlessly (no button click). */
    if (d->copy_once) {
        copy_image(d);
        cui_app_quit(NULL);
        return;
    }

    cui_heading(ui, "CUI + libcui-widget", 2);
    cui_secondary(ui, "Left: the widget DATA. Right: the rendered IMAGE. "
                      "The brain turns one into the other.");

    /* Two panes side by side. */
    cui_row(ui, .gap = 16, .width = CUI_FILL, .height = CUI_FILL) {
        /* LEFT — the widget DATA, as JSON. */
        cui_column(ui, .gap = 8, .width = CUI_FILL, .height = CUI_FILL) {
            cui_heading(ui, "Widget data", 3);
            cui_code_block_ex(ui, "data", d->json,
                              &(cui_code_opts){ .lang = "json",
                                                .title = "widget.json",
                                                .line_numbers = true,
                                                .max_height = 420 });
        }

        /* RIGHT — the RENDERED IMAGE, plus the copy button. */
        cui_column(ui, .gap = 8, .width = CUI_FILL, .height = CUI_FILL) {
            cui_heading(ui, "Rendered image", 3);
            if (d->tex_valid) {
                cui_image_ex(ui, d->tex, &(cui_image_opts){
                    .width = CUI_FILL });
            } else {
                cui_secondary(ui, "Rendering…");
            }
            cui_row(ui, .gap = 8) {
                if (cui_button_ex(ui, d->copied ? "Copied" : "Copy image",
                                  &(cui_button_opts){
                                      .variant = CUI_BUTTON_PRIMARY,
                                      .icon = CUI_ICON_CLIPBOARD })) {
                    copy_image(d);
                }
                cui_textf(ui, "%d × %d px", d->tex_w, d->tex_h);
            }
            if (d->copied)
                cui_secondary(ui, d->out_png);
        }
    }

    /* Paint the widget every frame so it stays live (animation, state). */
    render_widget(d, ui);
}

int main(int argc, char **argv) {
    const char *widget_path = "docs/showcase_hero.json";
    const char *out_png = "widget.png";
    bool copy_once = false;
    /* Parse args: the first non-flag is the widget doc, the second is the PNG
     * out path; --copy-once writes the PNG on the first frame and quits. */
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--copy-once") == 0) copy_once = true;
        else if (positional == 0) { widget_path = argv[i]; positional++; }
        else if (positional == 1) { out_png = argv[i]; positional++; }
    }

    /* Load the widget DATA (the RenderOps document) to show on the left. */
    char *json = read_file(widget_path, NULL);
    if (!json) { fprintf(stderr, "cannot read %s\n", widget_path); return 1; }

    /* The libcui-widget renderer that turns that data into pixels. */
    cuiw_renderer_t r = cuiw_create("dark", 380.0, 2.0);
    if (!r) { fprintf(stderr, "cuiw_create failed\n"); free(json); return 1; }

    /* Feed the document in as the envelope the widget host sends. */
    {
        const char *prefix = "{\"type\":\"ops\",\"ops\":";
        size_t nlen = strlen(prefix), jlen = strlen(json);
        char *msg = (char *)malloc(nlen + jlen + 2);
        if (!msg) { cuiw_destroy(r); free(json); return 1; }
        memcpy(msg, prefix, nlen);
        memcpy(msg + nlen, json, jlen);
        msg[nlen + jlen] = '}';
        msg[nlen + jlen + 1] = '\0';
        if (!cuiw_send(r, msg)) fprintf(stderr, "cuiw_send rejected ops\n");
        free(msg);
    }

    struct demo d;
    memset(&d, 0, sizeof d);
    d.r = r;
    d.json = json;
    d.out_png = out_png;
    d.copy_once = copy_once;

    /* The CUI window: our rendered widget is drawn inside it as a texture. */
    cui_app_run(&(cui_app_desc){ .title = "CUI + libcui-widget",
                                 .width = 980, .height = 640 },
                build_ui, &d);

    cuiw_destroy(r);
    free(json);
    return 0;
}
