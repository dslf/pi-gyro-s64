#include "vmath.h"

#include <string.h>

float deg2rad(float d) { return d * 0.017453292519943295f; }

vec3 vec3_normalize(vec3 v) {
  float l = vec3_length(v);
  if (l == 0.0f) return v;
  v.x /= l; v.y /= l; v.z /= l;
  return v;
}

float vec3_length(vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

float vec3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

vec3 vec3_sub(vec3 a, vec3 b) {
  vec3 r; r.x = a.x - b.x; r.y = a.y - b.y; r.z = a.z - b.z;
  return r;
}

vec3 vec3_cross(vec3 a, vec3 b) {
  vec3 r;
  r.x = a.y * b.z - a.z * b.y;
  r.y = a.z * b.x - a.x * b.z;
  r.z = a.x * b.y - a.y * b.x;
  return r;
}

mat4 m4_identity(void) {
  mat4 r;
  memset(&r, 0, sizeof r);
  r.m[0] = 1.0f; r.m[5] = 1.0f; r.m[10] = 1.0f; r.m[15] = 1.0f;
  return r;
}

mat4 m4_mul(const mat4* a, const mat4* b) {
  mat4 r;
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) sum += a->m[k * 4 + row] * b->m[col * 4 + k];
      r.m[col * 4 + row] = sum;
    }
  }
  return r;
}

mat4 m4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
  float f = 1.0f / tanf(fovy_rad * 0.5f);
  mat4 r;
  memset(&r, 0, sizeof r);
  r.m[0]  = f / aspect;
  r.m[5]  = f;
  /* OpenGL maps eye z = -znear -> ndc -1 and z = -zfar -> ndc +1, so m[10] and
     m[14] must be NEGATIVE. Flipped signs would invert depth ordering: the far
     faces would win the GL_LESS test and the model would render inside-out. */
  r.m[10] = -(zfar + znear) / (zfar - znear);
  r.m[11] = -1.0f;
  r.m[14] = -(2.0f * zfar * znear) / (zfar - znear);
  return r;
}

mat4 m4_ortho(float left, float right, float bottom, float top, float znear, float zfar) {
  float w = right - left, h = top - bottom, d = zfar - znear;
  mat4 r;
  memset(&r, 0, sizeof r);
  r.m[0]  = 2.0f / w;
  r.m[5]  = 2.0f / h;
  r.m[10] = -2.0f / d;
  r.m[15] = 1.0f;
  r.m[12] = -(right + left) / w;
  r.m[13] = -(top + bottom) / h;
  r.m[14] = -(zfar + znear) / d;
  return r;
}

vec3 m4_map_point(const mat4* m, vec3 p, float* w) {
  vec3 r;
  float ww = m->m[3] * p.x + m->m[7] * p.y + m->m[11] * p.z + m->m[15];
  r.x = m->m[0] * p.x + m->m[4] * p.y + m->m[8] * p.z + m->m[12];
  r.y = m->m[1] * p.x + m->m[5] * p.y + m->m[9] * p.z + m->m[13];
  r.z = m->m[2] * p.x + m->m[6] * p.y + m->m[10] * p.z + m->m[14];
  if (w) *w = ww;
  return r;
}

mat4 m4_look_at(vec3 eye, vec3 target, vec3 up) {
  vec3 z = vec3_normalize(vec3_sub(eye, target));
  vec3 x = vec3_normalize(vec3_cross(up, z));
  vec3 y = vec3_cross(z, x);

  /* Column-major storage: column k holds the k-th ROW of the view matrix
     (x, y, z) read as (x.x, y.x, z.x), (x.y, y.y, z.y), (x.z, y.z, z.z).
     Storing each basis vector as its own column instead would transpose the
     rotation, so the eye would look along a different axis and the depth
     functional would no longer be distance-from-eye - that renders the model
     inside-out (far faces pass the GL_LESS test). */
  mat4 r;
  memset(&r, 0, sizeof r);
  r.m[0]  = x.x; r.m[1]  = y.x; r.m[2]  = z.x;  r.m[3]  = 0.0f;
  r.m[4]  = x.y; r.m[5]  = y.y; r.m[6]  = z.y;  r.m[7]  = 0.0f;
  r.m[8]  = x.z; r.m[9]  = y.z; r.m[10] = z.z;  r.m[11] = 0.0f;
  r.m[12] = -vec3_dot(x, eye);
  r.m[13] = -vec3_dot(y, eye);
  r.m[14] = -vec3_dot(z, eye);
  r.m[15] = 1.0f;
  return r;
}

mat4 m4_rotation(vec3 axis, float rad) {
  vec3 a = vec3_normalize(axis);
  float c = cosf(rad), s = sinf(rad), t = 1.0f - c;
  mat4 r;
  memset(&r, 0, sizeof r);
  r.m[0]  = c + a.x * a.x * t; r.m[1]  = a.x * a.y * t + a.z * s; r.m[2]  = a.x * a.z * t - a.y * s; r.m[3]  = 0.0f;
  r.m[4]  = a.x * a.y * t - a.z * s; r.m[5] = c + a.y * a.y * t;  r.m[6]  = a.y * a.z * t + a.x * s; r.m[7]  = 0.0f;
  r.m[8]  = a.x * a.z * t + a.y * s; r.m[9] = a.y * a.z * t - a.x * s; r.m[10] = c + a.z * a.z * t;  r.m[11] = 0.0f;
  r.m[12] = 0.0f; r.m[13] = 0.0f; r.m[14] = 0.0f; r.m[15] = 1.0f;
  return r;
}

