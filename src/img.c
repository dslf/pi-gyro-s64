#include "img.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int put16(FILE* f, unsigned v) {
  unsigned char b[2] = { (unsigned char)(v & 255), (unsigned char)((v >> 8) & 255) };
  return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int put32(FILE* f, unsigned v) {
  unsigned char b[4] = {
    (unsigned char)(v & 255), (unsigned char)((v >> 8) & 255),
    (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 24) & 255)
  };
  return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

int img_write_bmp(const char* path, int w, int h, const unsigned char* rgba, char* err, size_t errcap) {
  if (w <= 0 || h <= 0 || !rgba) { snprintf(err, errcap, "bad image size %dx%d", w, h); return -1; }

  const int rowbytes = (w * 3 + 3) & ~3;
  const size_t image = (size_t)rowbytes * (size_t)h;

  FILE* f = fopen(path, "wb");
  if (!f) { snprintf(err, errcap, "cannot write '%s'", path); return -1; }

  int rc = 0;
  if (fwrite("BM", 1, 2, f) != 2) rc = -1;   /* magic: 'B','M' in that byte order */
  rc |= put32(f, 54u + image);       /* file size */
  rc |= put32(f, 0);                 /* reserved */
  rc |= put32(f, 54u);               /* pixel data offset */
  rc |= put32(f, 40u);               /* info header size */
  rc |= put32(f, (unsigned)w);
  rc |= put32(f, (unsigned)h);     /* positive height = bottom-up rows */
  rc |= put16(f, 1u);              /* planes */
  rc |= put16(f, 24u);             /* bits per pixel */
  rc |= put32(f, 0u);              /* BI_RGB */
  rc |= put32(f, (unsigned)image);
  rc |= put32(f, 0u);
  rc |= put32(f, 0u);
  rc |= put32(f, 0u);
  rc |= put32(f, 0u);

  unsigned char* row = malloc((size_t)rowbytes);
  if (!row) { fclose(f); snprintf(err, errcap, "out of memory"); return -1; }

  for (int y = h - 1; y >= 0; y--) {
    const unsigned char* src = rgba + (size_t)y * (size_t)w * 4;
    for (int x = 0; x < w; x++) {
      row[x * 3 + 0] = src[x * 4 + 2];   /* B */
      row[x * 3 + 1] = src[x * 4 + 1];   /* G */
      row[x * 3 + 2] = src[x * 4 + 0];   /* R */
    }
    for (int p = w * 3; p < rowbytes; p++) row[p] = 0;
    if (fwrite(row, 1, (size_t)rowbytes, f) != (size_t)rowbytes) { rc = -1; break; }
  }
  free(row);
  fclose(f);

  if (rc != 0) { snprintf(err, errcap, "failed to write '%s'", path); return -1; }
  return 0;
}

double img_changed_fraction(const unsigned char* rgba, int w, int h, int tol) {
  if (w <= 0 || h <= 0 || !rgba) return 0.0;
  const unsigned char* base = rgba;
  int changed = 0;
  for (int i = 0; i < w * h; i++) {
    int dr = (int)rgba[i * 4 + 0] - (int)base[0];
    int dg = (int)rgba[i * 4 + 1] - (int)base[1];
    int db = (int)rgba[i * 4 + 2] - (int)base[2];
    if (dr * dr + dg * dg + db * db > tol * tol) changed++;
  }
  return (double)changed / (double)(w * h);
}
