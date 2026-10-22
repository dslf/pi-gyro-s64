/* temporary probe: does the point on the eye->target axis project to the
   centre of the screen? (sanity check of look_at / map_point / mul order) */
#include "src/camera.h"
#include "src/vmath.h"
#include <stdio.h>

int main(void) {
  camera c;
  camera_reset(&c, 1.0f);
  camera_set_fov(&c, 60.0f);
  printf("yaw %.2f pitch %.2f dist %.3f fit %.3f ortho %d\n", c.yaw, c.pitch, c.dist, c.fit, c.ortho);

  vec3 eye = camera_eye(&c);
  printf("eye %.3f %.3f %.3f\n", eye.x, eye.y, eye.z);

  vec3 dir = vec3_normalize(vec3_sub(eye, c.target));
  vec3 p = c.target;
  p.x += dir.x * c.fit; p.y += dir.y * c.fit; p.z += dir.z * c.fit;
  printf("axis point %.3f %.3f %.3f\n", p.x, p.y, p.z);

  mat4 view = camera_view(&c);
  printf("view m: %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f\n",
         view.m[0], view.m[1], view.m[2], view.m[3], view.m[4], view.m[5], view.m[6], view.m[7],
         view.m[8], view.m[9], view.m[10], view.m[11], view.m[12], view.m[13], view.m[14], view.m[15]);
  vec3 zaxis = vec3_normalize(vec3_sub(eye, c.target));
  vec3 xaxis = vec3_normalize(vec3_cross((vec3){ 0.0f, 1.0f, 0.0f }, zaxis));
  vec3 yaxis = vec3_cross(zaxis, xaxis);
  printf("basis x %.4f %.4f %.4f  y %.4f %.4f %.4f  z %.4f %.4f %.4f\n",
         xaxis.x, xaxis.y, xaxis.z, yaxis.x, yaxis.y, yaxis.z, zaxis.x, zaxis.y, zaxis.z);
  float w = 0.0f;
  vec3 v = m4_map_point(&view, p, &w);
  printf("view space %.4f %.4f %.4f (w %.4f)\n", v.x, v.y, v.z, w);

  mat4 proj = camera_projection(&c, 1.0f);
  mat4 vp = m4_mul(&proj, &view);
  vec3 q = m4_map_point(&vp, p, &w);
  printf("ndc %.4f %.4f %.4f (w %.4f)\n", q.x / w, q.y / w, q.z / w, w);

  /* an off-axis point, to confirm the view basis is right-handed and upright */
  vec3 r = (vec3){ 1.0f, 0.0f, 0.0f };
  vec3 q2 = m4_map_point(&vp, r, &w);
  printf("+x world point -> ndc %.4f %.4f\n", q2.x / w, q2.y / w);
  vec3 u = (vec3){ 0.0f, 1.0f, 0.0f };
  vec3 q3 = m4_map_point(&vp, u, &w);
  printf("+y world point -> ndc %.4f %.4f\n", q3.x / w, q3.y / w);
  return 0;
}
