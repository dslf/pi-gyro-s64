#ifndef VIEWER_OBJ_H
#define VIEWER_OBJ_H

#include "vmath.h"

#define OBJ_HAS_NORMALS     1   /* the v array carries a normal per corner */
#define OBJ_HAS_TEXCOORDS   2   /* the v array carries a uv per corner */
#define OBJ_NORMALS_AUTHORED 4  /* those normals came from vn lines in the file */

/* CPU-side triangle list produced by the OBJ parser. */
typedef struct {
  float* v;        /* vcount * 8: x y z  nx ny nz  u v */
  int    vcount;   /* multiple of 3 */
  int    triangles;
  int    faces;    /* faces parsed (polygons; a polygon of k corners makes k-2 triangles) */
  int*   face_ids; /* triangles: 0-based ordinal of the face that produced each triangle */
  int*   vert_ids; /* vcount: 1-based vertex number in the file for each corner */
  int    flags;
  float  radius;   /* bounding radius around the origin */
  float  span[3];  /* bounding box extents (normalised, after the up-axis remap) */
  char   name[128];
} obj_mesh;

int obj_parse_file(const char* path, obj_mesh* out, char* err, size_t errcap);
int obj_parse_string(const char* text, size_t len, const char* name, obj_mesh* out, char* err, size_t errcap);
void obj_mesh_free(obj_mesh* m);

/* Normal of a triangle: the authored vn of its first corner when the file carries vn lines,
   otherwise the cross product of its edges (the same value the parser stored). Returns the
   length of that normal (0 for a degenerate triangle). dst gets a unit vector. */
int obj_face_normal(const obj_mesh* m, int tri, float dst[3]);

/* Vertex normals of triangle `tri` in dst[0..2] (unit vectors). Returns 1 when the model
   carries authored normals (vn lines), 0 when the parser computed them (dst holds the face
   normal for every corner). */
int obj_vertex_normals(const obj_mesh* m, int tri, float dst[3][3]);

/* Largest angle between the authored vertex normals of a triangle and its face normal, in
   degrees. Returns 0 when the model carries no authored normals (out_deg set to 0). */
int obj_normal_deviation_deg(const obj_mesh* m, int tri, float* out_deg);

/* The file's own identification of a triangle: the 0-based ordinal of the authored face it
   came from and the 1-based vertex numbers of its three corners, exactly as written in the
   f-line. Returns -1 with a reason in err when the triangle or the mesh is unusable. */
int obj_triangle_identity(const obj_mesh* m, int tri, int* face_out, int* v0, int* v1, int* v2,
                          char* err, size_t errcap);

#endif
