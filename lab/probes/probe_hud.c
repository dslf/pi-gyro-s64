/* Lab probe (discovery only, not product code): can a HUD be built with zero extra
   dependencies? GDI rasterizes the glyph atlas, GL draws text quads in pixel space,
   the frame is dumped to BMP and measured with the same helpers the tests use. */

#include "../../src/gl.h"
#include "../../src/img.h"

/* Not in src/gl.h yet: this prototype asks for a uniform buffer. Whether the product
   loader gains it is a separate decision (see the probe report). */
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A2F
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define FIRST_GLYPH 32
#define GLYPH_COUNT 95 /* 32..126 */

typedef struct {
    int cw, ch, cols, rows, w, h;
    unsigned char* rgba;      /* grayscale RGB, top-down - documentation/ASCII dump only */
    unsigned char* lum;       /* top-down luminance per pixel - the glyph mask used for geometry */
    float ink[GLYPH_COUNT];   /* 0..1 dark-pixel fraction inside each cell */
} atlas;

/* GDI rasterizes ASCII into a fixed-cell atlas. Cell size is measured, so the
   result is correct at any display DPI instead of assuming a 96 dpi font. */
static int build_atlas(atlas* a, char* err, size_t errcap) {
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) { snprintf(err, errcap, "CreateCompatibleDC failed"); return -1; }

    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                             FIXED_PITCH | FF_DONTCARE, L"Consolas");
    if (!font) { snprintf(err, errcap, "CreateFontW failed"); DeleteDC(dc); return -1; }
    SelectObject(dc, font);

    SIZE ext;
    if (!GetTextExtentPoint32W(dc, L"M", 1, &ext)) { snprintf(err, errcap, "GetTextExtentPoint32W failed"); goto fail; }
    TEXTMETRICW tm;
    if (!GetTextMetricsW(dc, &tm)) { snprintf(err, errcap, "GetTextMetricsW failed"); goto fail; }

    a->cw = ext.cx + 2;
    a->ch = tm.tmHeight + 2;
    a->cols = 10;
    a->rows = (GLYPH_COUNT + a->cols - 1) / a->cols;
    a->w = a->cols * a->cw;
    a->h = a->rows * a->ch;

    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = a->w;
    bi.biHeight = -a->h;            /* top-down DIB */
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { snprintf(err, errcap, "CreateDIBSection failed"); goto fail; }

    HGDIOBJ old = SelectObject(dc, bmp);
    HBRUSH white = GetStockObject(WHITE_BRUSH);
    RECT full = { 0, 0, a->w, a->h };
    FillRect(dc, &full, white);
    SetBkColor(dc, RGB(255, 255, 255));
    SetTextColor(dc, RGB(0, 0, 0));

    for (int i = 0; i < GLYPH_COUNT; ++i) {
        wchar_t ch = (wchar_t)(FIRST_GLYPH + i);
        int col = i % a->cols, row = i / a->cols;
        TextOutW(dc, col * a->cw, row * a->ch, &ch, 1);
    }

    /* Copy the DIB into top-down grayscale, and count ink per cell from the same copy. */
    a->rgba = malloc((size_t)a->w * a->h * 3);
    a->lum = malloc((size_t)a->w * a->h);
    if (!a->rgba || !a->lum) {
        snprintf(err, errcap, "out of memory");
        SelectObject(dc, old); DeleteObject(bmp); goto fail;
    }

    const unsigned char* src = (const unsigned char*)bits;
    for (int y = 0; y < a->h; ++y) {
        for (int x = 0; x < a->w; ++x) {
            const unsigned char* p = src + ((size_t)y * a->w + x) * 4; /* BGRA */
            unsigned char* q = a->rgba + ((size_t)y * a->w + x) * 3;
            int lum = (p[2] * 59 + p[1] * 20 + p[0] * 11) / 100;       /* RGB weights on RGB channels */
            q[0] = q[1] = q[2] = (unsigned char)lum;
            a->lum[(size_t)y * a->w + x] = (unsigned char)lum;
        }
    }

    for (int i = 0; i < GLYPH_COUNT; ++i) {
        int col = i % a->cols, row = i / a->cols;
        int dark = 0;
        for (int y = row * a->ch; y < (row + 1) * a->ch; ++y)
            for (int x = col * a->cw; x < (col + 1) * a->cw; ++x)
                if (a->lum[(size_t)y * a->w + x] < 160) dark++;
        a->ink[i] = (float)dark / (float)(a->cw * a->ch);
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

static const char* HUD_VERT =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "out vec4 vColor;\n"
    "void main(){\n"
    "  vColor = aColor;\n"
    "  gl_Position = vec4(aPos.x / 512.0 * 2.0 - 1.0, 1.0 - aPos.y * 2.0 / 96.0, 0.0, 1.0);\n"
    "}\n";

/* No texture, no sampler, no uniforms: glyph ink comes from the CPU atlas mask as small
   rectangles, color as a vertex attribute (premultiplied rgb = fg * ink, alpha = ink).
   This probe's dispatch layer misroutes uniform uploads and collapses UVs (probe notes),
   so the HUD is drawn as geometry - the same attribute path the product already uses. */
static const char* HUD_FRAG =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vColor; }\n";

static GLuint compile(GLenum type, const char* src, char* err, size_t errcap) {
    GLuint sh = my_glCreateShader(type);
    my_glShaderSource(sh, 1, &src, NULL);
    my_glCompileShader(sh);
    GLint ok = 0;
    my_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; GLint len = 0;
        my_glGetShaderInfoLog(sh, sizeof log, &len, log);
        snprintf(err, errcap, "shader compile: %s", log);
        return 0;
    }
    return sh;
}

