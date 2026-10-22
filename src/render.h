#ifndef VIEWER_RENDER_H
#define VIEWER_RENDER_H

#include "gl.h"
#include "vmath.h"
#include "mesh.h"
#include "obj.h"

typedef struct {
  GLuint prog;
  GLint uVP, uModel, uNormal, uCam, uLightDir, uLightCol, uAmbTop, uAmbBot, uColor, uShine, uSpecular;
  GLint uOverride, uOverrideCol;
  float bg[3];   /* linear clear colour, gamma-encoded by the caller's pipeline */
  /* Highlighted triangle: its three edges drawn as GL_LINES in a flat colour so a
     mouse pick is visible on top of the surface (depth test off for the outline). */
  GLuint hl_vao, hl_vbo;
  int    hl_tri;      /* -1 = nothing highlighted */
  float  hl_rgb[3];   /* gamma-encoded, same encoding the shader applies to shading */
  int    outline_visible;   /* 0 = the picked outline is not drawn (the pin stays active) */
} renderer;

typedef struct { GLuint fbo, tex, rb; int w, h; } framebuffer;

int  renderer_init(renderer* r, const char* vert_path, const char* frag_path, char* err, size_t errcap);
void renderer_free(renderer* r);
/* tri < 0 clears the highlight. Rebuilt from the CPU mesh on every call. */
void renderer_set_highlight(renderer* r, const obj_mesh* m, int tri);
int  renderer_highlight(const renderer* r);

/* Live rendering parameters for the settings widgets, in the same units as the shader
   constants (linear rgb; background and outline are gamma-encoded on store). */
void renderer_set_light(renderer* r, const float dir[3], float intensity);
void renderer_set_sky(renderer* r, float top_mul, float bot_mul);
void renderer_set_surface(renderer* r, const float rgb[3], float shine, float specular);
void renderer_set_background(renderer* r, const float rgb[3]);
void renderer_set_outline_color(renderer* r, const float rgb[3]);
/* Hide/show the picked-triangle outline. The selection itself stays pinned, only the marker
   disappears, so outline_visible=false is a display option rather than an un-pick. */
void renderer_set_outline_visible(renderer* r, int visible);
void renderer_draw(renderer* r, const gpu_mesh* m, const mat4* vp, const mat4* model,
                   const mat3* nmat, vec3 eye, int wireframe);

int  fb_create(framebuffer* fb, int w, int h, char* err, size_t errcap);
void fb_free(framebuffer* fb);
int  fb_read_rgba(const framebuffer* fb, unsigned char* out, char* err, size_t errcap);

#endif
