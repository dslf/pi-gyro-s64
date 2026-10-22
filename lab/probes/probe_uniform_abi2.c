/* Which ABI does the driver actually honor for glUniform3fv?
   gl.c declares the wrappers without the `program` parameter (fn_iusf = (location, count, vals)),
   but the real GL function is glUniform3fv(program, location, count, vals).
   Same program, same quad, two uploads with different colors: whichever color lands in the
   pixel is the convention the driver honors. Numbers, not reasoning. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl.h"

static GLuint compile(GLenum type, const char* src, char* err, size_t n) {
    GLuint s = my_glCreateShader(type);
    my_glShaderSource(s, 1, &src, NULL);
    my_glCompileShader(s);
    GLint ok = 0;
    my_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { my_glGetShaderInfoLog(s, (GLsizei)n - 1, NULL, err); return 0; }
    return s;
}

int main(void) {
    if (!glfwInit()) return 2;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* w = glfwCreateWindow(256, 256, "uniform abi", NULL, NULL);
    if (!w) { glfwTerminate(); return 2; }
    glfwMakeContextCurrent(w);
    char lerr[256] = {0};
    if (gl_load((gl_getproc)glfwGetProcAddress, lerr, sizeof lerr) != 0) { printf("FAIL gl_load: %s\n", lerr); glfwTerminate(); return 1; }
    printf("%s\n", (const char*)my_glGetString(GL_VERSION));
    printf("%s\n", (const char*)my_glGetString(GL_RENDERER));

    const char* vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    const char* fs =
        "#version 330 core\n"
        "out vec4 frag_out;\n"
        "uniform vec3 uBg;\n"
        "void main(){ frag_out = vec4(uBg, 1.0); }\n";
    char err[512] = {0};
    GLuint p = my_glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs, err, sizeof err);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, err, sizeof err);
    my_glAttachShader(p, v);
    my_glAttachShader(p, f);
    my_glLinkProgram(p);
    GLint ok = 0;
    my_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { printf("FAIL link: %s\n", err); return 1; }
    printf("program id=%u | uSize-free single uniform\n", p);

    typedef void (*fn_get_uniform)(GLuint, GLint, GLfloat*);
    typedef void (*fn_raw_u3)(GLuint, GLint, GLsizei, const float*);
    fn_get_uniform getu = (fn_get_uniform)glfwGetProcAddress("glGetUniformfv");
    fn_raw_u3 raw_u3 = (fn_raw_u3)glfwGetProcAddress("glUniform3fv");
    if (!getu || !raw_u3) { printf("FAIL missing raw uniform entry points\n"); return 1; }

    int loc = my_glGetUniformLocation(p, "uBg");
    printf("location of uBg = %d\n", loc);
    printf("wrapper ptr=%p raw ptr=%p\n", (void*)my_glUniform3fv, (void*)raw_u3);

    GLuint vao = 0, vbo = 0;
    my_glGenVertexArrays(1, &vao);
    my_glBindVertexArray(vao);
    my_glGenBuffers(1, &vbo);
    my_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const float quad[6 * 2] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    my_glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    my_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);
    my_glEnableVertexAttribArray(0);

    const float magenta[3] = { 1.0f, 0.0f, 1.0f };
    const float cyan[3] = { 0.0f, 1.0f, 1.0f };
    float got[4] = {0, 0, 0, 0};
    unsigned char px[4] = {0, 0, 0, 0};

    my_glUseProgram(p);
    my_glClearColor(0.1176f, 0.1255f, 0.1412f, 1.0f); /* panel bg 30,32,36 */

    /* Phase 1: upload through the gl.c wrapper (location-first, no program). */
    my_glUniform3fv(loc, 1, magenta);
    getu(p, loc, got);
    printf("wrapper upload -> readback: %.3f,%.3f,%.3f\n", got[0], got[1], got[2]);
    my_glClear(GL_COLOR_BUFFER_BIT);
    my_glDrawArrays(GL_TRIANGLES, 0, 6);
    my_glFinish();
    my_glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("wrapper upload -> pixel: %d,%d,%d -> %s\n", px[0], px[1], px[2],
           px[0] > 200 && px[2] > 200 ? "magenta: wrapper convention lands" :
           px[0] > 200 && px[1] > 200 ? "cyan: stale value from phase 2" :
           px[0] < 8 ? "black: uBg stayed 0, wrapper upload did NOT land" :
           "clear color: nothing drawn");

    printf("glError after wrapper upload: 0x%x\n", (unsigned)my_glGetError());
    /* Phase 2: upload through the raw driver entry point (program-first). */
    raw_u3(p, loc, 1, cyan);
    printf("glError after raw upload: 0x%x\n", (unsigned)my_glGetError());
    getu(p, loc, got);
    printf("raw upload -> readback: %.3f,%.3f,%.3f\n", got[0], got[1], got[2]);
    my_glClear(GL_COLOR_BUFFER_BIT);
    my_glDrawArrays(GL_TRIANGLES, 0, 6);
    my_glFinish();
    my_glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("raw upload -> pixel: %d,%d,%d -> %s\n", px[0], px[1], px[2],
           px[1] > 200 && px[2] > 200 ? "cyan: raw program-first convention lands" :
           px[0] < 8 ? "black: raw upload did NOT land" :
           "still the phase-1 value");

    /* Phase 3: program 0 = legacy "current program" path, to see which handle the driver wants. */
    raw_u3(0, loc, 1, magenta);
    getu(p, loc, got);
    printf("raw upload with program=0 -> readback: %.3f,%.3f,%.3f (glError 0x%x)\n", got[0], got[1], got[2], (unsigned)my_glGetError());
    my_glDeleteVertexArrays(1, &vao);
    my_glDeleteBuffers(1, &vbo);
    glfwDestroyWindow(w);
    glfwTerminate();
    printf("probe: DONE (classification above is the answer)\n");
    return 0;
}