/* Draw `s` at pixel (x, y), one cell per character. UVs are computed in the
   bottom-up texture space, because the atlas rows were flipped on upload.
   Positions and UVs live in two separate buffers, so both attribute streams use
   offset 0 - no interleaved byte offset is involved (the offset argument is the
   suspect for the flat glyph quads, see the row-ink assertion below).
   One draw call per string: 6 vertices per glyph. */
static void bind_stream(GLuint buf, GLuint loc, GLsizei stride) {
    my_glBindBuffer(GL_ARRAY_BUFFER, buf);
    my_glVertexAttribPointer(loc, stride == 2 * sizeof(float) ? 2 : 4, GL_FLOAT, GL_FALSE, stride,
                            (const void*)0);
    my_glEnableVertexAttribArray(loc);
}

/* Draw `s` at pixel (x, y): every glyph becomes horizontal ink runs taken from the CPU
   mask, each run a rectangle with a premultiplied color attribute. One draw call per
   string. Runs keep the vertex count small (a glyph is a handful of runs, not 228 pixels). */
static void draw_text(const char* s, int x, int y, const atlas* a, GLuint posVbo, GLuint colVbo,
                      const float fg[3]) {
    const int cap = 4096;                      /* runs per string; 6 vertices each */
    float pos[6 * 2 * 4096], col[6 * 4 * 4096];
    int n = 0, runs = 0, drawn = 0;
    for (const char* p = s; *p; ++p, x += a->cw, ++drawn) {
        int gi = (unsigned char)*p - FIRST_GLYPH;
        if (gi < 0 || gi >= GLYPH_COUNT) continue;
        int gc = gi % a->cols, gr = gi / a->cols;
        for (int v = 0; v < a->ch; ++v) {
            int run_start = -1; float run_ink = 0.0f;
            for (int c = 0; c <= a->cw; ++c) {
                float ink = 0.0f;
                if (c < a->cw) ink = (255.0f - (float)a->lum[(size_t)(gr * a->ch + v) * a->w + gc * a->cw + c]) / 255.0f;
                if (ink > 0.02f) {
                    if (run_start < 0) { run_start = c; run_ink = ink; }
                    else if (ink > run_ink) run_ink = ink;
                    continue;
                }
                if (run_start >= 0) {
                    if (runs >= cap) { p = s + strlen(s); break; }   /* stop, report below */
                    float px = (float)(x + run_start), py = (float)(y + v);
                    float qx = px + (float)(c - run_start), qy = py + 1.0f;
                    float r = fg[0] * run_ink, g = fg[1] * run_ink, b = fg[2] * run_ink;
                    for (int k = 0; k < 6; ++k) {
                        int corner = k;                            /* TL, TR, BR, TL, BR, BL */
                        float cx = (corner == 0 || corner == 3 || corner == 5) ? px : qx;
                        float cy = (corner == 0 || corner == 3) ? py : qy;
                        pos[n * 2 + 0] = cx; pos[n * 2 + 1] = cy;
                        col[n * 4 + 0] = r; col[n * 4 + 1] = g; col[n * 4 + 2] = b; col[n * 4 + 3] = run_ink;
                        n++;
                    }
                    runs++;
                    run_start = -1; run_ink = 0.0f;
                }
            }
            if (runs >= cap) break;
        }
        if (runs >= cap) break;
    }
    my_glBindBuffer(GL_ARRAY_BUFFER, posVbo);
    my_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 2 * sizeof(float)), pos, GL_DYNAMIC_DRAW);
    my_glBindBuffer(GL_ARRAY_BUFFER, colVbo);
    my_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 4 * sizeof(float)), col, GL_DYNAMIC_DRAW);
    bind_stream(posVbo, 0, 2 * sizeof(float));
    bind_stream(colVbo, 1, 4 * sizeof(float));
    my_glDrawArrays(GL_TRIANGLES, 0, n);
    if (runs >= cap) printf("note: string truncated after %d ink runs (buffer capacity)\n", runs);
    if (s[drawn]) printf("note: string truncated after %d glyphs (buffer capacity)\n", drawn);
}

static float row_ink(const unsigned char* rgba, int w, int h, int y_top) {
    /* rgba is bottom-up, so a top-down row y is stored at row h-1-y. */
    const unsigned char* row = rgba + ((size_t)(h - 1 - y_top) * w) * 4;
    int dark = 0;
    for (int x = 0; x < w; ++x) {
        float d = fabsf((float)row[x * 4 + 0] - 30.0f) + fabsf((float)row[x * 4 + 1] - 32.0f) + fabsf((float)row[x * 4 + 2] - 36.0f);
        if (d > 30.0f) dark++;
    }
    return dark / (float)w;
}

static float cell_ink(const unsigned char* rgba, int w, int h, int x0, int cw, int y_top, int ch) {
    int dark = 0;
    for (int y = y_top; y < y_top + ch; ++y)
        for (int x = x0; x < x0 + cw; ++x) {
            const unsigned char* p = rgba + ((size_t)(h - 1 - y) * w + x) * 4;
            float d = fabsf((float)p[0] - 30.0f) + fabsf((float)p[1] - 32.0f) + fabsf((float)p[2] - 36.0f);
            if (d > 30.0f) dark++;
        }
    return dark / (float)(cw * ch);
}

