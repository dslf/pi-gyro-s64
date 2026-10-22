/* OpenGL 4.6 probe: does a 4.6 core context exist here, and does the uniform/HUD
   behaviour change under it? Printed numbers decide, not assumptions.
   Also checks explicit uniform locations (layout(location=)) which GL 4.x makes strict. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include "gl.h"

/* Not in the project's minimal gl.h; the probe asks the driver for it directly. */
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B60
#endif

static GLuint compile(GLenum type, const char* src, char* err, size_t n) {
    GLuint s = my_glCreateShader(type);
    my_glShaderSource(s, 1, &src, NULL);
    my_glCompileShader(s);
    GLint ok = 0;
    my_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { my_glGetShaderInfoLog(s, (GLsizei)n - 1, NULL, err); return 0; }
    return s;
}

static int try_context(int major, int minor, char* out, size_t n, size_t outcap) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* w = glfwCreateWindow(256, 256, "gl probe", NULL, NULL);
    if (!w) {
        snprintf(out, outcap, "%d.%d core: NOT AVAILABLE", major, minor);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
        return 1;
    }
    glfwMakeContextCurrent(w);
    char lerr[256] = {0};
    if (gl_load((gl_getproc)glfwGetProcAddress, lerr, sizeof lerr) != 0) {
        snprintf(out, outcap, "%d.%d core: gl_load failed: %s", major, minor, lerr);
        glfwDestroyWindow(w);
        return 1;
    }
    snprintf(out, n, "%d.%d core OK | %s | %s | GLSL %s",
             major, minor,
             (const char*)my_glGetString(GL_VERSION),
             (const char*)my_glGetString(GL_RENDERER),
             (const char*)my_glGetString(GL_SHADING_LANGUAGE_VERSION));
    glfwDestroyWindow(w);
    return 0;
}

int main(void) {
    if (!glfwInit()) return 2;
    char line[512];
    /* The driver may hand 4.6 only if no debug/srgb hints are requested; ask plainly. */
    if (try_context(4, 6, line, sizeof line, sizeof line) == 0) printf("%s\n", line);
    else printf("%s\n", line);
    if (try_context(4, 5, line, sizeof line, sizeof line) == 0) printf("%s\n", line);
    else printf("%s\n", line);
    if (try_context(3, 3, line, sizeof line, sizeof line) == 0) printf("%s\n", line);
    else printf("%s\n", line);

    /* Keep the highest context we got, then run the uniform test inside it. */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* w = glfwCreateWindow(256, 256, "gl46 uniform test", NULL, NULL);
    if (!w) { printf("FAIL no 4.6 core window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(w);
    char lerr[256] = {0};
    if (gl_load((gl_getproc)glfwGetProcAddress, lerr, sizeof lerr) != 0) { printf("FAIL gl_load: %s\n", lerr); return 1; }
    printf("running in: %s\n", (const char*)my_glGetString(GL_VERSION));

    const char* vs =
        "#version 460 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    /* Explicit locations: GL 4.x strict-location semantics, so a mismatch is a hard error. */
    const char* fs =
        "#version 460 core\n"
        "layout(location=0) uniform vec3 uBg;\n"
        "layout(location=1) uniform vec3 uFg;\n"
        "out vec4 frag_out;\n"
        "void main(){ frag_out = vec4(mix(uBg, uFg, 0.0), 1.0); }\n";
    char err[512] = {0};
    GLuint p = my_glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs, err, sizeof err);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, err, sizeof err);
    if (!v || !f) { printf("FAIL compile: %s\n", err); glfwTerminate(); return 1; }
    my_glAttachShader(p, v);
    my_glAttachShader(p, f);
    my_glLinkProgram(p);
    GLint ok = 0;
    my_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { printf("FAIL link\n"); glfwTerminate(); return 1; }
    printf("program id=%u | explicit locations uBg=0 uFg=1 | queried: uBg=%d uFg=%d\n",
           p, my_glGetUniformLocation(p, "uBg"), my_glGetUniformLocation(p, "uFg"));

    typedef void (*fn_get_uniform)(GLuint, GLint, GLfloat*);
    fn_get_uniform getu = (fn_get_uniform)glfwGetProcAddress("glGetUniformfv");
    if (!getu) { printf("FAIL no glGetUniformfv\n"); glfwTerminate(); return 1; }

    GLuint vao = 0, vbo = 0;
    my_glGenVertexArrays(1, &vao);
    my_glBindVertexArray(vao);
    my_glGenBuffers(1, &vbo);
    my_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const float quad[6 * 2] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    my_glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    my_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);
    my_glEnableVertexAttribArray(0);

    const float bg[3] = { 30.0f / 255.0f, 32.0f / 255.0f, 36.0f / 255.0f };
    const float fg[3] = { 235.0f / 255.0f, 240.0f / 255.0f, 246.0f / 255.0f };
    my_glUseProgram(p);
    my_glUniform3fv(my_glGetUniformLocation(p, "uBg"), 1, bg);
    my_glUniform3fv(my_glGetUniformLocation(p, "uFg"), 1, fg);
    float got[4] = {0,0,0,0};
    getu(p, 0, got);
    printf("readback uBg: %.3f,%.3f,%.3f | glError 0x%x\n", got[0], got[1], got[2], (unsigned)my_glGetError());
    my_glClearColor(0, 0, 0, 1);
    my_glClear(GL_COLOR_BUFFER_BIT);
    my_glDrawArrays(GL_TRIANGLES, 0, 6);
    my_glFinish();
    unsigned char px[4] = {0,0,0,0};
    my_glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("pixel under mix(uBg,uFg,0): %d,%d,%d -> %s\n", px[0], px[1], px[2],
           px[0] == 30 && px[1] == 32 && px[2] == 36 ? "uBg reached the fragment" :
           (px[0] < 8 && px[1] < 8 && px[2] < 8 ? "uBg did NOT reach the fragment" : "other value"));
    printf("glError at end: 0x%x\n", (unsigned)my_glGetError());
    my_glDeleteVertexArrays(1, &vao);
    my_glDeleteBuffers(1, &vbo);
    glfwDestroyWindow(w);
    glfwTerminate();
    printf("probe: DONE\n");
    return 0;
}
