/* Standalone probe: does the product's GL loader feed per-vertex attributes with correct
   per-vertex offsets? Minimal — one quad, four distinct vertex colors, small viewport,
   exact pixel expectations. Nothing else (no atlas, no font, no blending).
   Build: gcc -O2 -std=c11 -Wall -Wextra lab/probes/probe_attrib.c src/gl.c -o lab/probes/probe_attrib.exe -I../../src -I../../  -lglfw3 -lopengl32 -lgdi32 */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gl.h"

#define GL_TRIANGLE_STRIP 0x0006 /* legacy constant; the product loader header does not declare it */

#include <stdio.h>
#include <string.h>

#define W 6
#define H 4

static int fails = 0;
#define Ok(...) do { printf("ok   %s\n", __VA_ARGS__); } while (0)
#define Fail(...) do { printf("FAIL "); printf(__VA_ARGS__); ++fails; } while (0)

static const char* VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aCol;\n"
    "out vec4 vCol;\n"
    "void main(){ vCol = aCol; gl_Position = vec4(aPos.x / 3.0 - 1.0, aPos.y / 2.0 - 1.0, 0.0, 1.0); }\n";

static const char* FS =
    "#version 330 core\n"
    "in vec4 vCol;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vCol; }\n";

int main(void) {
    if (!glfwInit()) { printf("FAIL glfwInit\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(W, H, "probe_attrib", NULL, NULL);
    if (!win) { printf("FAIL window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    char err[256] = {0};
    if (gl_load((gl_getproc)glfwGetProcAddress, err, sizeof err) != 0) {
        printf("FAIL gl_load: %s\n", err);
        glfwTerminate();
        return 1;
    }

    GLuint vs = my_glCreateShader(GL_VERTEX_SHADER);
    my_glShaderSource(vs, 1, &VS, NULL);
    my_glCompileShader(vs);
    GLint st = 0;
    my_glGetShaderiv(vs, GL_COMPILE_STATUS, &st);
    if (!st) {
        char log[512] = {0};
        my_glGetShaderInfoLog(vs, sizeof log, NULL, log);
        Fail("vertex shader: %s", log);
        glfwTerminate();
        return 1;
    }
    GLuint fs = my_glCreateShader(GL_FRAGMENT_SHADER);
    my_glShaderSource(fs, 1, &FS, NULL);
    my_glCompileShader(fs);
    my_glGetShaderiv(fs, GL_COMPILE_STATUS, &st);
    if (!st) { Fail("fragment shader compile failed"); glfwTerminate(); return 1; }
    GLuint prog = my_glCreateProgram();
    my_glAttachShader(prog, vs);
    my_glAttachShader(prog, fs);
    my_glLinkProgram(prog);
    my_glGetProgramiv(prog, GL_LINK_STATUS, &st);
    if (!st) { Fail("link failed"); glfwTerminate(); return 1; }

    /* A quad covering the whole 6x4 framebuffer, drawn as a triangle strip.
       Vertex order: (0,H) top-left white, (W,H) top-right green, (0,0) bottom-left blue,
       (W,0) bottom-right red. Attribute 1 holds the colors, stride 16, offsets 0/16/32/48. */
    float pos[8] = {0, H, W, H, 0, 0, W, 0};
    float col[16] = {
        255, 255, 255, 255,   /* v0 top-left white */
        0,   255, 0,   255,   /* v1 top-right green */
        0,   0,   255, 255,   /* v2 bottom-left blue */
        255, 0,   0,   255,   /* v3 bottom-right red */
    };

    GLuint vao, vbo_pos, vbo_col;
    /* Render into an FBO: a hidden window renders into its back buffer, and readback of the
       front buffer there is empty, so every measurement needs an offscreen target. */
    GLuint fbo, rb;
    my_glGenFramebuffers(1, &fbo);
    my_glGenRenderbuffers(1, &rb);
    my_glBindRenderbuffer(GL_RENDERBUFFER, rb);
    my_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    my_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    my_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    if (my_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Fail("FBO incomplete");
        glfwTerminate();
        return 1;
    }
    my_glGenVertexArrays(1, &vao);
    my_glGenBuffers(1, &vbo_pos);
    my_glGenBuffers(1, &vbo_col);
    my_glBindVertexArray(vao);
    my_glBindBuffer(GL_ARRAY_BUFFER, vbo_pos);
    my_glBufferData(GL_ARRAY_BUFFER, sizeof pos, pos, GL_STATIC_DRAW);
    my_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);
    my_glEnableVertexAttribArray(0);
    my_glBindBuffer(GL_ARRAY_BUFFER, vbo_col);
    my_glBufferData(GL_ARRAY_BUFFER, sizeof col, col, GL_STATIC_DRAW);
    my_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void*)0);
    my_glEnableVertexAttribArray(1);
    while (my_glGetError() != GL_NO_ERROR) {}

    my_glDisable(GL_BLEND);
    my_glViewport(0, 0, W, H);
    my_glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    my_glClear(GL_COLOR_BUFFER_BIT);
    my_glUseProgram(prog);
    my_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    my_glFinish();
    printf("after draw err=0x%x\n", (unsigned)my_glGetError());

    unsigned char px[W * H * 4];
    my_glReadBuffer(GL_COLOR_ATTACHMENT0);
    my_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

    /* Report the whole 6x4 grid, top-down rows, so the mapping is visible. */
    for (int y = 0; y < H; ++y) {
        printf("row %d:", y);
        for (int x = 0; x < W; ++x) {
            const unsigned char* p = px + ((size_t)(H - 1 - y) * W + x) * 4;
            printf(" %d,%d,%d", p[0], p[1], p[2]);
        }
        printf("\n");
    }

    /* Expectation: a strip over 4 vertices covers the whole quad; interpolation makes each
       pixel a blend of the two nearest vertex colors. Corners therefore carry the corner
       color strongly. */
    const unsigned char* tl = px + (size_t)(H - 1) * W * 4;            /* top-left pixel */
    const unsigned char* tr = px + (size_t)(H - 1) * W * 4 + (W - 1) * 4;
    const unsigned char* bl = px + 0 * W * 4;                           /* bottom-left pixel */
    const unsigned char* br = px + (W - 1) * 4;                         /* bottom-right pixel */

    printf("corners: top-left %d,%d,%d (want ~255,255,255) | top-right %d,%d,%d (want ~0,255,0)"
           " | bottom-left %d,%d,%d (want ~0,0,255) | bottom-right %d,%d,%d (want ~255,0,0)\n",
           tl[0], tl[1], tl[2], tr[0], tr[1], tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2]);

    if (tl[1] > 200 && tl[2] > 200 && tl[0] > 200) Ok("top-left vertex color reached the pixel");
    else Fail("top-left pixel %d,%d,%d is not white - vertex 0's attribute is not at offset 0", tl[0], tl[1], tl[2]);
    if (tr[1] > 200 && tr[0] < 90) Ok("top-right vertex color reached the pixel");
    else Fail("top-right pixel %d,%d,%d is not green - vertex 1's attribute is not at offset 16", tr[0], tr[1], tr[2]);
    if (bl[2] > 200 && bl[0] < 90) Ok("bottom-left vertex color reached the pixel");
    else Fail("bottom-left pixel %d,%d,%d is not blue - vertex 2's attribute is not at offset 32", bl[0], bl[1], bl[2]);
    if (br[0] > 200 && br[2] < 90) Ok("bottom-right vertex color reached the pixel");
    else Fail("bottom-right pixel %d,%d,%d is not red - vertex 3's attribute is not at offset 48", br[0], br[1], br[2]);

    my_glDeleteBuffers(1, &vbo_pos);
    my_glDeleteBuffers(1, &vbo_col);
    my_glDeleteVertexArrays(1, &vao);
    my_glDeleteFramebuffers(1, &fbo);
    my_glDeleteRenderbuffers(1, &rb);
    my_glDeleteProgram(prog);
    my_glDeleteShader(vs);
    my_glDeleteShader(fs);
    glfwDestroyWindow(win);
    glfwTerminate();

    printf("probe: %s\n", fails ? "FAILED" : "PASS");
    return fails ? 1 : 0;
}
