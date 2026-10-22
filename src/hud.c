/* HUD text: GDI rasterizes an ASCII glyph atlas, GL draws the ink as premultiplied
   coloured rectangles. Kept out of render.c so render stays a model-walking module. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "gl.h"
#include "hud.h"
#include "shader.h"

#define HUD_VERT "shaders/hud.vert"
#define HUD_FRAG "shaders/hud.frag"

/* GDI rasterizes ASCII into a fixed-cell atlas. Cell size is measured from the font,
   so the atlas is correct at any display DPI instead of assuming a 96 dpi font. */
static int build_atlas(hud* h, char* err, size_t errcap) {
  HDC dc = CreateCompatibleDC(NULL);
  if (!dc) { snprintf(err, errcap, "CreateCompatibleDC failed (GDI text device)"); return -1; }

  HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                           FIXED_PITCH | FF_DONTCARE, L"Consolas");
  if (!font) { snprintf(err, errcap, "CreateFontW failed for 'Consolas'"); DeleteDC(dc); return -1; }
  SelectObject(dc, font);

  SIZE ext;
  if (!GetTextExtentPoint32W(dc, L"M", 1, &ext)) {
    snprintf(err, errcap, "GetTextExtentPoint32W failed measuring 'M'"); goto fail;
  }
  TEXTMETRICW tm;
  if (!GetTextMetricsW(dc, &tm)) { snprintf(err, errcap, "GetTextMetricsW failed"); goto fail; }

  h->cw = ext.cx + 2;
  h->ch = tm.tmHeight + 2;
  h->cols = 10;
  h->rows = (HUD_GLYPH_COUNT + h->cols - 1) / h->cols;
  h->w = h->cols * h->cw;
  h->h = h->rows * h->ch;

  BITMAPINFOHEADER bi;
  memset(&bi, 0, sizeof bi);
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = h->w;
  bi.biHeight = -h->h; /* top-down DIB: row 0 is the top of the glyph */
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;

  void* bits = NULL;
  HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bmp || !bits) { snprintf(err, errcap, "CreateDIBSection failed for %dx%d atlas", h->w, h->h); goto fail; }

  HGDIOBJ old = SelectObject(dc, bmp);
  RECT full = { 0, 0, h->w, h->h };
  FillRect(dc, &full, GetStockObject(WHITE_BRUSH));
  SetBkColor(dc, RGB(255, 255, 255));
  SetTextColor(dc, RGB(0, 0, 0));

  for (int i = 0; i < HUD_GLYPH_COUNT; ++i) {
    wchar_t ch = (wchar_t)(HUD_FIRST_GLYPH + i);
    TextOutW(dc, (i % h->cols) * h->cw, (i / h->cols) * h->ch, &ch, 1);
  }

  h->lum = malloc((size_t)h->w * h->h);
  if (!h->lum) {
    snprintf(err, errcap, "out of memory for glyph mask (%zu bytes)", (size_t)h->w * h->h);
    SelectObject(dc, old); DeleteObject(bmp); goto fail;
  }

  const unsigned char* src = (const unsigned char*)bits;
  for (int y = 0; y < h->h; ++y) {
    for (int x = 0; x < h->w; ++x) {
      const unsigned char* p = src + ((size_t)y * h->w + x) * 4; /* BGRA in the DIB */
      /* BT.601 luminance (weights sum to 100), so white stays 255 and black stays 0. */
      int lum = (p[2] * 30 + p[1] * 59 + p[0] * 11) / 100;
      h->lum[(size_t)y * h->w + x] = (unsigned char)lum;
    }
  }

  for (int i = 0; i < HUD_GLYPH_COUNT; ++i) {
    int col = i % h->cols, row = i / h->cols, dark = 0;
    for (int y = row * h->ch; y < (row + 1) * h->ch; ++y)
      for (int x = col * h->cw; x < (col + 1) * h->cw; ++x)
        if (h->lum[(size_t)y * h->w + x] < 160) dark++;
    h->ink[i] = (float)dark / (float)(h->cw * h->ch);
  }

  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
  DeleteObject(font);
  return 0;

fail:
  DeleteDC(dc);
  DeleteObject(font);
  return -1;
}

