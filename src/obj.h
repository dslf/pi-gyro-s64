#ifndef VIEWER_OBJ_H
#define VIEWER_OBJ_H

#include "vmath.h"

#define OBJ_HAS_NORMALS    1
#define OBJ_HAS_TEXCOORDS  2

/* CPU-side triangle list produced by the OBJ parser. */
typedef struct {
  float* v;        /* vcount * 8: x y z  nx ny nz  u v */
  int    vcount;   /* multiple of 3 */
  int    triangles;
  int    flags;
  float  radius;   /* bounding radius around the origin */
  float  span[3];  /* bounding box extents (normalised, after the up-axis remap) */
  char   name[128];
} obj_mesh;

int obj_parse_file(const char* path, obj_mesh* out, char* err, size_t errcap);
int obj_parse_string(const char* text, size_t len, const char* name, obj_mesh* out, char* err, size_t errcap);
void obj_mesh_free(obj_mesh* m);

#endif