static float minor4(const mat4* m, int a, int b, int c, int d, int e, int f, int g, int h) {
  /* rows a,b,c,d and columns e,f,g,h of a 4x4, stored column-major */
  float m00 = m->m[e * 4 + a], m01 = m->m[f * 4 + a], m02 = m->m[g * 4 + a], m03 = m->m[h * 4 + a];
  float m10 = m->m[e * 4 + b], m11 = m->m[f * 4 + b], m12 = m->m[g * 4 + b], m13 = m->m[h * 4 + b];
  float m20 = m->m[e * 4 + c], m21 = m->m[f * 4 + c], m22 = m->m[g * 4 + c], m23 = m->m[h * 4 + c];
  float m30 = m->m[e * 4 + d], m31 = m->m[f * 4 + d], m32 = m->m[g * 4 + d], m33 = m->m[h * 4 + d];
  return m00 * (m11 * (m22 * m33 - m23 * m32) - m12 * (m21 * m33 - m23 * m31) + m13 * (m21 * m32 - m22 * m31))
       - m01 * (m10 * (m22 * m33 - m23 * m32) - m12 * (m20 * m33 - m23 * m30) + m13 * (m20 * m32 - m22 * m30))
       + m02 * (m10 * (m21 * m33 - m23 * m31) - m11 * (m20 * m33 - m23 * m30) + m13 * (m20 * m31 - m21 * m30))
       - m03 * (m10 * (m21 * m32 - m22 * m31) - m11 * (m20 * m32 - m22 * m30) + m12 * (m20 * m31 - m21 * m30));
}

mat4 m4_inverse(const mat4* m) {
  float cof[16];
  /* cof[col*4+row] = signed minor that removes row 'row' and column 'col' */
  for (int row = 0; row < 4; row++) {
    int r[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 4; i++) if (r[i] == row) { r[i] = 4; }
    for (int col = 0; col < 4; col++) {
      int c[4] = { 0, 1, 2, 3 };
      for (int i = 0; i < 4; i++) if (c[i] == col) { c[i] = 4; }
      cof[col * 4 + row] = ((row + col) & 1) ? -minor4(m, r[0], r[1], r[2], r[3], c[0], c[1], c[2], c[3])
                                             :  minor4(m, r[0], r[1], r[2], r[3], c[0], c[1], c[2], c[3]);
    }
  }
  /* determinant = sum over the first row of m * cofactor */
  float det = m->m[0] * cof[0] + m->m[4] * cof[1] + m->m[8] * cof[2] + m->m[12] * cof[3];
  mat4 inv;
  memset(&inv, 0, sizeof inv);
  if (det == 0.0f) return inv;
  float k = 1.0f / det;
  /* transpose the cofactor matrix */
  for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++)
      inv.m[col * 4 + row] = cof[row * 4 + col] * k;
  return inv;
}

mat3 m3_normal(const mat4* model) {
  float a00 = model->m[0], a01 = model->m[1], a02 = model->m[2];
  float a10 = model->m[4], a11 = model->m[5], a12 = model->m[6];
  float a20 = model->m[8], a21 = model->m[9], a22 = model->m[10];

  float c00 = a11 * a22 - a12 * a21;
  float c01 = a12 * a20 - a10 * a22;
  float c02 = a10 * a21 - a11 * a20;
  float c10 = a21 * a02 - a22 * a01;
  float c11 = a22 * a00 - a20 * a02;
  float c12 = a20 * a01 - a21 * a00;
  float c20 = a01 * a12 - a02 * a11;
  float c21 = a02 * a10 - a00 * a12;
  float c22 = a00 * a11 - a01 * a10;

  float det = a00 * c00 + a10 * c01 + a20 * c02;
  mat3 n;
  memset(&n, 0, sizeof n);
  if (det == 0.0f) {
    /* Degenerate model matrix: fall back to the raw rotation part. */
    n.m[0] = a00; n.m[1] = a10; n.m[2] = a20;
    n.m[3] = a01; n.m[4] = a11; n.m[5] = a21;
    n.m[6] = a02; n.m[7] = a12; n.m[8] = a22;
    return n;
  }
  float k = 1.0f / det;
  /* inverse-transpose of a 3x3 is just the cofactor matrix scaled */
  n.m[0] = c00 * k; n.m[1] = c01 * k; n.m[2] = c02 * k;
  n.m[3] = c10 * k; n.m[4] = c11 * k; n.m[5] = c12 * k;
  n.m[6] = c20 * k; n.m[7] = c21 * k; n.m[8] = c22 * k;
  return n;
}