int hud_init(hud* h, char* err, size_t errcap) {
  memset(h, 0, sizeof *h);
  h->fg[0] = 0.95f; h->fg[1] = 0.98f; h->fg[2] = 1.00f;

  if (build_atlas(h, err, errcap) < 0) return -1;

  /* Every printable glyph must actually rasterize; a blank cell means the font
     did not render and the HUD would silently show nothing. */
  float min_ink = 0.0f;
  for (int i = 0; i < HUD_GLYPH_COUNT; ++i) {
    if (i == 0) continue; /* space is intentionally empty */
    if (h->ink[i] < 0.02f) {
      snprintf(err, errcap, "glyph '%c' rasterized blank (ink %.4f of cell %dx%d)",
               (char)(HUD_FIRST_GLYPH + i), h->ink[i], h->cw, h->ch);
      return -1;
    }
    if (i == 1 || h->ink[i] < min_ink) min_ink = h->ink[i];
  }

  if (shader_build(HUD_VERT, HUD_FRAG, &h->prog, err, errcap) < 0) return -1;
  h->uViewport = my_glGetUniformLocation(h->prog, "uViewport");
  if (h->uViewport < 0) {
    snprintf(err, errcap, "uniform 'uViewport' not found in %s", HUD_VERT);
    return -1;
  }

  my_glGenVertexArrays(1, &h->vao);
  my_glGenBuffers(2, &h->pos_vbo); /* pos_vbo, then col_vbo = pos_vbo + 1 */
  h->col_vbo = h->pos_vbo + 1;

  h->run_cap = 4096; /* ink runs per string, 6 vertices each */
  h->pos_buf = malloc((size_t)(6 * 2 * h->run_cap) * sizeof(float));
  h->col_buf = malloc((size_t)(6 * 4 * h->run_cap) * sizeof(float));
  if (!h->pos_buf || !h->col_buf) {
    snprintf(err, errcap, "out of memory for HUD geometry (%zu bytes)",
             (size_t)(6 * 6 * h->run_cap) * sizeof(float));
    return -1;
  }

  printf("hud: atlas %dx%d, cell %dx%d, %d glyphs, min ink %.3f\n",
         h->w, h->h, h->cw, h->ch, HUD_GLYPH_COUNT - 1, min_ink);
  return 0;
}

void hud_free(hud* h) {
  if (h->prog) my_glDeleteProgram(h->prog);
  if (h->pos_vbo || h->col_vbo) { my_glDeleteBuffers(1, &h->pos_vbo); my_glDeleteBuffers(1, &h->col_vbo); }
  if (h->vao) my_glDeleteVertexArrays(1, &h->vao);
  free(h->lum);
  free(h->pos_buf);
  free(h->col_buf);
  memset(h, 0, sizeof *h);
}

float hud_glyph_ink(const hud* h, char ch) {
  int gi = (unsigned char)ch - HUD_FIRST_GLYPH;
  if (gi < 0 || gi >= HUD_GLYPH_COUNT) return 0.0f;
  return h->ink[gi];
}

/* Per-row ink fraction of one glyph in the CPU mask, rows counted top-down.
   Used by the selftest to prove the rendered glyph matches the mask orientation. */
void hud_glyph_row_profile(const hud* h, char ch, float* prof, int prof_cap) {
  int gi = (unsigned char)ch - HUD_FIRST_GLYPH;
  for (int v = 0; v < prof_cap; ++v) prof[v] = 0.0f;
  if (gi < 0 || gi >= HUD_GLYPH_COUNT) return;
  int gc = gi % h->cols, gr = gi / h->cols;
  for (int v = 0; v < h->ch && v < prof_cap; ++v) {
    int ink_px = 0;
    for (int c = 0; c < h->cw; ++c) {
      float ink = (255.0f - (float)h->lum[(size_t)(gr * h->ch + v) * h->w + gc * h->cw + c]) / 255.0f;
      if (ink > 0.02f) ink_px++;
    }
    prof[v] = (float)ink_px / (float)h->cw;
  }
}

void hud_glyph_cell_mask(const hud* h, char ch, unsigned char* out, int cap) {
  int gi = (unsigned char)ch - HUD_FIRST_GLYPH;
  for (int i = 0; i < cap; ++i) out[i] = 0;
  if (gi < 0 || gi >= HUD_GLYPH_COUNT) return;
  int gc = gi % h->cols, gr = gi / h->cols;
  int copy = h->cw * h->ch;
  if (copy > cap) copy = cap;
  for (int i = 0; i < copy; ++i) {
    int v = i / h->cw, c = i % h->cw;
    out[i] = h->lum[(size_t)(gr * h->ch + v) * h->w + gc * h->cw + c];
  }
}

