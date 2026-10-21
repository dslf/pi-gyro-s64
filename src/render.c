#include "render.h"
#include "shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const float LIGHT_DIR[3]   = { 0.45f, 0.80f, 0.55f };
static const float LIGHT_COL[3]   = { 1.00f, 0.97f, 0.92f };
static const float AMB_TOP[3]     = { 0.30f, 0.34f, 0.40f };
static const float AMB_BOT[3]     = { 0.12f, 0.12f, 0.15f };
static const float BASE_COLOR[3]  = { 0.72f, 0.74f, 0.78f };
static const float BG_COLOR[3]    = { 0.22f, 0.24f, 0.28f };   /* linear */
static const float HL_COLOR[3]    = { 0.95f, 0.55f, 0.18f };   /* linear, picked-triangle outline */

int renderer_init(renderer* r, const char* vert_path, const char* frag_path, char* err, size_t errcap) {
  memset(r, 0, sizeof *r);

  if (shader_build(vert_path, frag_path, &r->prog, err, errcap) != 0) return -1;

  my_glUseProgram(r->prog);
  r->uVP       = my_glGetUniformLocation(r->prog, "uVP");
  r->uModel    = my_glGetUniformLocation(r->prog, "uModel");
  r->uNormal   = my_glGetUniformLocation(r->prog, "uNormal");
  r->uCam      = my_glGetUniformLocation(r->prog, "uCam");
  r->uLightDir = my_glGetUniformLocation(r->prog, "uLightDir");
  r->uLightCol = my_glGetUniformLocation(r->prog, "uLightCol");
  r->uAmbTop   = my_glGetUniformLocation(r->prog, "uAmbTop");
  r->uAmbBot   = my_glGetUniformLocation(r->prog, "uAmbBot");
  r->uColor    = my_glGetUniformLocation(r->prog, "uColor");
  r->uShine    = my_glGetUniformLocation(r->prog, "uShine");
  r->uSpecular = my_glGetUniformLocation(r->prog, "uSpecular");
  r->uOverride = my_glGetUniformLocation(r->prog, "uOverride");
  r->uOverrideCol = my_glGetUniformLocation(r->prog, "uOverrideCol");
  if (r->uOverride < 0 || r->uOverrideCol < 0) {
    snprintf(err, errcap, "uniform uOverride / uOverrideCol missing in %s", frag_path);
    return -1;
  }

  /* Static lighting parameters are uploaded once; the loop only updates matrices. */
  float ld[3] = { LIGHT_DIR[0], LIGHT_DIR[1], LIGHT_DIR[2] };
  float l = sqrtf(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
  if (l > 1e-6f) { ld[0] /= l; ld[1] /= l; ld[2] /= l; }
  my_glUniform3fv(r->uLightDir, 1, ld);
  my_glUniform3fv(r->uLightCol, 1, (float*)LIGHT_COL);
  my_glUniform3fv(r->uAmbTop, 1, (float*)AMB_TOP);
  my_glUniform3fv(r->uAmbBot, 1, (float*)AMB_BOT);
  my_glUniform3fv(r->uColor, 1, (float*)BASE_COLOR);
  my_glUniform1f(r->uShine, 48.0f);
  my_glUniform1f(r->uSpecular, 0.45f);
  my_glUseProgram(0);

  /* The fragment shader gamma-encodes its output; the clear path does not, so
     store the background pre-encoded to keep it consistent with the model. */
  for (int i = 0; i < 3; i++) r->bg[i] = powf(BG_COLOR[i], 1.0f / 2.2f);
  for (int i = 0; i < 3; i++) r->hl_rgb[i] = powf(HL_COLOR[i], 1.0f / 2.2f);
  r->hl_tri = -1;

  GLenum e = gl_check_error();
  if (e != GL_NO_ERROR) { snprintf(err, errcap, "GL error %u resolving uniforms", (unsigned)e); return -1; }
  return 0;
}

void renderer_free(renderer* r) {
  if (r && r->prog) my_glDeleteProgram(r->prog);
  if (r && r->hl_vbo) my_glDeleteBuffers(1, &r->hl_vbo);
  if (r && r->hl_vao) my_glDeleteVertexArrays(1, &r->hl_vao);
  memset(r, 0, sizeof *r);
}

int renderer_highlight(const renderer* r) { return r ? r->hl_tri : -1; }

/* Outline the picked triangle: three edges as GL_LINES, rebuilt from the CPU
   mesh so the geometry always matches the mesh the pick was computed against. */
void renderer_set_highlight(renderer* r, const obj_mesh* m, int tri) {
  if (!r) return;
  if (tri < 0) { r->hl_tri = -1; return; }
  if (!m || !m->v || tri >= m->triangles) { r->hl_tri = -1; return; }

  if (!r->hl_vao) {
    my_glGenVertexArrays(1, &r->hl_vao);
    my_glGenBuffers(1, &r->hl_vbo);
    /* The attribute pointers capture the ARRAY_BUFFER binding at this moment,
       so the highlight buffer must be bound inside the VAO, not only later. */
    my_glBindVertexArray(r->hl_vao);
    my_glBindBuffer(GL_ARRAY_BUFFER, r->hl_vbo);
    const GLsizei stride = MESH_STRIDE_FLOATS * (GLsizei)sizeof(float);
    my_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    my_glEnableVertexAttribArray(0);
    my_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
    my_glEnableVertexAttribArray(1);
    my_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(6 * sizeof(float)));
    my_glEnableVertexAttribArray(2);
    my_glBindBuffer(GL_ARRAY_BUFFER, 0);
    my_glBindVertexArray(0);
  }

  /* v0-v1, v1-v2, v2-v0 in the same interleaved layout the model uses. */
  const float* src = m->v + (size_t)tri * 3u * MESH_STRIDE_FLOATS;
  float data[6 * MESH_STRIDE_FLOATS];
  const int order[6] = { 0, 1, 1, 2, 2, 0 };
  for (int i = 0; i < 6; i++)
    memcpy(&data[i * MESH_STRIDE_FLOATS], src + order[i] * MESH_STRIDE_FLOATS,
           MESH_STRIDE_FLOATS * sizeof(float));

  my_glBindBuffer(GL_ARRAY_BUFFER, r->hl_vbo);
  my_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof data, data, GL_DYNAMIC_DRAW);
  my_glBindBuffer(GL_ARRAY_BUFFER, 0);
  r->hl_tri = tri;
}

