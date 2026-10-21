#include "camera.h"

#include <math.h>

#include "mesh.h"   /* MESH_STRIDE_FLOATS: the interleaved layout cursor picking reads */

#define PITCH_LIMIT (1.53f)   /* +-87 deg, keeps look_at from degenerating at the poles */
#define DIST_MIN    0.05f
#define DIST_MAX    50.0f
/* DEFAULT_FOV comes from camera.h - perspective is the primary projection. */

/* Distance at which a sphere of radius `fit` exactly fills the vertical fov. */
static float frame_dist(float fit, float fov_deg) {
  return fit / sinf(deg2rad(fov_deg) * 0.5f) * 1.08f;   /* 8% margin, never cropped */
}

void camera_reset(camera* c, float fit) {
  c->fit = fit > 1e-4f ? fit : 1.0f;
  c->yaw = 0.6f;
  c->pitch = 0.25f;
  c->fov_deg = DEFAULT_FOV;
  c->ortho = 0;
  c->aspect = 1.0f;
  /* fit is the bounding radius: the eye sits far enough that the whole sphere
     lands inside the vertical fov, with a margin so nothing is cropped. */
  c->dist = frame_dist(c->fit, c->fov_deg);
  c->target.x = 0.0f; c->target.y = 0.0f; c->target.z = 0.0f;
}

/* A tall window (aspect < 1) crops horizontally, so push the eye back until the
   bounding sphere fits the tighter of the two half-angles. The user's own zoom
   is preserved: only the ratio between old and new aspect is applied. */
static float aspect_need(float aspect) { return aspect < 1.0f ? 1.0f / aspect : 1.0f; }

void camera_set_aspect(camera* c, float aspect) {
  if (aspect <= 1e-3f) return;
  float prev = c->aspect > 1e-3f ? c->aspect : aspect;
  float k = aspect_need(aspect) / aspect_need(prev);
  if (k != 1.0f) {
    c->dist *= k;
    if (c->dist > DIST_MAX) c->dist = DIST_MAX;
    if (c->dist < DIST_MIN) c->dist = DIST_MIN;
  }
  c->aspect = aspect;
}

vec3 camera_eye(const camera* c) {
  vec3 e;
  e.x = c->target.x + c->dist * cosf(c->pitch) * sinf(c->yaw);
  e.y = c->target.y + c->dist * sinf(c->pitch);
  e.z = c->target.z + c->dist * cosf(c->pitch) * cosf(c->yaw);
  return e;
}

mat4 camera_view(const camera* c) {
  return m4_look_at(camera_eye(c), c->target, (vec3){ 0.0f, 1.0f, 0.0f });
}

/* Near/far hug the model: the range is measured from the eye to the model's
   centre (the origin - obj.c recentres the mesh), not from the orbit distance,
   so panning cannot push the model outside [near, far] and depth precision
   stays high at every zoom level. */
mat4 camera_projection(const camera* c, float aspect) {
  float centre = vec3_length(camera_eye(c));
  float znear = centre - c->fit * 1.2f;
  if (znear < c->fit * 0.01f + 0.001f) znear = c->fit * 0.01f + 0.001f;
  float zfar = centre + c->fit * 1.2f;
  if (zfar < znear * 1.5f + 0.01f) zfar = znear * 1.5f + 0.01f;

  if (c->ortho) {
    /* Same frustum cross-section as perspective at this distance, so toggling
       projection does not change the size of the object on screen. */
    float half = c->dist * tanf(deg2rad(c->fov_deg) * 0.5f);
    return m4_ortho(-half * aspect, half * aspect, -half, half, znear, zfar);
  }
  return m4_perspective(deg2rad(c->fov_deg), aspect, znear, zfar);
}

void camera_set_fov(camera* c, float deg) {
  if (deg < FOV_MIN) deg = FOV_MIN;
  if (deg > FOV_MAX) deg = FOV_MAX;
  /* The object must stay the same size on screen, so distance follows sin(fov/2):
     a wider fov with a closer eye is what makes perspective visibly distort. */
  float k = sinf(deg2rad(c->fov_deg) * 0.5f) / sinf(deg2rad(deg) * 0.5f);
  c->fov_deg = deg;
  c->dist *= k;
  if (c->dist < DIST_MIN) c->dist = DIST_MIN;
  if (c->dist > DIST_MAX) c->dist = DIST_MAX;
}

void camera_fov_step(camera* c, float delta_deg) { camera_set_fov(c, c->fov_deg + delta_deg); }

