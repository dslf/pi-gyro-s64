/* probe_gl_abi.c — does the project's uniform wrapper actually upload?
 *
 * src/gl.h declares the uniform entry points WITHOUT the leading `program`
 * argument (fn_iusf = (GLint location, GLsizei count, const float*)), but gl.c
 * stores the real driver function pointers, whose real signature is
 * (GLuint program, GLint location, GLsizei count, ...). This probe settles it
 * with numbers instead of reading: upload through the project wrapper, read the
 * value back from the program, and read the rendered pixel.
 *
 * Build (from repo root):
 *   gcc -O2 -std=c11 -Wall -Wextra lab/probes/probe_gl_abi.c -I../../src -I../../ src/gl.c \
 *       -lglfw3 -lopengl32 -lgdi32 -lwinmm -static -o lab/probes/probe_gl_abi.exe
 * Run: ./lab/probes/probe_gl_abi.exe
 */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl.h"

static int fails = 0;
static void Ok(const char* what) { printf("ok   %s\n", what); }
static void Fail(const char* what) { printf("FAIL %s\n", what); fails++; }

typedef void (*fn_get_uniform)(GLuint, GLint, GLfloat*);

static const char* VERT =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "void main(){ gl_Position = vec4(aPos.x * 2.0 - 1.0, aPos.y * 2.0 - 1.0, 0.0, 1.0); }\n";

/* uF and uV are only read back; the fragment output proves what the GPU sees. */
static const char* FRAG =
    "#version 330 core\n"
    "uniform float uF;\n"
    "uniform vec3 uV;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vec4(uF, uV.r, uV.g, 1.0); }\n";

static GLuint shader(GLenum type, const char* src) {
    GLuint s = my_glCreateShader(type);
    my_glShaderSource(s, 1, &src, NULL);
    my_glCompileShader(s);
    GLint ok = 0;
    my_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; GLsizei n = 0;
        my_glGetShaderInfoLog(s, sizeof log, &n, log);
        printf("shader compile failed: %s\n", log);
        fails++;
    }
    return s;
}

int main(void) {
    if (!glfwInit()) { printf("glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* w = glfwCreateWindow(64, 64, "abi", NULL, NULL);
    if (!w) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(w);

    char err[256];
    if (gl_load(glfwGetProcAddress, err, sizeof err) != 0) { printf("gl_load: %s\n", err); return 1; }

    /* The readback function is not in gl.c; resolve it directly to compare. */
    fn_get_uniform glGetUniformfv = (fn_get_uniform)glfwGetProcAddress("glGetUniformfv");
    if (!glGetUniformfv) { printf("glGetUniformfv unavailable\n"); return 1; }

    GLuint vs = shader(GL_VERTEX_SHADER, VERT), fs = shader(GL_FRAGMENT_SHADER, FRAG);
    GLuint prog = my_glCreateProgram();
    my_glAttachShader(prog, vs);
    my_glAttachShader(prog, fs);
    my_glLinkProgram(prog);
    GLint locF = my_glGetUniformLocation(prog, "uF");
    GLint locV = my_glGetUniformLocation(prog, "uV");
    printf("locations: uF=%d uV=%d\n", (int)locF, (int)locV);

    const float val = 0.75f;
    const float vec[3] = { 0.25f, 0.50f, 0.10f };
    my_glUseProgram(prog);
    /* Project convention: location-leading, no program argument (render.c does this). */
    my_glUniform1f(locF, val);
    my_glUniform3fv(locV, 1, vec);
    my_glGetError();

    float gotF = -1.0f, gotV[4] = {0,0,0,0};
    glGetUniformfv(prog, locF, &gotF);
    glGetUniformfv(prog, locV, gotV);
    printf("readback after wrapper upload: uF=%.4f (expected %.4f) uV=%.4f,%.4f,%.4f\n",
           gotF, val, gotV[0], gotV[1], gotV[2]);

    GLuint vao = 0, vbo = 0;
    my_glGenVertexArrays(1, &vao);
    my_glBindVertexArray(vao);
    my_glGenBuffers(1, &vbo);
    my_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const float quad[12] = {0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    my_glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    my_glEnableVertexAttribArray(0);
    my_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);

    GLuint fbo = 0, rb = 0;
    my_glGenFramebuffers(1, &fbo);
    my_glGenRenderbuffers(1, &rb);
    my_glBindRenderbuffer(GL_RENDERBUFFER, rb);
    my_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    my_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    my_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    my_glViewport(0, 0, 4, 4);
    my_glClearColor(0, 0, 0, 1);
    my_glClear(GL_COLOR_BUFFER_BIT);
    my_glDrawArrays(GL_TRIANGLES, 0, 6);
    unsigned char px[16];
    my_glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("rendered pixel r=%d g=%d b=%d (expected r=%.0f g=%.0f b=%.0f)\n",
           px[0], px[1], px[2], val * 255.0f, vec[0] * 255.0f, vec[1] * 255.0f);

    if (gotF == val && gotV[0] == vec[0] && gotV[1] == vec[1]) Ok("wrapper uploads reach the program");
    else Fail("wrapper uploads do NOT reach the program (missing leading program argument)");
    /* 8-bit rounding: 0.5*255 lands on 127 or 128, so compare with a tolerance. */
    if (abs(px[0] - (int)(val * 255.0f)) <= 1 && abs(px[1] - (int)(vec[0] * 255.0f)) <= 1 &&
        abs(px[2] - (int)(vec[1] * 255.0f)) <= 1) Ok("shader sees the uploaded values");
    else Fail("shader does not see the uploaded values");

    my_glDeleteVertexArrays(1, &vao);
    my_glDeleteBuffers(1, &vbo);
    my_glDeleteFramebuffers(1, &fbo);
    my_glDeleteRenderbuffers(1, &rb);
    my_glDeleteProgram(prog);
    my_glDeleteShader(vs);
    my_glDeleteShader(fs);
    glfwDestroyWindow(w);
    glfwTerminate();
    printf(fails ? "probe: FAILED\n" : "probe: PASS\n");
    return fails ? 1 : 0;
}
