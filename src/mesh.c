#include "mesh.h"

#include <stdio.h>
#include <string.h>

int mesh_upload(const obj_mesh* m, gpu_mesh* out, char* err, size_t errcap) {
  memset(out, 0, sizeof *out);

  if (!m || m->vcount == 0 || !m->v) {
    snprintf(err, errcap, "nothing to upload: mesh has no vertices");
    return -1;
  }

  const size_t bytes = (size_t)m->vcount * MESH_STRIDE_FLOATS * sizeof(float);

  my_glGenVertexArrays(1, &out->vao);
  my_glGenBuffers(1, &out->vbo);

  my_glBindBuffer(GL_ARRAY_BUFFER, out->vbo);
  my_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, m->v, GL_STATIC_DRAW);

  my_glBindVertexArray(out->vao);

  const GLsizei stride = MESH_STRIDE_FLOATS * (GLsizei)sizeof(float);
  my_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
  my_glEnableVertexAttribArray(0);
  my_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
  my_glEnableVertexAttribArray(1);
  my_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(6 * sizeof(float)));
  my_glEnableVertexAttribArray(2);

  my_glBindVertexArray(0);
  my_glBindBuffer(GL_ARRAY_BUFFER, 0);

  out->count = (GLsizei)m->vcount;
  out->radius = m->radius > 0 ? m->radius : 1.0f;

  /* Framing uses the bounding radius: it covers every span and any rotation. */
  out->fit = out->radius;

  GLenum e = gl_check_error();
  if (e != GL_NO_ERROR) {
    snprintf(err, errcap, "GL error %u while uploading mesh", (unsigned)e);
    mesh_free(out);
    return -1;
  }
  return 0;
}

void mesh_free(gpu_mesh* m) {
  if (!m) return;
  if (m->vbo) my_glDeleteBuffers(1, &m->vbo);
  if (m->vao) my_glDeleteVertexArrays(1, &m->vao);
  memset(m, 0, sizeof *m);
}
