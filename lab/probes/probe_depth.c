/* temporary probe: what NDC depth does a point at a given eye distance get? */
#include "src/camera.h"
#include <stdio.h>

int main(void) {
  camera c;
  camera_reset(&c, 1.0f);
  c.yaw = 0.0f; c.pitch = 0.0f;          /* eye on +z looking at origin */
  mat4 proj = camera_projection(&c, 1.0f);
  mat4 view = camera_view(&c);
  mat4 vp = m4_mul(&proj, &view);

  
  /* recover near/far from the matrix by mapping the model's nearest/farthest points */
  for (int i = 0; i < 3; i++) {
    float d = c.fit - i * c.fit;   /* nearest to eye, target, farthest */
    vec3 p = (vec3){ 0.0f, 0.0f, d };    /* along the eye->target line, +z toward eye */
    float w = 0.0f;
    vec3 q = m4_map_point(&vp, p, &w);
    printf("world z=%.2f (eye dist %.2f) -> ndc z %.4f, w %.4f\n", d, c.dist - d, q.z / w, w);
  }
  printf("perspective m[10]=%.4f m[11]=%.4f m[14]=%.4f\n", proj.m[10], proj.m[11], proj.m[14]);
  printf("ortho m[10]=%.4f m[14]=%.4f\n", m4_ortho(-1,1,-1,1,0.1f,10.0f).m[10], m4_ortho(-1,1,-1,1,0.1f,10.0f).m[14]);
  return 0;
}

