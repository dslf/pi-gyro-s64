/* CPU rasteriser reference for the GPU path: same obj.c mesh, same camera.c
   matrices, same GL conventions (column-major m4_mul, w-divide, ndc z in
   [-1,1], GL_LESS depth, y down in the image). Compares the resulting
   silhouette against viewer.exe's BMP so the perspective foreshortening,
   depth order and vertical direction are checked against a reference. */
#include "obj.h"
#include "camera.h"
#include "vmath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 400
#define H 300

static unsigned char* fg;      /* 1 = covered by a front-facing triangle */
static float* depth;           /* ndc z of the nearest fragment */

/* Map through the vp matrix with the implicit w=1, then divide. */
static void ndc(const mat4* m, vec3 p, float* x, float* y, float* z) {
  float w = 1.0f;
  vec3 q = m4_map_point(m, p, &w);
  if (w == 0.0f) w = 1.0f;
  *x = q.x / w; *y = q.y / w; *z = q.z / w;
}

static void draw_tri(const float* a, const float* b, const float* c, const mat4* vp) {
  vec3 pa = { a[0], a[1], a[2] }, pb = { b[0], b[1], b[2] }, pc = { c[0], c[1], c[2] };
  float ax, ay, az, bx, by, bz, cx, cy, cz;
  ndc(vp, pa, &ax, &ay, &az); ndc(vp, pb, &bx, &by, &bz); ndc(vp, pc, &cx, &cy, &cz);
  ax = (ax + 1.0f) * 0.5f * W; ay = (1.0f - ay) * 0.5f * H;
  bx = (bx + 1.0f) * 0.5f * W; by = (1.0f - by) * 0.5f * H;
  cx = (cx + 1.0f) * 0.5f * W; cy = (1.0f - cy) * 0.5f * H;

  float minx = fminf(fminf(ax, bx), cx), maxx = fmaxf(fmaxf(ax, bx), cx);
  float miny = fminf(fminf(ay, by), cy), maxy = fmaxf(fmaxf(ay, by), cy);
  if (minx < 0) minx = 0;
  if (maxx > W - 1) maxx = W - 1;
  if (miny < 0) miny = 0;
  if (maxy > H - 1) maxy = H - 1;
  if (az < -1.0f || az > 1.0f) return;   /* clipped away (near/far plane) */

  float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  if (fabsf(area) < 1e-6f) return;

  for (int y = (int)floorf(miny); y <= (int)ceilf(maxy); y++) {
    for (int x = (int)floorf(minx); x <= (int)ceilf(maxx); x++) {
      float px = x + 0.5f, py = y + 0.5f;
      /* Dividing by the signed area gives correct weights for either winding. */
      float w0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / area;
      float w1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) / area;
      float w2 = 1.0f - w0 - w1;
      if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
      float z = w0 * az + w1 * bz + w2 * cz;
      size_t i = (size_t)y * W + x;
      if (z < depth[i]) { depth[i] = z; fg[i] = 1; }   /* GL_LESS */
    }
  }
}

static int band_top(int k, int* width, float* zmean) {
  int first = -1;
  for (int y = 0; y < H && first < 0; y++)
    for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) { first = y; break; }
  if (first < 0 || first + k > H) return -1;
  int lo = 1 << 20, hi = -1; double zsum = 0.0; int n = 0;
  for (int y = first; y < first + k; y++)
    for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) {
      if (x < lo) lo = x; if (x > hi) hi = x; zsum += depth[(size_t)y * W + x]; n++;
    }
  if (hi < lo || !n) return -1;
  *width = hi - lo + 1; *zmean = zsum / n; return 0;
}

