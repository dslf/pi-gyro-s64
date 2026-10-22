#ifndef VIEWER_IMG_H
#define VIEWER_IMG_H

#include <stddef.h>

/* Write a 24 bpp Windows BMP. `rgba` is w*h pixels, one row per image row, with row 0 at the
   TOP of the image (the order fb_read_rgba returns). The file itself is a bottom-up DIB
   (positive height), so the writer emits rows in reverse: the image bottom goes first. */
int img_write_bmp(const char* path, int w, int h, const unsigned char* rgba, char* err, size_t errcap);

/* Fraction (0..1) of pixels whose color differs from the first pixel of `rgba` by more than `tol`. */
double img_changed_fraction(const unsigned char* rgba, int w, int h, int tol);

#endif
