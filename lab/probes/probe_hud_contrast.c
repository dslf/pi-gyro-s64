/* Probe: HUD legibility math, no GL. Builds a GDI glyph mask at two font sizes and
   composites it in CPU over a dark panel at several scene luminances, then reports
   WCAG contrast ratios. Acceptance: ink vs panel >= 4.5, panel vs bright scene >= 2.5,
   and the glyph ink profile must stay structured (not flat). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>

static float srgb_lin(float c) { /* c in 0..1 gamma space -> linear luminance */
  return c > 0.03928f ? powf((c + 0.055f) / 1.055f, 2.4f) : c / 12.92f;
}

static float contrast(float a, float b) { /* luminance ratio, a assumed brighter */
  float la = srgb_lin(a / 255.0f), lb = srgb_lin(b / 255.0f);
  if (la < lb) { float t = la; la = lb; lb = t; }
  return (la + 0.05f) / (lb + 0.05f);
}

/* Premultiplied composite: panel first (dark, alpha A), then ink (fg * i over the panel). */
static float composite(float scene, float ink, float panel_alpha, float fg) {
  float panel = scene * (1.0f - panel_alpha);
  return fg * ink + panel * (1.0f - ink);
}

static int build_mask(int font_px, unsigned char** lum, int* w, int* h, int* cw, int* ch) {
  HDC dc = CreateCompatibleDC(NULL);
  if (!dc) { fprintf(stderr, "CreateCompatibleDC failed\n"); return -1; }
  HFONT font = CreateFontW(-font_px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                           FIXED_PITCH | FF_DONTCARE, L"Consolas");
  if (!font) { fprintf(stderr, "CreateFontW failed\n"); DeleteDC(dc); return -1; }
  SelectObject(dc, font);
  SIZE ext;
  if (!GetTextExtentPoint32W(dc, L"M", 1, &ext)) { fprintf(stderr, "extent failed\n"); goto fail; }
  TEXTMETRICW tm;
  if (!GetTextMetricsW(dc, &tm)) { fprintf(stderr, "metrics failed\n"); goto fail; }
  int cols = 10;
  *cw = ext.cx + 2;
  *ch = tm.tmHeight + 2;
  *w = cols * *cw;
  *h = (*ch) * 10; /* ten rows is enough for the glyphs we probe */

  BITMAPINFOHEADER bi;
  memset(&bi, 0, sizeof bi);
  bi.biSize = sizeof bi;
  bi.biWidth = *w;
  bi.biHeight = -*h;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  void* bits = NULL;
  HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bmp || !bits) { fprintf(stderr, "CreateDIBSection failed\n"); goto fail; }
  HGDIOBJ old = SelectObject(dc, bmp);
  RECT full = { 0, 0, *w, *h };
  FillRect(dc, &full, GetStockObject(WHITE_BRUSH));
  for (int i = 0; i < 95; ++i) {
    wchar_t glyph = (wchar_t)(32 + i);
    TextOutW(dc, (i % cols) * *cw, (i / cols) * *ch, &glyph, 1);
  }
  unsigned char* out = malloc((size_t)(*w) * *h);
  const unsigned char* src = (const unsigned char*)bits;
  for (int y = 0; y < *h; ++y)
    for (int x = 0; x < *w; ++x) {
      const unsigned char* p = src + ((size_t)y * *w + x) * 4;
      out[(size_t)y * *w + x] = (unsigned char)((p[2] * 30 + p[1] * 59 + p[0] * 11) / 100);
    }
  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
  DeleteObject(font);
  *lum = out;
  return 0;
fail:
  DeleteDC(dc);
  DeleteObject(font);
  return -1;
}

static void report_size(int font_px, float panel_alpha, float fg) {
  unsigned char* lum = NULL;
  int w = 0, h = 0, cw = 0, ch = 0;
  if (build_mask(font_px, &lum, &w, &h, &cw, &ch) != 0) return;
  printf("font %d px: cell %dx%d, atlas %dx%d\n", font_px, cw, ch, w, h);

  for (int gi = 0; gi < 95; ++gi) {
    int gc = gi % 10, gr = gi / 10;
    if (gr >= 10) break;
    int dark = 0, flat_rows = 0;
    float mx = 0.0f, mn = 1.0f;
    for (int v = 0; v < ch; ++v) {
      int ink_px = 0;
      for (int c = 0; c < cw; ++c) {
        float ink = (255.0f - (float)lum[(size_t)gr * ch * w + gc * cw + c]) / 255.0f;
        if (ink > 0.02f) ink_px++;
      }
      float frac = (float)ink_px / (float)cw;
      dark += ink_px;
      if (frac > mx) mx = frac;
      if (frac < mn) mn = frac;
      if (frac > 0.90f) flat_rows++;
    }
    char chch = (char)(32 + gi);
    if (chch == 'p' || chch == 'W' || chch == 'M')
      printf("  glyph '%c' ink %.1f%% of cell, row ink %.0f%%..%.0f%%, flat rows %d/%d\n",
             chch, 100.0 * dark / (float)(cw * ch), 100.0 * mn, 100.0 * mx, flat_rows, ch);
  }

  /* Ink core (ink=1) and edge (ink=0.35) against the panel, over several scene levels. */
  const float scenes[] = { 255.0f, 160.0f, 90.0f, 40.0f };
  for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; ++i) {
    float s = scenes[i];
    float panel = s * (1.0f - panel_alpha);
    float core = composite(s, 1.0f, panel_alpha, fg);
    float edge = composite(s, 0.35f, panel_alpha, fg);
    printf("  scene %.0f: panel %.0f (CR %.2f vs scene), ink core %.0f (CR %.2f vs panel), edge %.0f (CR %.2f vs panel)\n",
           s, panel, contrast(panel, s), core, contrast(core, panel), edge, contrast(edge, panel));
  }
  free(lum);
}

int main(void) {
  printf("probe HUD contrast: panel alpha 0.72, fg 245\n");
  report_size(14, 0.72f, 245.0f);
  printf("\nprobe HUD contrast: panel alpha 0.78, fg 250 (larger glyphs)\n");
  report_size(20, 0.78f, 250.0f);
  printf("\ncontrol: no panel, dark ink on bright scene\n");
  {
    float s = 200.0f;
    float ink = composite(s, 1.0f, 0.0f, 0.0f);
    printf("  scene %.0f, dark ink %.0f, CR %.2f\n", s, ink, contrast(s, ink));
  }
  return 0;
}