void renderer_draw(renderer* r, const gpu_mesh* m, const mat4* vp, const mat4* model,
                   const mat3* nmat, vec3 eye, int wireframe) {
  my_glUseProgram(r->prog);
  my_glUniformMatrix4fv(r->uVP, 1, GL_FALSE, vp->m);
  my_glUniformMatrix4fv(r->uModel, 1, GL_FALSE, model->m);
  my_glUniformMatrix3fv(r->uNormal, 1, GL_FALSE, nmat->m);
  float e3[3] = { eye.x, eye.y, eye.z };
  my_glUniform3fv(r->uCam, 1, e3);
  my_glUniform1f(r->uOverride, 0.0f);

  my_glBindVertexArray(m->vao);
  my_glDrawArrays(wireframe ? GL_LINES : GL_TRIANGLES, 0, m->count);
  my_glBindVertexArray(0);

  if (r->hl_tri >= 0 && r->hl_vao) {
    /* Drawn without a depth test so the outline is readable on the surface it
       picked; the outline is a UI marker, not geometry to be occluded. */
    my_glUniform1f(r->uOverride, 1.0f);
    my_glUniform3fv(r->uOverrideCol, 1, r->hl_rgb);
    my_glDisable(GL_DEPTH_TEST);
    my_glBindVertexArray(r->hl_vao);
    my_glDrawArrays(GL_LINES, 0, 6);
    my_glBindVertexArray(0);
    my_glEnable(GL_DEPTH_TEST);
    my_glUniform1f(r->uOverride, 0.0f);
  }
}

int fb_create(framebuffer* fb, int w, int h, char* err, size_t errcap) {
  memset(fb, 0, sizeof *fb);
  if (w < 1 || h < 1) { snprintf(err, errcap, "bad framebuffer size %dx%d", w, h); return -1; }

  fb->w = w; fb->h = h;
  my_glGenFramebuffers(1, &fb->fbo);
  my_glGenTextures(1, &fb->tex);
  my_glGenRenderbuffers(1, &fb->rb);

  my_glBindTexture(GL_TEXTURE_2D, fb->tex);
  my_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  my_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  my_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  my_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  my_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  my_glBindTexture(GL_TEXTURE_2D, 0);

  my_glBindRenderbuffer(GL_RENDERBUFFER, fb->rb);
  my_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  my_glBindRenderbuffer(GL_RENDERBUFFER, 0);

  my_glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
  my_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->tex, 0);
  my_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->rb);

  GLenum st = my_glCheckFramebufferStatus(GL_FRAMEBUFFER);
  my_glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (st != GL_FRAMEBUFFER_COMPLETE) {
    snprintf(err, errcap, "framebuffer incomplete (status 0x%x)", (unsigned)st);
    fb_free(fb);
    return -1;
  }
  return 0;
}

void fb_free(framebuffer* fb) {
  if (!fb) return;
  if (fb->fbo) my_glDeleteFramebuffers(1, &fb->fbo);
  if (fb->tex) my_glDeleteTextures(1, &fb->tex);
  if (fb->rb)  my_glDeleteRenderbuffers(1, &fb->rb);
  memset(fb, 0, sizeof *fb);
}

/* RGBA rows with row 0 at the TOP of the image. glReadPixels returns rows
   bottom-up (OpenGL origin), so they are reversed here: every consumer - the
   pixel checks in main.c and img_write_bmp - wants screen-top-first. */
int fb_read_rgba(const framebuffer* fb, unsigned char* out, char* err, size_t errcap) {
  my_glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
  /* The default (window) framebuffer has no color attachment: read from GL_BACK. */
  my_glReadBuffer(fb->fbo ? GL_COLOR_ATTACHMENT0 : GL_BACK);
  my_glReadPixels(0, 0, fb->w, fb->h, GL_RGBA, GL_UNSIGNED_BYTE, out);
  my_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

  const size_t rb = (size_t)fb->w * 4u;
  unsigned char* tmp = malloc(rb);
  if (!tmp) { snprintf(err, errcap, "out of memory while flipping rows"); return -1; }
  for (int y = 0; y < fb->h / 2; y++) {
    unsigned char* a = out + (size_t)y * rb;
    unsigned char* b = out + (size_t)(fb->h - 1 - y) * rb;
    memcpy(tmp, a, rb);
    memcpy(a, b, rb);
    memcpy(b, tmp, rb);
  }
  free(tmp);

  GLenum e = gl_check_error();
  if (e != GL_NO_ERROR) { snprintf(err, errcap, "GL error %u reading pixels", (unsigned)e); return -1; }
  return 0;
}
