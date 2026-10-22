/* Standalone probe: does img_write_bmp preserve vertical orientation?
   Pure CPU - no GL. Builds a 4x3 image in the img_write_bmp contract (row 0 = top):
     y=0 (top) red, y=1 green, y=2 (bottom) blue.
   Then reads the BMP back with the documented convention (positive height = bottom-up,
   so file row 0 = image bottom) and prints what a viewer would show, top-down.
   Build: gcc -O2 -std=c11 -Wall -Wextra lab/probes/probe_img.c src/img.c -o lab/probes/probe_img.exe -I../../src -I../../ */
#include "img.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 4
#define H 3

int main(void) {
    unsigned char rgba[W * H * 4];
    /* img_write_bmp takes rows top-down (row 0 = image top), the order fb_read_rgba returns.
       Top row red, middle green, bottom blue. */
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            unsigned char* p = rgba + (size_t)y * W * 4 + (size_t)x * 4;
            p[0] = (y == 0) ? 255 : 0;   /* R */
            p[1] = (y == 1) ? 255 : 0;   /* G */
            p[2] = (y == 2) ? 255 : 0;   /* B */
            p[3] = 255;
        }

    char err[256] = {0};
    if (img_write_bmp("lab/probes/probe_img.bmp", W, H, rgba, err, sizeof err) != 0) {
        printf("FAIL write: %s\n", err);
        return 1;
    }

    /* Read the file back: header + pixel rows in file order. */
    FILE* f = fopen("lab/probes/probe_img.bmp", "rb");
    if (!f) { printf("FAIL cannot read back\n"); return 1; }
    unsigned char hdr[54];
    if (fread(hdr, 1, 54, f) != 54) { printf("FAIL short file\n"); fclose(f); return 1; }
    int w = (int)(hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24));
    int h = (int)(hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24));
    int bpp = hdr[28] | (hdr[29] << 8);
    printf("bmp header: %dx%d bpp=%d height_sign=%s\n", w, h, bpp, h >= 0 ? "positive (bottom-up)" : "negative (top-down)");
    const int rowbytes = (w * 3 + 3) & ~3;

    /* File rows in order. For a positive-height BMP, file row 0 is the image BOTTOM row,
       so a viewer shows file rows in reverse: last file row = image top. */
    unsigned char row[16];
    for (int fr = 0; fr < h; ++fr) {
        if (fread(row, 1, (size_t)rowbytes, f) != (size_t)rowbytes) { printf("FAIL short rows\n"); fclose(f); return 1; }
        /* BMP pixel bytes are B,G,R in that order; file row 0 of a positive-height BMP is the
           image BOTTOM row, so it is displayed at top-down index h-1-fr. */
        printf("file row %d -> rgb %d,%d,%d, displayed at top-down index %d\n", fr, row[2], row[1], row[0], h - 1 - fr);
    }
    fclose(f);

    /* What a viewer shows top-down: file row h-1 first (top), ..., file row 0 last (bottom). */
    printf("displayed top-down:");
    f = fopen("lab/probes/probe_img.bmp", "rb");
    fseek(f, 54, SEEK_SET);
    unsigned char rows[H][16];
    for (int fr = 0; fr < h; ++fr)
        if (fread(rows[fr], 1, (size_t)rowbytes, f) != (size_t)rowbytes) { printf("FAIL short rows\n"); fclose(f); return 1; }
    fclose(f);
    for (int d = 0; d < h; ++d) {
        const unsigned char* r = rows[h - 1 - d];
        printf(" [%d,%d,%d]", r[2], r[1], r[0]);
    }
    printf("\n");
    int fails = 0;
    /* Expected display: top = red (255,0,0), middle = green (0,255,0), bottom = blue (0,0,255).
       BMP bytes are BGR, so red means bytes [0]=0,[1]=0,[2]=255. */
    if (rows[h - 1][2] == 255 && rows[h - 1][1] == 0 && rows[h - 1][0] == 0)
        printf("ok   displayed top row is red\n");
    else { printf("FAIL displayed top row is rgb %d,%d,%d - expected red 255,0,0\n", rows[h - 1][2], rows[h - 1][1], rows[h - 1][0]); ++fails; }
    if (rows[h / 2][1] == 255) printf("ok   displayed middle row is green\n");
    else { printf("FAIL displayed middle row is rgb %d,%d,%d - expected green\n", rows[h / 2][2], rows[h / 2][1], rows[h / 2][0]); ++fails; }
    if (rows[0][0] == 255) printf("ok   displayed bottom row is blue\n");
    else { printf("FAIL displayed bottom row is rgb %d,%d,%d - expected blue\n", rows[0][2], rows[0][1], rows[0][0]); ++fails; }

    printf("probe: %s\n", fails ? "FAILED" : "PASS");
    return fails ? 1 : 0;
}
