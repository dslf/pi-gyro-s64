#include "obj.h"
#include <stdio.h>
int main(void) {
  obj_mesh m; char err[256];
  if (obj_parse_file("assets/utah_teapot.obj", &m, err, sizeof err) < 0) { fprintf(stderr, "%s\n", err); return 1; }
  float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
  for (int i = 0; i < m.vcount; i++)
    for (int k = 0; k < 3; k++) {
      float v = m.v[i * 8 + k];
      if (v < mn[k]) mn[k] = v;
      if (v > mx[k]) mx[k] = v;
    }
  printf("vcount %d tris %d bbox min %.3f %.3f %.3f max %.3f %.3f %.3f\n", m.vcount, m.triangles, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
  printf("measured spans %.3f %.3f %.3f  radius %.3f  reported span %.3f %.3f %.3f\n",
         mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], m.radius, m.span[0], m.span[1], m.span[2]);
  obj_mesh_free(&m);
  return 0;
}
