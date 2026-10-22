/* Isolated probe: dump the CPU glyph mask built by src/hud.c's atlas code, so the
   "solid block" rendering can be diagnosed without touching the product. */
#include <windows.h>
#include <stdio.h>

#define FIRST 32
#define COUNT 95

int main(void) {
  HDC dc = CreateCompatibleDC(NULL);
  HFONT font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  HGDIOBJ oldf = SelectObject(dc, font);

  SIZE ext;
  GetTextExtentPoint32W(dc, L"M", 1, &ext);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int cw = ext.cx + 2, ch = tm.tmHeight + 2, cols = 10;
  int rows = (COUNT + cols - 1) / cols;
  int w = cols * cw, h = rows * ch;
  printf("cell %dx%d atlas %dx%d\n", cw, ch, w, h);

  BITMAPINFOHEADER bi;
  memset(&bi, 0, sizeof bi);
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
  void* bits = NULL;
  HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ old = SelectObject(dc, bmp);
  RECT full = { 0, 0, w, h };
  FillRect(dc, &full, GetStockObject(WHITE_BRUSH));
  SetBkColor(dc, RGB(255, 255, 255));
  SetTextColor(dc, RGB(0, 0, 0));
  for (int i = 0; i < COUNT; ++i) {
    wchar_t ch_w = (wchar_t)(FIRST + i);
    TextOutW(dc, (i % cols) * cw, (i / cols) * ch, &ch_w, 1);
  }

  unsigned char* lum = malloc((size_t)w * h);
  const unsigned char* src = (const unsigned char*)bits;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = src + ((size_t)y * w + x) * 4;
      /* BT.601 luminance (weights sum to 100), matching src/hud.c. */
      lum[(size_t)y * w + x] = (unsigned char)((p[2] * 30 + p[1] * 59 + p[0] * 11) / 100);
    }

  printf("first row of the atlas, 40 pixels (lum as #/.): \n");
  for (int y = 0; y < ch; ++y) {
    printf("%2d ", y);
    for (int x = 0; x < 40; ++x) printf("%c", lum[(size_t)y * w + x] < 160 ? '#' : '.');
    printf("\n");
  }
  printf("\ncell masks for p e s o:\n");
  for (int row = 0; row < 1; ++row) {
    for (int col = 0; col < 6; ++col) {
      int gi = row * cols + col;
      printf("'%c' ", (char)(FIRST + gi));
      for (int v = 0; v < ch; ++v) {
        printf("      %2d ", v);
        for (int c = 0; c < cw; ++c) printf("%c", lum[(size_t)((row * ch + v) * w + col * cw + c)] < 160 ? '#' : '.');
        printf("\n");
      }
    }
  }
  free(lum);
  SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); DeleteObject(oldf); DeleteObject(font);
  return 0;
}
