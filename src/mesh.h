#ifndef VIEWER_MESH_H
#define VIEWER_MESH_H

#include "gl.h"
#include "obj.h"

/* Interleaved vertex layout: position(3) normal(3) uv(2) -> 8 floats. */
#define MESH_STRIDE_FLOATS 8

typedef struct {
  GLuint vao;
  GLuint vbo;
  GLsizei count;      /* vertex count, always a multiple of 3 */
  float radius;       /* bounding radius, handy for camera framing */
  float fit;          /* bounding radius used to frame the object on reset */
} gpu_mesh;

/* Upload an already parsed obj_mesh. Returns 0 on success, -1 with err set. */
int  mesh_upload(const obj_mesh* m, gpu_mesh* out, char* err, size_t errcap);
void mesh_free(gpu_mesh* m);

#endif
