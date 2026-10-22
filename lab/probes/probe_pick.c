/* What does pick_triangle see? Print NDC for each synthetic triangle at a pixel. */
#include "camera.h"
#include "mesh.h"
#include "vmath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  obj_mesh m; memset(&m, 0, sizeof m);
  m.vcount = 12; m.triangles = 4; m.radius = 1.0f;
  m.v = calloc(12 * 8, sizeof(float));
  const float tri[4][3][3] = {
    { { -0.4f, 0.4f, -0.5f }, { 0.4f, 0.4f, -0.5f }, { 0.0f, -0.4f, -0.5f } },
    { { -0.15f, 0.15f, 0.5f }, { 0.15f, 0.15f, 0.5f }, { 0.0f, -0.15f, 0.5f } },
    { { -0.9f, 0.2f, -0.5f }, { -0.3f, 0.2f, -0.5f }, { -0.6f, 0.45f, -0.5f } },
    { { -0.9f, -0.2f, -0.5f }, { -0.3f, -0.2f, -0.5f }, { -0.6f, -0.45f, -0.5f } },
  };
  for (int t = 0; t < 4; t++)
    for (int i = 0; i < 3; i++) {
      float* v = &m.v[(size_t)(t * 3 + i) * 8u];
      v[0] = tri[t][i][0]; v[1] = tri[t][i][1]; v[2] = tri[t][i][2];
      v[3] = 1.0f;
    }
  camera c; memset(&c, 0, sizeof c);
  camera_reset(&c, m.radius);
  c.yaw = 0; c.pitch = 0; c.dist = 2.0f; c.aspect = 1.0f;
  mat4 proj = camera_projection(&c, 1.0f);
  mat4 view = camera_view(&c);
  mat4 vp = m4_mul(&proj, &view);
  printf("eye=(%.2f %.2f %.2f) dist=%.2f fov=%.1f\n",
         camera_eye(&c).x, camera_eye(&c).y, camera_eye(&c).z, c.dist, c.fov_deg);
  for (int t = 0; t < 4; t++) {
    printf("tri %d:", t);
    for (int i = 0; i < 3; i++) {
      const float* vtx = &m.v[(size_t)(t * 3 + i) * 8u];
      float hw = 0;
      vec3 q = m4_map_point(&vp, (vec3){ vtx[0], vtx[1], vtx[2] }, &hw);
      printf(" (%.3f %.3f z%.3f hw%.3f)", q.x / hw, q.y / hw, q.z / hw, hw);
    }
    printf("\n");
  }
  for (int i = 0; i < 4; i++) {
    double px[2] = { 64, 38 }, py[2] = { 50, 45 }, pt[2] = { 30, 35 }, pb[2] = { 30, 65 };
    const double* p = i == 0 ? px : i == 1 ? py : i == 2 ? pt : pb;
    printf("pixel (%.0f,%.0f) ndc (%.3f %.3f)\n", p[0], p[1], 2.0*p[0]/100 - 1.0, 1.0 - 2.0*p[1]/100);
  }
  free(m.v);
  return 0;
}
