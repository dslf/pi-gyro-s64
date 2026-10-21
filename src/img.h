#ifndef VIEWER_IMG_H
#define VIEWER_IMG_H

#include <stddef.h>

/* Write a 24 bpp Windows BMP. `rgba` is w*h pixels in GL order (row 0 = bottom row). */
int img_write_bmp(const char* path, int w, int h, const unsigned char* rgba, char* err, size_t errcap);

/* Fraction (0..1) of pixels whose color differs from the bottom-left pixel by more than `tol`. */
double img_changed_fraction(const unsigned char* rgba, int w, int h, int tol);

#endif