void camera_toggle_proj(camera* c) { c->ortho = !c->ortho; }

void camera_orbit(camera* c, float dx, float dy) {
  c->yaw -= dx * 0.005f;
  c->pitch += dy * 0.005f;
  if (c->pitch > PITCH_LIMIT) c->pitch = PITCH_LIMIT;
  if (c->pitch < -PITCH_LIMIT) c->pitch = -PITCH_LIMIT;
  if (c->yaw > 6.2832f) c->yaw -= 6.2832f;
  if (c->yaw < -6.2832f) c->yaw += 6.2832f;
}

void camera_zoom(camera* c, float steps) {
  c->dist *= powf(0.9f, steps);
  if (c->dist < DIST_MIN) c->dist = DIST_MIN;
  if (c->dist > DIST_MAX) c->dist = DIST_MAX;
}

void camera_pan(camera* c, float dx, float dy) {
  vec3 eye = camera_eye(c);
  vec3 forward = vec3_normalize(vec3_sub(c->target, eye));
  vec3 up = (vec3){ 0.0f, 1.0f, 0.0f };
  vec3 right = vec3_normalize(vec3_cross(forward, up));
  vec3 upv = vec3_cross(right, forward);

  float k = c->dist * 0.002f;
  c->target.x += (-right.x * dx + upv.x * dy) * k;
  c->target.y += (-right.y * dx + upv.y * dy) * k;
  c->target.z += (-right.z * dx + upv.z * dy) * k;
}

/* Cursor picking. Every triangle goes through the same view-projection the frame
   was drawn with, so the pick agrees with what is on screen. The barycentric test
   runs in NDC, where the projection has already done the perspective division;
   the winner is chosen on interpolated NDC depth, which grows towards the far
   plane in both projections - so the nearest surface wins, not the first triangle
   in the file. */
int pick_triangle(const camera* c, const obj_mesh* m, double mx, double my, int w, int h,
                  float aspect, float* depth_out) {
  if (!c || !m || !m->v || m->triangles <= 0 || w <= 0 || h <= 0) return -1;

  mat4 proj = camera_projection(c, aspect);
  mat4 view = camera_view(c);
  mat4 vp = m4_mul(&proj, &view);

  /* Mouse pixels are top-left origin with y growing down; NDC y grows up. */
  float ndcx = (float)(mx / (double)w) * 2.0f - 1.0f;
  float ndcy = 1.0f - (float)(my / (double)h) * 2.0f;

  int best = -1;
  float best_z = 0.0f;

  for (int t = 0; t < m->triangles; t++) {
    vec3 q[3];
    float hw[3];
    int usable = 1;
    for (int i = 0; i < 3; i++) {
      const float* vtx = m->v + (size_t)(t * 3 + i) * MESH_STRIDE_FLOATS;
      /* hw is the homogeneous divisor: <= 0 means the vertex is behind the eye,
         where the projection mirrors it, so the triangle must never be picked. */
      q[i] = m4_map_point(&vp, (vec3){ vtx[0], vtx[1], vtx[2] }, &hw[i]);
      if (hw[i] <= 0.0f) { usable = 0; break; }
      q[i].x /= hw[i];
      q[i].y /= hw[i];
      q[i].z /= hw[i];
      if (q[i].x < -1.0f || q[i].x > 1.0f || q[i].y < -1.0f || q[i].y > 1.0f) { usable = 0; break; }
    }
    if (!usable) continue;   /* a vertex outside the frustum: partial spans are not clipped here */

    float e1x = q[1].x - q[0].x, e1y = q[1].y - q[0].y;
    float e2x = q[2].x - q[0].x, e2y = q[2].y - q[0].y;
    float den = e1x * e2y - e1y * e2x;
    if (fabsf(den) < 1e-8f) continue;   /* degenerate or edge-on: it covers no pixel */

    float px = ndcx - q[0].x, py = ndcy - q[0].y;
    float s = (px * e2y - py * e2x) / den;
    float r = (e1x * py - e1y * px) / den;
    if (s < 0.0f || r < 0.0f || s + r > 1.0f) continue;

    float z = q[0].z + s * (q[1].z - q[0].z) + r * (q[2].z - q[0].z);
    if (z < -1.0f || z > 1.0f) continue;   /* outside the near/far range */

    if (best < 0 || z < best_z) { best = t; best_z = z; }
  }

  if (depth_out) *depth_out = best_z;
  return best;
}