/* Per-row ink counts inside one glyph cell: a real glyph has rows with very
   different ink (strokes), a flat slab has every row identical. */
static void cell_row_profile(const unsigned char* px, int w, int h, int x0, int cw, int y_top, int ch,
                             float* lo, float* hi) {
    float mn = 1.0f, mx = 0.0f;
    for (int y = y_top; y < y_top + ch && y < h; ++y) {
        int dark = 0;
        for (int x = x0; x < x0 + cw && x < w; ++x) {
            const unsigned char* p = px + ((size_t)(h - 1 - y) * w + x) * 4;
            float dev = fabsf((float)p[0] - 30.0f) + fabsf((float)p[1] - 32.0f) + fabsf((float)p[2] - 36.0f);
            if (dev > 30.0f) dark++;
        }
        float v = dark / (float)cw;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    *lo = mn; *hi = mx;
}

/* Profile one glyph cell of a rendered frame: fraction of light pixels (glyph
   ink), fraction of pixels that stay at the HUD background, and the brightest
   deviation. A cell that comes out as a solid slab fails the background share. */
static void cell_profile(const unsigned char* px, int w, int h, int x0, int cw, int y_top, int ch,
                         float* ink, float* bg_share, float* peak_dev) {
    float dev_sum = 0.0f;
    int bg = 0, ink_px = 0, n = 0;
    *ink = 0.0f; *bg_share = 0.0f; *peak_dev = 0.0f;
    for (int y = y_top; y < y_top + ch && y < h; ++y) {
        for (int x = x0; x < x0 + cw && x < w; ++x, ++n) {
            const unsigned char* p = px + ((size_t)(h - 1 - y) * w + x) * 4;
            float dev = fabsf((float)p[0] - 30.0f) + fabsf((float)p[1] - 32.0f) + fabsf((float)p[2] - 36.0f);
            if (dev > 60.0f) ink_px++;   /* glyph ink, including antialiased edges */
            if (dev < 8.0f) bg++;
            if (dev > *peak_dev) *peak_dev = dev;
            dev_sum += dev;
        }
    }
    if (n == 0) return;
    *ink = (float)ink_px / (float)n;
    *bg_share = (float)bg / (float)n;
    dev_sum /= (float)n;   /* mean deviation, reported alongside ink */
    if (dev_sum > 255.0f) dev_sum = 255.0f;
    printf("mean deviation %.0f\n", dev_sum);
}

/* Print glyph cells as ASCII art so legibility can be judged without an image viewer.
   '#' = strong ink, '.' = antialias edge. The mask is top-down, so rows print top-first. */
static void dump_glyphs_ascii(const atlas* a, const char* chars, int n) {
    printf("glyph patterns (%dx%d cells, antialiased):\n", a->cw, a->ch);
    for (int v = 0; v < a->ch; ++v) {
        for (int i = 0; i < n; ++i) {
            int g = (unsigned char)chars[i] - FIRST_GLYPH;
            int col = g % a->cols, row = g / a->cols;
            for (int c = 0; c < a->cw; ++c) {
                int lum = a->lum[(size_t)(row * a->ch + v) * a->w + col * a->cw + c];
                float ink = (255 - lum) / 255.0f;
                putchar(ink > 0.5f ? '#' : (ink > 0.2f ? '.' : ' '));
            }
            putchar('|');
        }
        putchar('\n');
    }
    printf("  ");
    for (int i = 0; i < n; ++i) printf("%c%10s", chars[i], "|");
    putchar('\n');
}

/* Per-row ink fraction of one glyph in the CPU mask, top-down rows. */
static void mask_profile(const atlas* a, int gi, float* prof) {
    int col = gi % a->cols, row = gi / a->cols;
    for (int v = 0; v < a->ch; ++v) {
        int dark = 0;
        for (int c = 0; c < a->cw; ++c)
            if ((255.0f - (float)a->lum[(size_t)(row * a->ch + v) * a->w + col * a->cw + c]) > 60.0f) dark++;
        prof[v] = (float)dark / (float)a->cw;
    }
}

/* Same profile read out of a rendered frame, top-down rows; px is bottom-up readback. */
static void frame_profile(const unsigned char* px, int w, int h, int x0, int y_top, int cw, int ch, float* prof) {
    for (int v = 0; v < ch; ++v) {
        int y = y_top + v;
        if (y >= h) { prof[v] = 0.0f; continue; }
        int dark = 0;
        for (int x = x0; x < x0 + cw && x < w; ++x) {
            const unsigned char* p = px + ((size_t)(h - 1 - y) * w + x) * 4;
            float dev = fabsf((float)p[0] - 30.0f) + fabsf((float)p[1] - 32.0f) + fabsf((float)p[2] - 36.0f);
            if (dev > 30.0f) dark++;
        }
        prof[v] = (float)dark / (float)cw;
    }
}

static int pixels_differing(const unsigned char* a, const unsigned char* b, int w, int h, int x0, int x1, int tol) {
    int n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = x0; x < x1; ++x) {
            const unsigned char* p = a + ((size_t)y * w + x) * 4;
            const unsigned char* q = b + ((size_t)y * w + x) * 4;
            if (abs((int)p[0] - (int)q[0]) > tol || abs((int)p[1] - (int)q[1]) > tol || abs((int)p[2] - (int)q[2]) > tol) n++;
        }
    return n;
}