int main(void) {
  obj_mesh m; char err[256] = "";
  if (obj_parse_file("assets/utah_teapot.obj", &m, err, sizeof err) < 0) { fprintf(stderr, "%s\n", err); return 1; }
  printf("fit %.3f (bounding radius) span %.3f %.3f %.3f\n", m.radius, m.span[0], m.span[1], m.span[2]);

  camera cam; camera_reset(&cam, m.radius);
  camera_set_aspect(&cam, (float)W / (float)H);
  mat4 view = camera_view(&cam);
  printf("perspective\n");
  mat4 proj = camera_projection(&cam, (float)W / (float)H);
  printf("  proj rows: [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]\n",
         proj.m[0], proj.m[4], proj.m[8], proj.m[12], proj.m[1], proj.m[5], proj.m[9], proj.m[13],
         proj.m[2], proj.m[6], proj.m[10], proj.m[14], proj.m[3], proj.m[7], proj.m[11], proj.m[15]);
  mat4 vp = m4_mul(&proj, &view);
  printf("  eye dist %.3f  pitch %.3f yaw %.3f\n", cam.dist, cam.pitch, cam.yaw);

  /* where the top and bottom of the model land, before rasterising */
  float bmn[3] = { 1e30f, 1e30f, 1e30f }, bmx[3] = { -1e30f, -1e30f, -1e30f };
  for (int i = 0; i < m.vcount; i++)
    for (int k = 0; k < 3; k++) {
      float v = m.v[i * 8 + k];
      if (v < bmn[k]) bmn[k] = v;
      if (v > bmx[k]) bmx[k] = v;
    }
  printf("  measured bbox min %.3f %.3f %.3f max %.3f %.3f %.3f (obj.c span %.3f %.3f %.3f)\n",
         bmn[0], bmn[1], bmn[2], bmx[0], bmx[1], bmx[2], m.span[0], m.span[1], m.span[2]);
  vec3 top = { 0.0f, bmx[1], 0.0f }, bot = { 0.0f, bmn[1], 0.0f };
  float tx, ty, tz, bx2, by2, bz2;
  ndc(&vp, top, &tx, &ty, &tz); ndc(&vp, bot, &bx2, &by2, &bz2);
  printf("  model top: ndc y %.3f z %.3f | bottom: ndc y %.3f z %.3f\n", ty, tz, by2, bz2);

  fg = calloc((size_t)W * H, 1); depth = malloc((size_t)W * H * sizeof(float));
  for (size_t i = 0; i < (size_t)W * H; i++) depth[i] = 1e9f;

  int skipped = 0;
  for (int t = 0; t < m.triangles; t++) {
    const float* a = m.v + (size_t)t * 24;
    const float* b = m.v + (size_t)t * 24 + 8;
    const float* c = m.v + (size_t)t * 24 + 16;
    float ax, ay, az;
    ndc(&vp, (vec3){ a[0], a[1], a[2] }, &ax, &ay, &az);
    if (az < -1.0f || az > 1.0f) { skipped++; continue; }
    draw_tri(a, b, c, &vp);
  }
  int cov = 0, xlo = 1 << 20, xhi = -1, ylo = 1 << 20, yhi = -1;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) {
    cov++; if (x < xlo) xlo = x; if (x > xhi) xhi = x; if (y < ylo) ylo = y; if (y > yhi) yhi = y;
  }
  printf("  coverage %d px, bbox x %d..%d y %d..%d, %d triangles skipped by the z range\n",
         cov, xlo, xhi, ylo, yhi, skipped);
  int wp, wb; float zp, zb;
  if (band_top(16, &wp, &zp) == 0) {
    /* the bottom band, counted from the last covered row upwards */
    int last = -1;
    for (int y = H - 1; y >= 0; y--) for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) { last = y; break; }
    int lo = 1 << 20, hi = -1; double zsum = 0.0; int n = 0;
    for (int y = last - 15; y <= last; y++) for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) {
      if (x < lo) lo = x; if (x > hi) hi = x; zsum += depth[(size_t)y * W + x]; n++;
    }
    wb = hi - lo + 1; zb = zsum / n;
    printf("  CPU raster: top band %d px (z %.3f) bottom band %d px (z %.3f) -> base/top %.2f\n",
           wp, zp, wb, zb, (float)wb / (float)wp);
  }

  /* orthographic reference */
  for (size_t i = 0; i < (size_t)W * H; i++) { depth[i] = 1e9f; fg[i] = 0; }
  camera_toggle_proj(&cam);
  proj = camera_projection(&cam, (float)W / (float)H);
  vp = m4_mul(&proj, &view);
  for (int t = 0; t < m.triangles; t++) {
    const float* a = m.v + (size_t)t * 24;
    const float* b = m.v + (size_t)t * 24 + 8;
    const float* c = m.v + (size_t)t * 24 + 16;
    float ax, ay, az;
    ndc(&vp, (vec3){ a[0], a[1], a[2] }, &ax, &ay, &az);
    if (az < -1.0f || az > 1.0f) continue;
    draw_tri(a, b, c, &vp);
  }
  int cov2 = 0;
  for (size_t i = 0; i < (size_t)W * H; i++) cov2 += fg[i] != 0;
  printf("  ortho coverage %d px\n", cov2);
  int wo = 0, bo = 0;
  float zo = 0.0f, bo_z = 0.0f;
  band_top(16, &wo, &zo);
  int last = -1;
  for (int y = H - 1; y >= 0; y--) for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) { last = y; break; }
  int lo = 1 << 20, hi = -1; double zsum = 0.0; int n = 0;
  for (int y = last - 15; y <= last; y++) for (int x = 0; x < W; x++) if (fg[(size_t)y * W + x]) {
    if (x < lo) lo = x; if (x > hi) hi = x; zsum += depth[(size_t)y * W + x]; n++;
  }
  bo = hi - lo + 1; bo_z = zsum / n;
  printf("  ortho raster: top band %d px (z %.3f) bottom band %d px (z %.3f) -> base/top %.2f\n",
         wo, zo, bo, bo_z, (float)bo / (float)wo);

  obj_mesh_free(&m);
  free(fg); free(depth);
  return 0;
}
