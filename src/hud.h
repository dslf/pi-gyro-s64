#ifndef VIEWER_HUD_H
#define VIEWER_HUD_H

#include <stddef.h>
#include "gl.h"

#define HUD_FIRST_GLYPH 32
#define HUD_GLYPH_COUNT 95 /* ASCII 32..126 */

/* GDI rasterizes ASCII into a fixed-cell atlas whose cell size is measured from
   the font, so it stays correct at any display DPI. `lum` is a top-down mask
   (row 0 = top of the glyph) - glyph ink becomes geometry, not a texture. */
typedef struct {
  int cw, ch, cols, rows, w, h;
  unsigned char* lum;
  float ink[HUD_GLYPH_COUNT];
  GLuint prog, vao, pos_vbo, col_vbo;
  GLint uViewport;
  float* pos_buf;      /* 6 vertices per ink run, 2 floats each */
  float* col_buf;      /* 6 vertices per ink run, 4 floats each */
  int run_cap;
  float fg[3]; /* gamma-encoded foreground colour */
} hud;

int  hud_init(hud* h, char* err, size_t errcap);
void hud_free(hud* h);

/* Draw `text` at pixel (x, y) with y counted from the top of the viewport. Each glyph
   is a set of horizontal ink runs, one triangle pair per run, colour premultiplied by
   ink. Depth test and blending are handled by the caller. */
void hud_draw(const hud* h, int viewport_w, int viewport_h, int x, int y, const char* text);

/* Test helpers: ink fraction of one glyph and its per-row ink profile (top-down). */
float hud_glyph_ink(const hud* h, char ch);
void  hud_glyph_row_profile(const hud* h, char ch, float* prof, int prof_cap);

/* Copy one glyph cell of the luminance mask into `out` (cap >= cw*ch). Diagnostics for
   the selftest, so a solid-looking cell can be dumped column by column. */
void  hud_glyph_cell_mask(const hud* h, char ch, unsigned char* out, int cap);

#endif
