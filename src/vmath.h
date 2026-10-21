#ifndef VIEWER_MATH_H
#define VIEWER_MATH_H

#include <math.h>

typedef struct { float x, y, z; } vec3;
typedef struct { float m[16]; } mat4;  /* column-major: element (row,col) is m[col*4+row] */
typedef struct { float m[9];  } mat3;  /* column-major */

vec3  vec3_normalize(vec3 v);
float deg2rad(float deg);
vec3  vec3_cross(vec3 a, vec3 b);
vec3  vec3_sub(vec3 a, vec3 b);
float vec3_dot(vec3 a, vec3 b);
float vec3_length(vec3 v);

mat4  m4_identity(void);
mat4  m4_mul(const mat4* a, const mat4* b);
mat4  m4_perspective(float fovy_rad, float aspect, float znear, float zfar);
mat4  m4_look_at(vec3 eye, vec3 target, vec3 up);
mat4  m4_rotation(vec3 axis, float rad);
mat4  m4_inverse(const mat4* m);

mat4  m4_ortho(float left, float right, float bottom, float top, float znear, float zfar);

/* Normal matrix = transpose(inverse(upper-left 3x3)). Column-major. */
mat3  m3_normal(const mat4* model);

/* Map a point through m; *w receives the homogeneous divisor (1 for ortho). */
vec3  m4_map_point(const mat4* m, vec3 p, float* w);

#endif