/* Draw `text` at pixel (x, y) into the currently bound framebuffer, y counted from the top.
   Each glyph becomes horizontal ink runs read from the CPU mask; colour is premultiplied
   (rgb = fg * ink, a = ink), so blending is (ONE, ONE_MINUS_SRC_ALPHA) over whatever was
   already rendered. Depth test is off - the HUD is a screen overlay, not scene geometry. */
void hud_draw(const hud* h, int viewport_w, int viewport_h, int x, int y, const char* text) {
  if (viewport_w <= 0 || viewport_h <= 0) {
    printf("hud: viewport %dx%d is empty, text not drawn\n", viewport_w, viewport_h);
    return;
  }

  int n = 0, runs = 0, drawn = 0;
  for (const char* p = text; *p; ++p, x += h->cw, ++drawn) {
    int gi = (unsigned char)*p - HUD_FIRST_GLYPH;
    if (gi < 0 || gi >= HUD_GLYPH_COUNT) continue;
    int gc = gi % h->cols, gr = gi / h->cols;
    for (int v = 0; v < h->ch; ++v) {
      int run_start = -1;
      float run_ink = 0.0f;
      for (int c = 0; c <= h->cw; ++c) {
        float ink = 0.0f;
        if (c < h->cw)
          ink = (255.0f - (float)h->lum[(size_t)(gr * h->ch + v) * h->w + gc * h->cw + c]) / 255.0f;
        if (ink > 0.02f) {
          if (run_start < 0) { run_start = c; run_ink = ink; }
          else if (ink > run_ink) run_ink = ink;
          continue;
        }
        if (run_start < 0) continue;
        if (runs >= h->run_cap) break;

        float px = (float)(x + run_start), py = (float)(y + v);
        float qx = px + (float)(c - run_start), qy = py + 1.0f;
        float r = h->fg[0] * run_ink, g = h->fg[1] * run_ink, b = h->fg[2] * run_ink;
        for (int k = 0; k < 6; ++k) { /* TL, TR, BR, TL, BR, BL */
          float cx = (k == 0 || k == 3 || k == 5) ? px : qx;
          float cy = (k == 0 || k == 3) ? py : qy;
          h->pos_buf[n * 2 + 0] = cx;
          h->pos_buf[n * 2 + 1] = cy;
          h->col_buf[n * 4 + 0] = r;
          h->col_buf[n * 4 + 1] = g;
          h->col_buf[n * 4 + 2] = b;
          h->col_buf[n * 4 + 3] = run_ink;
          n++;
        }
        runs++;
        run_start = -1;
        run_ink = 0.0f;
      }
      if (runs >= h->run_cap) break;
    }
    if (runs >= h->run_cap) break;
  }

  float vp[2] = { (float)viewport_w, (float)viewport_h };
  my_glUseProgram(h->prog);
  my_glUniform2fv(h->uViewport, 1, vp);
  my_glBindVertexArray(h->vao);

  my_glBindBuffer(GL_ARRAY_BUFFER, h->pos_vbo);
  my_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 2 * sizeof(float)), h->pos_buf, GL_DYNAMIC_DRAW);
  my_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);
  my_glEnableVertexAttribArray(0);

  my_glBindBuffer(GL_ARRAY_BUFFER, h->col_vbo);
  my_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 4 * sizeof(float)), h->col_buf, GL_DYNAMIC_DRAW);
  my_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void*)0);
  my_glEnableVertexAttribArray(1);

  int depth_was = my_glIsEnabled(GL_DEPTH_TEST);
  my_glDisable(GL_DEPTH_TEST);
  my_glEnable(GL_BLEND);
  my_glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); /* premultiplied ink */
  my_glDrawArrays(GL_TRIANGLES, 0, n);
  my_glDisable(GL_BLEND);
  if (depth_was) my_glEnable(GL_DEPTH_TEST);
  my_glDisableVertexAttribArray(0);
  my_glDisableVertexAttribArray(1);

  if (runs >= h->run_cap) printf("hud: '%s' truncated after %d ink runs (cap %d)\n", text, runs, h->run_cap);
  if (text[drawn]) printf("hud: '%s' truncated after %d glyphs (cap %d runs)\n", text, drawn, h->run_cap);
}
