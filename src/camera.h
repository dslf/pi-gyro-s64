#ifndef VIEWER_CAMERA_H
#define VIEWER_CAMERA_H

#include "vmath.h"
#include "obj.h"

typedef struct {
  float yaw;      /* radians around the Y axis */
  float pitch;    /* radians, clamped to +-89 deg */
  float dist;     /* distance from target */
  vec3  target;
  float fit;      /* bounding radius of the loaded model, used for framing */
  float fov_deg;  /* vertical field of view (perspective mode) */
  float aspect;   /* window aspect the framing was computed for */
  int   ortho;    /* 1 = parallel rays, 0 = perspective */
} camera;

#define FOV_MIN 8.0f
#define FOV_MAX 90.0f
#define DEFAULT_FOV 60.0f   /* perspective is the primary projection: at 60 deg the
                              eye sits at 2.16 * fit, so foreshortening is visible */

void  camera_reset(camera* c, float fit);        /* yaw/pitch/dist/fov back to defaults */
void  camera_set_aspect(camera* c, float aspect); /* keep the model framed on resize */
vec3  camera_eye(const camera* c);
mat4  camera_view(const camera* c);
mat4  camera_projection(const camera* c, float aspect);

void  camera_orbit(camera* c, float dx, float dy);      /* pixels */
void  camera_zoom(camera* c, float steps);              /* scroll steps */
void  camera_pan(camera* c, float dx, float dy);        /* pixels */
void  camera_set_fov(camera* c, float deg);             /* clamped, keeps the object framed */
void  camera_fov_step(camera* c, float delta_deg);
void  camera_toggle_proj(camera* c);                    /* perspective <-> orthographic */
void  camera_set_proj(camera* c, int ortho);            /* explicit projection choice */
/* Orientation from degrees. Pitch is clamped to the pole limit so look_at never
   degenerates; yaw wraps into +-180 so the stored value stays a readable angle. */
void  camera_set_orientation(camera* c, float yaw_deg, float pitch_deg);
/* Distance that frames the bounding sphere at the current fov, scaled by dist_scale
   (1.0 = the auto-fit distance). The settings widget reframes instead of zooming. */
void  camera_reframe(camera* c, float dist_scale);

/* Pick the triangle under a cursor (pixels, origin at the window's top-left).
   Returns the triangle index, or -1 when nothing is under the cursor. When a
   triangle is found, *depth_out holds its NDC depth in [-1,1]: smaller is nearer
   the eye in both projections, which is what makes this a depth test rather than
   a first-hit-wins scan. */
int   pick_triangle(const camera* c, const obj_mesh* m, double mx, double my, int w, int h,
                    float aspect, float* depth_out);

#endif