/* readPixels hands back bottom-up rows; img_write_bmp takes top-down rows, so the
   saved frame must be flipped or the HUD lands at the bottom of the image. */
static void flip_rows(unsigned char* buf, int w, int h) {
    const size_t row = (size_t)w * 4;
    unsigned char tmp[512 * 4];
    for (int y = 0; y < h / 2; ++y) {
        memcpy(tmp, buf + (size_t)y * row, row);
        memcpy(buf + (size_t)y * row, buf + (size_t)(h - 1 - y) * row, row);
        memcpy(buf + (size_t)(h - 1 - y) * row, tmp, row);
    }
}

static int fails = 0;
static void Ok(const char* fmt, ...) { printf("ok   "); va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }
static void Fail(const char* fmt, ...) { fails++; printf("FAIL "); va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }

int main(void) {
    setbuf(stdout, NULL);   /* keep the log even if a GL call crashes the process */
    atlas a;
    memset(&a, 0, sizeof a);
    char err[256] = "";
    clock_t t0 = clock();
    if (build_atlas(&a, err, sizeof err) < 0) { printf("FAIL atlas build: %s\n", err); return 1; }
    double build_ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf("atlas %dx%d, cell %dx%d, %d glyphs, build %.2f ms\n", a.w, a.h, a.cw, a.ch, GLYPH_COUNT, build_ms);

    /* Every printable glyph must actually rasterize - a blank cell means the font
       or the cell metrics are wrong, which would silently produce an empty HUD. */
    int blank = 0;
    float min_ink = 1.0f;
    for (int i = 0; i < GLYPH_COUNT; ++i) {
        char ch = (char)(FIRST_GLYPH + i);
        if (ch == ' ') continue;
        if (a.ink[i] < 0.02f) { blank++; printf("  blank glyph '%c'\n", ch); }
        if (a.ink[i] < min_ink) min_ink = a.ink[i];
    }
    if (blank) Fail("%d glyphs rasterized blank", blank); else Ok("all %d non-space glyphs have ink (min %.3f)", GLYPH_COUNT - 1, min_ink);
    dump_glyphs_ascii(&a, "013469caf", 9);
    fflush(stdout);

    if (!glfwInit()) { printf("FAIL glfwInit\n"); return 1; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    /* Matches the product context. A 4.6 core context makes the loader's GL 1.1 fallback
       (opengl32.dll exports) address a different implementation, so attribute state reads
       as zeros there - see the probe notes. Shaders stay at #version 460 where supported. */
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "hud probe", NULL, NULL);
    if (!win) {
        const char* desc = NULL;
        int e = glfwGetError(&desc);
        printf("FAIL window: error %d (%s)\n", e, desc ? desc : "no description");
        return 1;
    }
    glfwMakeContextCurrent(win);
    if (gl_load(glfwGetProcAddress, err, sizeof err) < 0) { printf("FAIL gl_load: %s\n", err); return 1; }
    char ver[128], rend[128];
    printf("gl: %s / %s\n", gl_version_string(ver, sizeof ver), gl_renderer_string(rend, sizeof rend));
    printf("vendor: %s\n", (const char*)my_glGetString(GL_VENDOR));
    {
        const char* ext = (const char*)my_glGetString(GL_EXTENSIONS);
        printf("has GL_ARB_uniform_buffer_object: %s\n", ext && strstr(ext, "GL_ARB_uniform_buffer_object") ? "yes" : "no");
        printf("has GL_ARB_vertex_buffer_object: %s\n", ext && strstr(ext, "GL_ARB_vertex_buffer_object") ? "yes" : "no");
        HMODULE ms = GetModuleHandleA("opengl32.dll");
        printf("opengl32.dll module: %p, glBindBuffer ptr %p, glBindBufferBase ptr %p\n",
               (void*)ms, (void*)my_glBindBuffer, (void*)glfwGetProcAddress("glBindBufferBase"));
    }
    printf("window attribs: %d.%d profile=%d (core=%d compat=%d) fb+srgb=%d\n",
           glfwGetWindowAttrib(win, GLFW_CONTEXT_VERSION_MAJOR),
           glfwGetWindowAttrib(win, GLFW_CONTEXT_VERSION_MINOR),
           glfwGetWindowAttrib(win, GLFW_OPENGL_PROFILE),
           GLFW_OPENGL_CORE_PROFILE,
           GLFW_OPENGL_COMPAT_PROFILE,
           glfwGetWindowAttrib(win, GLFW_SRGB_CAPABLE));

    /* HUD colors: the background is the clear color, the glyph ink comes from vertex
       attributes as premultiplied rgb = fg * ink, alpha = ink. */
    float bg[3] = { 30.0f / 255.0f, 32.0f / 255.0f, 36.0f / 255.0f };
    float fg[3] = { 235.0f / 255.0f, 240.0f / 255.0f, 246.0f / 255.0f };
    printf("hud colors: bg=%.4f,%.4f,%.4f fg=%.4f,%.4f,%.4f (ink via vertex attributes)\n",
           bg[0], bg[1], bg[2], fg[0], fg[1], fg[2]);
    Ok("glyph mask ready: %dx%d luminance, top-down", a.w, a.h);

    GLuint vs = compile(GL_VERTEX_SHADER, HUD_VERT, err, sizeof err);
    GLuint fs = compile(GL_FRAGMENT_SHADER, HUD_FRAG, err, sizeof err);
    if (!vs || !fs) { printf("FAIL shader: %s\n", err); return 1; }
    GLuint prog = my_glCreateProgram();
    my_glAttachShader(prog, vs);
    my_glAttachShader(prog, fs);
    my_glLinkProgram(prog);
    GLint ok = 0;
    my_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { printf("FAIL link\n"); return 1; }
    Ok("hud program linked (color comes from vertex attribute location 1, no sampler)");

    /* One VAO, four buffers: each text row gets its own position and color stream, so a
       frame never re-uploads the same buffer twice and no attribute needs a byte offset. */
    GLuint vao = 0, vbo = 0, vbo_col = 0, vbo2 = 0, vbo2_col = 0;
    my_glGenVertexArrays(1, &vao);
    my_glBindVertexArray(vao);
    my_glGenBuffers(1, &vbo);
    my_glGenBuffers(1, &vbo_col);
    my_glGenBuffers(1, &vbo2);
    my_glGenBuffers(1, &vbo2_col);
    Ok("hud vao + 4 attribute buffers (pos/color per row)");

    /* A second pair of buffers for the second row - two glBufferData calls on one
       buffer inside a single frame was the earlier suspect for the empty row. */

    const int W = 512, H = 96;
    GLuint fbo = 0, rb = 0;
    my_glGenFramebuffers(1, &fbo);
    my_glGenRenderbuffers(1, &rb);
    my_glBindRenderbuffer(GL_RENDERBUFFER, rb);
    my_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    my_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    my_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    if (my_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("FAIL fbo\n"); return 1; }

    /* Channel-order check: a known clear color must come back as RGBA, not BGR. */
    my_glViewport(0, 0, W, H);
    my_glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    my_glClear(GL_COLOR_BUFFER_BIT);
    my_glFinish();
    unsigned char kp[4] = {0, 0, 0, 0};
    my_glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, kp);
    printf("known red read back as %d,%d,%d,%d\n", kp[0], kp[1], kp[2], kp[3]);
    if (kp[0] != 255 || kp[1] != 0 || kp[2] != 0 || kp[3] != 255) {
        printf("FAIL readback channel order: expected 255,0,0,255 got %d,%d,%d,%d\n", kp[0], kp[1], kp[2], kp[3]);
        return 1;
    }

    /* Attribute-color check: a quad spanning the whole frame with known per-corner RGBA
       must come back pixel-exact through the same program. This is the path the HUD uses
       now (glyph ink as a color attribute), so channels and orientation are proven here. */
    {
        /* Drain anything pending from earlier setup so the per-call error prints below
           report only the call they follow. */
        while (my_glGetError() != GL_NO_ERROR) {}
        printf("loader pointers: vertexAttribPointer=%p enableAttrib=%p useProgram=%p drawArrays=%p bindVAO=%p bufferData=%p\n",
               (void*)my_glVertexAttribPointer, (void*)my_glEnableVertexAttribArray, (void*)my_glUseProgram,
               (void*)my_glDrawArrays, (void*)my_glBindVertexArray, (void*)my_glBufferData);
        printf("raw pointers: vertexAttribPointer=%p enableAttrib=%p useProgram=%p drawArrays=%p bindVAO=%p bufferData=%p\n",
               glfwGetProcAddress("glVertexAttribPointer"), glfwGetProcAddress("glEnableVertexAttribArray"),
               glfwGetProcAddress("glUseProgram"), glfwGetProcAddress("glDrawArrays"),
               glfwGetProcAddress("glBindVertexArray"), glfwGetProcAddress("glBufferData"));
        /* Corner colors: top-left white, top-right green, bottom-left blue, bottom-right red. */
        float pos[12] = { 0,0, 512,0, 512,96, 0,0, 512,96, 0,96 };
        float col[24] = {
            255,255,255,255,   0,255,0,255,   255,0,0,255,
            255,255,255,255,   255,0,0,255,   0,0,255,255,
        };
        GLuint pbuf = 0, cbuf = 0;
        my_glGenBuffers(1, &pbuf);
        my_glGenBuffers(1, &cbuf);
        my_glBindBuffer(GL_ARRAY_BUFFER, pbuf);
        my_glBufferData(GL_ARRAY_BUFFER, sizeof pos, pos, GL_STATIC_DRAW);
        printf("bufferData pos (%zu bytes) err=0x%x\n", sizeof pos, (unsigned)my_glGetError());
        my_glBindBuffer(GL_ARRAY_BUFFER, cbuf);
        my_glBufferData(GL_ARRAY_BUFFER, sizeof col, col, GL_STATIC_DRAW);
        printf("bufferData col (%zu bytes) err=0x%x\n", sizeof col, (unsigned)my_glGetError());
        printf("buffer names pos=%u color=%u prog=%u\n", pbuf, cbuf, prog);
        bind_stream(pbuf, 0, 2 * sizeof(float));
        printf("after attrib pos err=0x%x\n", (unsigned)my_glGetError());
        bind_stream(cbuf, 1, 4 * sizeof(float));
        printf("after attrib color err=0x%x\n", (unsigned)my_glGetError());
        {
            typedef void (*fn_getiv)(GLenum, GLint*);
            typedef void (*fn_getvattrib)(GLuint, GLenum, GLint*);
            fn_getiv getiv = (fn_getiv)glfwGetProcAddress("glGetIntegerv");
            fn_getvattrib gav = (fn_getvattrib)glfwGetProcAddress("glGetVertexAttribiv");
            if (!getiv || !gav) { printf("note: glGetIntegerv / glGetVertexAttribiv unresolved\n"); }
            else {
                int ab = 0, v0buf = 0, v1buf = 0, v0size = 0, v1size = 0, v0stride = 0, v1stride = 0, v0type = 0, v1type = 0;
                getiv(0x8892 /* GL_ARRAY_BUFFER_BINDING */, &ab);
                gav(0, 0x8864 /* GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING */, &v0buf);
                gav(1, 0x8864, &v1buf);
                gav(0, 0x8865 /* GL_VERTEX_ATTRIB_ARRAY_SIZE */, &v0size);
                gav(1, 0x8865, &v1size);
                gav(0, 0x8867 /* GL_VERTEX_ATTRIB_ARRAY_STRIDE */, &v0stride);
                gav(1, 0x8867, &v1stride);
                gav(0, 0x8866 /* GL_VERTEX_ATTRIB_ARRAY_TYPE */, &v0type);
                gav(1, 0x8866, &v1type);
                printf("attrib state: array_buffer=%d loc0 buf=%d size=%d stride=%d type=%d, loc1 buf=%d size=%d stride=%d type=%d\n",
                       ab, v0buf, v0size, v0stride, v0type, v1buf, v1size, v1stride, v1type);
            }
        }
        my_glUseProgram(prog);
        printf("after useprogram err=0x%x\n", (unsigned)my_glGetError());
        /* Blend off on purpose: if the draw renders, corners are the attribute colors;
           if they stay green, the draw call itself never rasterized. */
        my_glDisable(GL_BLEND);
        printf("after disable blend err=0x%x\n", (unsigned)my_glGetError());
        my_glViewport(0, 0, 2, 2);
        printf("after viewport err=0x%x\n", (unsigned)my_glGetError());
        my_glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        my_glClear(GL_COLOR_BUFFER_BIT);
        printf("after clear err=0x%x\n", (unsigned)my_glGetError());
        my_glDrawArrays(GL_TRIANGLES, 0, 6);
        printf("after drawarrays err=0x%x\n", (unsigned)my_glGetError());
        my_glFinish();
        printf("after draw err=0x%x\n", (unsigned)my_glGetError());
        unsigned char tb[16];
        my_glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, tb);
        printf("2x2 known corner colors, top-down rows:");
        for (int y = 1; y >= 0; --y)
            for (int x = 0; x < 2; ++x) {
                const unsigned char* p = tb + ((size_t)y * 2 + x) * 4;
                printf(" (%d,%d)=%d,%d,%d,%d", x, y, p[0], p[1], p[2], p[3]);
            }
        printf("\n");
        int bad = 0;
        const unsigned char* tl = tb + (size_t)1 * 8;   /* readback row 1 = top row */
        const unsigned char* tr = tb + (size_t)1 * 8 + 4;
        const unsigned char* bl = tb;
        const unsigned char* br = tb + 4;
        if (tl[0] != 255 || tl[1] != 255 || tl[2] != 255) bad++;
        if (tr[0] != 0 || tr[1] != 255 || tr[2] != 0) bad++;
        if (bl[0] != 0 || bl[1] != 0 || bl[2] != 255) bad++;
        if (br[0] != 255 || br[1] != 0 || br[2] != 0) bad++;
        if (bad) { printf("FAIL known colors through the program: %d corners wrong\n", bad); return 1; }
        Ok("attribute color path is pixel-exact and channel-correct (RGBA, top-down rows preserved)");
        my_glDeleteBuffers(1, &pbuf);
        my_glDeleteBuffers(1, &cbuf);
    }

    typedef void (*fn_blend)(GLenum, GLenum);
    fn_blend blend = (fn_blend)glfwGetProcAddress("glBlendFunc");
    if (!blend) { printf("FAIL glBlendFunc not resolved\n"); return 1; }
    my_glUseProgram(prog);
    my_glEnable(GL_BLEND);
    blend(0x0001 /* GL_ONE */, 0x000D /* GL_ONE_MINUS_SRC_ALPHA */);
    my_glViewport(0, 0, W, H);
    my_glClearColor(bg[0], bg[1], bg[2], 1.0f);
    my_glClear(GL_COLOR_BUFFER_BIT);

    /* Two rows: what a real HUD would show. Row bands must differ, otherwise the
       layout is faking the text with one giant quad. */
    draw_text("FPS 60.3  16.6ms  tri 3750", 4, 2, &a, vbo, vbo_col, fg);
    draw_text("cam yaw 30 pitch 20 fov 45", 4, 2 + a.ch, &a, vbo2, vbo2_col, fg);
    my_glFlush();

    unsigned char* px = malloc((size_t)W * H * 4);
    my_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

    char path[128];
    snprintf(path, sizeof path, "lab/probes/hud_frame.bmp");
    flip_rows(px, W, H);
    if (img_write_bmp(path, W, H, px, err, sizeof err) < 0) { printf("FAIL bmp: %s\n", err); return 1; }
    flip_rows(px, W, H);   /* measurements below index bottom-up rows, so restore the readback order */
    double whole = img_changed_fraction(px, W, H, 30);
    if (whole < 0.02 || whole > 0.45) Fail("hud ink fraction %.1f%% outside sane range", whole * 100.0);
    else Ok("hud frame covers %.1f%% of %dx%d", whole * 100.0, W, H);

    /* Row profile: two text rows must each carry ink, and the strip below them
       must stay background - that is a layout assertion, not a coverage guess. */
    float r1 = 0.0f, r2 = 0.0f, below = 0.0f;
    for (int y = 2; y < 2 + a.ch; ++y) { float v = row_ink(px, W, H, y); if (v > r1) r1 = v; }
    for (int y = 2 + a.ch; y < 2 + 2 * a.ch; ++y) { float v = row_ink(px, W, H, y); if (v > r2) r2 = v; }
    for (int y = 2 + 2 * a.ch + 2; y < H; ++y) { float v = row_ink(px, W, H, y); if (v > below) below = v; }

    if (r1 < 0.02f || r2 < 0.02f) Fail("a text row came out empty (row1 peak %.3f, row2 peak %.3f)", r1, r2);
    else Ok("both rows rendered: row1 peak %.1f%% ink, row2 peak %.1f%% ink", r1 * 100.0, r2 * 100.0);

    if (below > 0.02f) Fail("strip below the HUD rows is not background (%.1f%% ink)", below * 100.0);
    else Ok("strip below the HUD rows stays background (%.1f%% ink)", below * 100.0);

    /* Does a changing number change pixels? This is what a HUD regression test
       needs to assert: "the FPS field moved", and nothing else moved. Isolated to
       one short string so the cell geometry is unambiguous. */
    unsigned char* sA = malloc((size_t)W * H * 4);
    unsigned char* sB = malloc((size_t)W * H * 4);
    /* An overlay pass owns its own GL state every frame - re-assert it here rather
       than relying on state left over from an earlier frame. */
    my_glUseProgram(prog);
    my_glEnable(GL_BLEND);
    blend(0x0001 /* GL_ONE */, 0x000D /* GL_ONE_MINUS_SRC_ALPHA */);

    my_glClear(GL_COLOR_BUFFER_BIT);
    draw_text("FPS 60.3", 4, 2, &a, vbo, vbo_col, fg);
    my_glFinish();
    my_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, sA);
    my_glClear(GL_COLOR_BUFFER_BIT);
    draw_text("FPS 60.4", 4, 2, &a, vbo, vbo_col, fg);
    my_glFinish();
    my_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, sB);

    int digits = pixels_differing(sA, sB, W, H, 4 + 7 * a.cw, 4 + 8 * a.cw, 16);
    int elsewhere = pixels_differing(sA, sB, W, H, 0, 4 + 7 * a.cw, 16) + pixels_differing(sA, sB, W, H, 4 + 8 * a.cw, W, 16);
    if (digits == 0) Fail("changing 60.3 -> 60.4 changed no pixels in the digit cell");
    else Ok("digit change moved %d pixels in the changed glyph cell", digits);
    if (elsewhere > 0) Fail("%d pixels changed outside the one changed glyph cell", elsewhere);
    else Ok("only the changed glyph cell moved - layout is deterministic");
    /* Isolated frames go to disk so the eye can judge them; numbers below decide pass/fail. */
    flip_rows(sA, W, H);
    flip_rows(sB, W, H);
    if (img_write_bmp("lab/probes/hud_digit_a.bmp", W, H, sA, err, sizeof err) < 0) { printf("FAIL bmp A: %s\n", err); return 1; }
    if (img_write_bmp("lab/probes/hud_digit_b.bmp", W, H, sB, err, sizeof err) < 0) { printf("FAIL bmp B: %s\n", err); return 1; }
    flip_rows(sA, W, H);
    flip_rows(sB, W, H);
    printf("glError after sB read: 0x%x\n", (unsigned)my_glGetError());
    /* Premultiplied atlas + blending: a background pixel inside a glyph quad must read
       back as the HUD background, not as black or as the foreground. */
    const unsigned char* qp = sA + (size_t)(H - 1 - 10) * (W * 4) + 4 * 4;
    printf("quad interior @(4,10) rgba=%d,%d,%d,%d -> %s\n", qp[0], qp[1], qp[2], qp[3],
           qp[3] > 200 ? "ink=1 at this pixel (glyph stroke)"
                       : (qp[0] < 8 && qp[1] < 8 && qp[2] < 8 ? "black - blend not applied"
                                                             : "blended over the HUD background - correct"));

    /* Cell profile: glyph pixels must reach the foreground, and most of the cell must
       stay at the HUD background. A solid slab (light or black) fails bg_share. */
    float inkA, bgA, peakA, inkB, bgB, peakB;
    {
        int lo = H, hi = -1;
        for (int y = 0; y < H; ++y) {
            int n = 0;
            for (int x = 0; x < W; ++x) {
                const unsigned char* p = sA + ((size_t)y * W + x) * 4;
                if (fabsf((float)p[0] - 30.0f) + fabsf((float)p[1] - 32.0f) + fabsf((float)p[2] - 36.0f) > 30.0f) n++;
            }
            if (n) { if (y < lo) lo = y; if (y > hi) hi = y; }
        }
        printf("sA stored rows with ink (bottom-up index): %d..%d\n", lo, hi);
        printf("sA cell column x=4..13, stored rows 93..74: ");
        for (int y = 93; y >= 74; --y) {
            const unsigned char* p = sA + ((size_t)y * W + 4) * 4;
            printf("%d,%d,%d,%d ", p[0], p[1], p[2], p[3]);
        }
        printf("\n");
    }
    cell_profile(sA, W, H, 4, a.cw, 2, a.ch, &inkA, &bgA, &peakA);
    cell_profile(sB, W, H, 4 + 7 * a.cw, a.cw, 2, a.ch, &inkB, &bgB, &peakB);
    printf("cell0 ink %.1f%% bg %.1f%% peak %.0f | digit cell ink %.1f%% bg %.1f%% peak %.0f\n",
           inkA * 100.0, bgA * 100.0, peakA, inkB * 100.0, bgB * 100.0, peakB);
    if (peakA < 150.0f || peakB < 150.0f) Fail("glyph pixels never reach the HUD foreground (peak %.0f, %.0f)", peakA, peakB);
    else Ok("glyph pixels reach the foreground (peak %.0f, %.0f)", peakA, peakB);
    if (bgA < 0.40f || bgB < 0.40f) Fail("glyph cells are solid slabs, not text (bg share %.1f%%, %.1f%%)", bgA * 100.0, bgB * 100.0);
    else Ok("glyph cells keep background around the ink (bg %.1f%%, %.1f%%)", bgA * 100.0, bgB * 100.0);

    /* Strokes vs slab: rows inside a glyph must differ a lot, otherwise the quad is
       sampling an averaged region of the atlas instead of the glyph cell. */
    float lo1, hi1;
    cell_row_profile(sA, W, H, 4, a.cw, 2, a.ch, &lo1, &hi1);
    printf("cell0 row ink range %.1f%%..%.1f%%\n", lo1 * 100.0, hi1 * 100.0);
    if (hi1 - lo1 < 0.15f || lo1 > 0.35f) Fail("glyph cell is flat (row ink %.1f%%..%.1f%%) - UV/geometry is not 1:1", lo1 * 100.0, hi1 * 100.0);
    else Ok("glyph cell has stroke structure (row ink %.1f%%..%.1f%%)", lo1 * 100.0, hi1 * 100.0);

    float deltaA = cell_ink(sA, W, H, 4 + 7 * a.cw, a.cw, 2, a.ch);
    float deltaB = cell_ink(sB, W, H, 4 + 7 * a.cw, a.cw, 2, a.ch);
    if (fabsf(deltaA - deltaB) < 0.02f) Fail("digit cell ink did not change (%.3f vs %.3f)", deltaA, deltaB);
    else Ok("digit cell ink changed %.1f%% -> %.1f%%", deltaA * 100.0, deltaB * 100.0);

    /* Orientation: the rendered glyph must match the CPU mask row by row, and must NOT
       match it mirrored. This is what catches a vertically flipped HUD image. */
    float mp[64], fp[64], rp[64];
    if (a.ch > 64) { printf("FAIL cell taller than profile buffer\n"); return 1; }
    mask_profile(&a, 'F' - FIRST_GLYPH, mp);
    frame_profile(sA, W, H, 4, 2, a.cw, a.ch, fp);
    for (int v = 0; v < a.ch; ++v) rp[v] = mp[a.ch - 1 - v];
    float direct = 0.0f, mirrored = 0.0f;
    for (int v = 0; v < a.ch; ++v) { direct += fabsf(fp[v] - mp[v]); mirrored += fabsf(fp[v] - rp[v]); }
    printf("glyph 'F' profile vs mask: direct %.2f, mirrored %.2f (lower is better)\n", direct, mirrored);
    printf("mask rows:"); for (int v = 0; v < a.ch; ++v) printf("%.1f ", mp[v]);
    printf("\nframe rows:"); for (int v = 0; v < a.ch; ++v) printf("%.1f ", fp[v]);
    printf("\n");
    if (direct >= mirrored) Fail("hud is vertically flipped (mirrored profile %.2f beats direct %.2f)", mirrored, direct);
    else if (direct > 1.0f) Fail("glyph rows do not match the mask (direct %.2f)", direct);
    else Ok("hud is upright: profile matches the mask (direct %.2f, mirrored %.2f)", direct, mirrored);

    free(sA);
    free(sB);

    /* Cost check: how expensive is a full HUD refresh per frame? */
    clock_t m0 = clock();
    for (int i = 0; i < 300; ++i) { my_glClear(GL_COLOR_BUFFER_BIT); draw_text("FPS 60.3  16.6ms  tri 3750", 4, 2, &a, vbo, vbo_col, fg); }
    my_glFinish();
    double text_ms = (double)(clock() - m0) * 1000.0 / CLOCKS_PER_SEC / 300.0;
    printf("hud refresh: %.3f ms/frame (300 frames, CPU-side timing)\n", text_ms);
    if (text_ms > 3.0) Fail("hud refresh %.3f ms/frame is too slow for a per-frame overlay", text_ms);
    else Ok("hud refresh %.3f ms/frame leaves headroom", text_ms);

    GLenum e = gl_check_error();
    if (e != GL_NO_ERROR) Fail("GL error 0x%x during hud pass", (unsigned)e); else Ok("no GL errors in hud pass");

    my_glDeleteBuffers(1, &vbo);
    my_glDeleteBuffers(1, &vbo_col);
    my_glDeleteBuffers(1, &vbo2);
    my_glDeleteBuffers(1, &vbo2_col);
    my_glDeleteVertexArrays(1, &vao);
    my_glDeleteFramebuffers(1, &fbo);
    my_glDeleteRenderbuffers(1, &rb);
    my_glDeleteProgram(prog);
    my_glDeleteShader(vs);
    my_glDeleteShader(fs);
    free(px);
    free(a.rgba);
    free(a.lum);
    glfwDestroyWindow(win);
    glfwTerminate();

    printf("probe: %s\n", fails ? "FAILED" : "PASS");
    return fails ? 1 : 0;
}
