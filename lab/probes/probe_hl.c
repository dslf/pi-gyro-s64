/* Minimal repro: does the highlight upload + outline draw crash? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "mesh.h"
#include "obj.h"
#include "render.h"
#include "shader.h"
#include "gl.h"
#include "vmath.h"
#include <GLFW/glfw3.h>

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (!glfwInit()) { printf("glfwInit failed\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* win = glfwCreateWindow(64, 64, "probe", NULL, NULL);
  if (!win) { printf("no context\n"); return 1; }
  glfwMakeContextCurrent(win);
  char err0[128] = "";
  printf("gl_load=%d %s\n", gl_load(glfwGetProcAddress, err0, sizeof err0), err0);
  printf("ptrs: genbuf=%p genvao=%p bindvao=%p bufdata=%p vap=%p enable=%p disable=%p draw=%p linew=%p\n",
         (void*)my_glGenBuffers, (void*)my_glGenVertexArrays, (void*)my_glBindVertexArray,
         (void*)my_glBufferData, (void*)my_glVertexAttribPointer, (void*)my_glEnable,
         (void*)my_glDisableVertexAttribArray, (void*)my_glDrawArrays, (void*)my_glLineWidth);

  obj_mesh m; char err[256] = "";
  if (obj_parse_file("assets/cube.obj", &m, err, sizeof err) < 0) { printf("parse: %s\n", err); return 1; }
  gpu_mesh g; memset(&g, 0, sizeof g);
  if (mesh_upload(&m, &g, &err[0], sizeof err) < 0) { printf("upload: %s\n", err); return 1; }
  printf("upload ok: count %d fit %.3f\n", g.count, g.fit);

  camera c; memset(&c, 0, sizeof c);
  camera_reset(&c, g.fit);
  camera_set_aspect(&c, 1.0f);

  framebuffer fb; memset(&fb, 0, sizeof fb);
  if (fb_create(&fb, 64, 64, err, sizeof err) < 0) { printf("fbo: %s\n", err); return 1; }

  renderer r; memset(&r, 0, sizeof r);
  if (renderer_init(&r, "shaders/model.vert", "shaders/model.frag", err, sizeof err) < 0) {
    printf("init: %s\n", err); return 1;
  }
  printf("highlight rgb %d %d %d\n", (int)(r.hl_rgb[0]*255), (int)(r.hl_rgb[1]*255), (int)(r.hl_rgb[2]*255));

  mat4 model = m4_identity();
  mat4 proj = camera_projection(&c, 1.0f);
  mat4 view = camera_view(&c);
  mat4 vp = m4_mul(&proj, &view);
  mat3 nm = m3_normal(&model);
  vec3 eye = camera_eye(&c);

  my_glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
  my_glViewport(0, 0, fb.w, fb.h);
  my_glClearColor(r.bg[0], r.bg[1], r.bg[2], 1.0f);
  my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  my_glEnable(GL_DEPTH_TEST);
  renderer_draw(&r, &g, &vp, &model, &nm, eye, 0);
  printf("draw plain ok\n");

  renderer_set_highlight(&r, &m, 0);
  printf("set highlight tri 0 ok, hl_vbo=%u hl_vao=%u\n", r.hl_vbo, r.hl_vao);
  printf("array_buffer_binding=%d vao=%d\n", (int)gl_check_error(), 0);

  /* Control experiment: a clean position-only VAO drawing three lines. */
  GLuint va2 = 0, vb2 = 0;
  my_glGenVertexArrays(1, &va2);
  my_glGenBuffers(1, &vb2);
  my_glBindVertexArray(va2);
  my_glBindBuffer(GL_ARRAY_BUFFER, vb2);
  float pos[9] = { -0.5f, 0, 0, 0.5f, 0, 0, 0, 0.5f, 0 };
  my_glBufferData(GL_ARRAY_BUFFER, sizeof pos, pos, GL_STATIC_DRAW);
  my_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (const void*)0);
  my_glEnableVertexAttribArray(0);
  my_glBindBuffer(GL_ARRAY_BUFFER, 0);
  printf("control vao=%u vbo=%u\n", va2, vb2);
  my_glUseProgram(r.prog);
  my_glUniformMatrix4fv(r.uVP, 1, GL_FALSE, vp.m);
  my_glUniformMatrix4fv(r.uModel, 1, GL_FALSE, model.m);
  my_glUniformMatrix3fv(r.uNormal, 1, GL_FALSE, nm.m);
  float e3b[3] = { eye.x, eye.y, eye.z };
  my_glUniform3fv(r.uCam, 1, e3b);
  my_glUniform1f(r.uOverride, 1.0f);
  my_glUniform3fv(r.uOverrideCol, 1, r.hl_rgb);
  my_glDisable(GL_DEPTH_TEST);
  my_glBindVertexArray(va2);
  printf("X control draw lines\n");
  my_glDrawArrays(GL_LINES, 0, 3);
  printf("X control draw done err=%u\n", (unsigned)gl_check_error());
  my_glBindVertexArray(0);
  my_glEnable(GL_DEPTH_TEST);
  my_glUniform1f(r.uOverride, 0.0f);
  my_glDeleteBuffers(1, &vb2);
  my_glDeleteVertexArrays(1, &va2);
  printf("control cleaned\n");
  my_glBindVertexArray(0);
  my_glEnable(GL_DEPTH_TEST);
  my_glUniform1f(r.uOverride, 0.0f);
  my_glBindVertexArray(g.vao);
  my_glDrawArrays(GL_TRIANGLES, 0, g.count);
  my_glBindVertexArray(0);
  printf("H draw triangles err=%u\n", (unsigned)gl_check_error());

  renderer_set_highlight(&r, &m, 1200);   /* out of range: must be ignored */
  printf("set highlight out-of-range ok, highlight=%d\n", renderer_highlight(&r));
  my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  renderer_draw(&r, &g, &vp, &model, &nm, eye, 0);
  printf("draw after clearing ok\n");

  unsigned char px[64 * 64 * 4];
  fb_read_rgba(&fb, px, err, sizeof err);
  int er = (int)(r.hl_rgb[0] * 255.0f + 0.5f), eg = (int)(r.hl_rgb[1] * 255.0f + 0.5f),
      eb = (int)(r.hl_rgb[2] * 255.0f + 0.5f);
  printf("last frame after clearing outline: orange px = ?\n  rgb target %d %d %d\n", er, eg, eb);

  fb_free(&fb); mesh_free(&g); renderer_free(&r); obj_mesh_free(&m);
  glfwDestroyWindow(win);
  glfwTerminate();
  printf("done\n");
  return 0;
}
