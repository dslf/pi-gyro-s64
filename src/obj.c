#include "obj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* --- growable float / int buffers ------------------------------------------- */
typedef struct { float* d; size_t n, cap; } float_arr;
typedef struct { int*   d; size_t n, cap; } int_arr;

static void f_reserve(float_arr* a, size_t add) {
  if (a->n + add > a->cap) {
    size_t cap = a->cap ? a->cap * 2 : 64;
    while (cap < a->n + add) cap *= 2;
    float* p = realloc(a->d, cap * sizeof(float));
    if (!p) { return; }
    a->d = p; a->cap = cap;
  }
}

static void f_push(float_arr* a, float v) { f_reserve(a, 1); if (a->cap >= a->n + 1) a->d[a->n++] = v; }
static void f_push3(float_arr* a, float x, float y, float z) { f_push(a, x); f_push(a, y); f_push(a, z); }

static void i_reserve(int_arr* a, size_t add) {
  if (a->n + add > a->cap) {
    size_t cap = a->cap ? a->cap * 2 : 64;
    while (cap < a->n + add) cap *= 2;
    int* p = realloc(a->d, cap * sizeof(int));
    if (!p) return;
    a->d = p; a->cap = cap;
  }
}
static void i_push(int_arr* a, int v) { i_reserve(a, 1); if (a->cap >= a->n + 1) a->d[a->n++] = v; }

/* A face corner: "p", "p/vt", "p/vt/vn" or "p//vn". -1 means absent. */
typedef struct { int p, t, n; } corner;

static int dup_str(const char* s, size_t len, char* dst, size_t cap) {
  size_t i = 0;
  for (; i < len && i + 1 < cap; i++) dst[i] = s[i];
  dst[i] = '\0';
  return 0;
}

/* Split a line into tokens separated by whitespace, ignoring '#' comments. */
static int tokenize_line(char* s, char* tok[], int max) {
  int n = 0;
  char* p = s;
  while (*p && n < max) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    if (*p == '#') break;
    tok[n++] = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = '\0';
  }
  return n;
}

/* Coordinates live in separate whitespace tokens, so parse one per token. */
static int parse_token_floats(char* tok[], int nt, float* out, int max) {
  int n = 0;
  for (int i = 1; i < nt && n < max; i++) {
    char* stop = NULL;
    double v = strtod(tok[i], &stop);
    if (stop == tok[i]) continue;   /* not a number (e.g. a v4 w field) - skip */
    out[n++] = (float)v;
  }
  return n;
}

static int parse_corner(const char* s, corner* c, int posn, int texn, int nrnn) {
  int val[3] = { -1, -1, -1 };
  int field = 0;
  const char* p = s;

  while (*p && field < 3) {
    if (*p != '/') {
      char* stop = NULL;
      long v = strtol(p, &stop, 10);
      if (stop != p) val[field] = (int)v;
      p = stop;
    }
    field++;
    if (*p == '/') p++;
    else break;
  }

  if (val[0] == 0) return -1;                 /* OBJ indices are 1-based; 0 is invalid */
  if (val[0] < 0) val[0] = posn + val[0];     /* negative = relative to the end */
  if (val[1] < 0) val[1] = texn + val[1];
  if (val[2] < 0) val[2] = nrnn + val[2];
  val[0]--; val[1]--; val[2]--;               /* to 0-based */

  c->p = val[0]; c->t = val[1]; c->n = val[2];
  if (c->p < 0 || c->p >= posn) return -1;
  if (c->t >= texn || c->t < 0) c->t = -1;    /* a missing uv is legal */
  if (c->n >= nrnn || c->n < 0) c->n = -1;
  return 0;
}

/* Rotate so that the detected up axis becomes +Y. Pure rotation (det = +1). */
static void remap_axis(const float* in, float* out, int axis) {
  if (axis == 3) { out[0] = in[0]; out[1] = in[2]; out[2] = -in[1]; }        /* Z-up  */
  else if (axis == 1) { out[0] = in[2]; out[1] = in[0]; out[2] = in[1]; }    /* X-up  */
  else { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; }                   /* Y-up  */
}

static int parse_into(const char* text, size_t len, const char* name, obj_mesh* out, char* err, size_t errcap) {
  float_arr pos = {0}, tex = {0}, nrm = {0};
  int_arr   tri = {0};                        /* 3 corner slots per triangle: p,t,n */
  char* buf = malloc(len + 1);
  if (!buf) { snprintf(err, errcap, "out of memory"); return -1; }

  memcpy(buf, text, len);
  buf[len] = '\0';

  int bad_faces = 0;
  char* p = buf;
  char* tok[64];

  while (*p) {
    char* eol = strchr(p, '\n');
    if (eol) *eol = '\0';
    int nt = tokenize_line(p, tok, 64);
    p = eol ? eol + 1 : p + strlen(p);
    if (nt == 0) continue;

    if (strcmp(tok[0], "v") == 0) {
      float xyz[4] = {0, 0, 0, 0};
      int n = parse_token_floats(tok, nt, xyz, 4);
      if (n < 3) continue;
      f_push3(&pos, xyz[0], xyz[1], xyz[2]);
    } else if (strcmp(tok[0], "vt") == 0) {
      float uv[3] = {0, 0, 0};
      int n = parse_token_floats(tok, nt, uv, 3);
      if (n < 2) continue;
      f_push(&tex, uv[0]); f_push(&tex, uv[1]);
    } else if (strcmp(tok[0], "vn") == 0) {
      float xyz[3] = {0, 0, 0};
      int n = parse_token_floats(tok, nt, xyz, 3);
      if (n < 3) continue;
      f_push3(&nrm, xyz[0], xyz[1], xyz[2]);
    } else if (strcmp(tok[0], "f") == 0) {
      /* corner references are whitespace-separated, so tokenize_line already split them */
      int cnt = nt - 1;
      if (cnt < 3) { bad_faces++; continue; }
      if (cnt > 64) cnt = 64;

      corner c[64];
      int ok = 1;
      for (int i = 0; i < cnt; i++) {
        if (parse_corner(tok[i + 1], &c[i], (int)pos.n, (int)tex.n, (int)nrm.n) < 0) { ok = 0; break; }
      }
      if (!ok) { bad_faces++; continue; }

      for (int i = 1; i + 1 < cnt; i++) {
        i_push(&tri, c[0].p); i_push(&tri, c[0].t); i_push(&tri, c[0].n);
        i_push(&tri, c[i].p); i_push(&tri, c[i].t); i_push(&tri, c[i].n);
        i_push(&tri, c[i + 1].p); i_push(&tri, c[i + 1].t); i_push(&tri, c[i + 1].n);
      }
    }
    /* g / o / usemtl / mtllib / s / l are ignored in this milestone */
  }

  free(buf);

  if (pos.n < 3) { snprintf(err, errcap, "no vertices found%s", bad_faces ? " (malformed faces present)" : ""); return -1; }
  if (tri.n == 0) { snprintf(err, errcap, "no faces found (%d malformed)", bad_faces); return -1; }

  /* --- detect the up axis from the bounding box --- */
  float mn[3] = { pos.d[0], pos.d[1], pos.d[2] };
  float mx[3] = { pos.d[0], pos.d[1], pos.d[2] };
  for (size_t i = 3; i < pos.n; i += 3) {
    for (int k = 0; k < 3; k++) {
      if (pos.d[i + k] < mn[k]) mn[k] = pos.d[i + k];
      if (pos.d[i + k] > mx[k]) mx[k] = pos.d[i + k];
    }
  }
  float ext[3] = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };

  /* Which axis points "up"? Two signals, in order of reliability:
     1. a model that rests on the ground plane has min[k] ~ 0 while max[k] > 0
        (classic Z-up exports: Blender/3ds Max/obj -> z from 0 upward);
     2. otherwise a model whose z span clearly exceeds its y span is Z-up.
     A model centred on the origin is left alone (OBJ's Y-up convention). */
  int up = 1;
  float asym[3];
  for (int k = 0; k < 3; k++) {
    float denom = ext[k] > 1e-6f ? ext[k] : 1.0f;
    asym[k] = fabsf(mn[k]) / denom;          /* 1.0 = centred, 0.0 = sitting on 0 */
  }
  int resting = -1;
  for (int k = 0; k < 3; k++) {
    if (asym[k] < 0.15f && ext[k] > 1e-6f) resting = k;
  }
  if (resting >= 0) {
    up = resting + 1;
  } else if (ext[2] > ext[1] && ext[2] > ext[0]) {
    up = 3;
  } else if (ext[0] > ext[1] && ext[0] > ext[2]) {
    up = 1;
  }

  float center[3] = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };
  float span = ext[0] > ext[1] ? ext[0] : ext[1];
  if (ext[2] > span) span = ext[2];
  float scale = span > 1e-6f ? 1.0f / span : 1.0f;

  float* tmp = malloc(3 * sizeof(float));
  if (!tmp) { snprintf(err, errcap, "out of memory"); return -1; }

  int has_nrm = nrm.n >= 3;
  int has_uv = tex.n >= 2;

  /* --- per-face normals for models without vn --- */
  float_arr face_n = {0};
  int tris = (int)(tri.n / 9);
  if (!has_nrm) {
    for (int t = 0; t < tris; t++) {
      const int* c = &tri.d[t * 9];
      float a[3] = { pos.d[c[0] * 3], pos.d[c[0] * 3 + 1], pos.d[c[0] * 3 + 2] };
      float b[3] = { pos.d[c[3] * 3], pos.d[c[3] * 3 + 1], pos.d[c[3] * 3 + 2] };
      float d[3] = { pos.d[c[6] * 3], pos.d[c[6] * 3 + 1], pos.d[c[6] * 3 + 2] };
      float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
      float e2[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
      float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
      f_push3(&face_n, n[0], n[1], n[2]);
    }
  }

  out->v = malloc((size_t)tri.n / 3 * 8 * sizeof(float));   /* tris*3 vertices, 8 floats each */
  if (!out->v) {
    free(tmp); free(face_n.d); free(pos.d); free(tex.d); free(nrm.d); free(tri.d);
    snprintf(err, errcap, "out of memory");
    return -1;
  }

  float mn2[3] = { 1e30f, 1e30f, 1e30f }, mx2[3] = { -1e30f, -1e30f, -1e30f };

  for (int t = 0; t < tris; t++) {
    const int* c = &tri.d[t * 9];
    for (int k = 0; k < 3; k++) {
      float* o = out->v + (t * 3 + k) * 8;
      int pi = c[k * 3];

      float raw[3] = { pos.d[pi * 3], pos.d[pi * 3 + 1], pos.d[pi * 3 + 2] };
      float shifted[3] = { raw[0] - center[0], raw[1] - center[1], raw[2] - center[2] };
      remap_axis(shifted, tmp, up);
      o[0] = tmp[0] * scale;
      o[1] = tmp[1] * scale;
      o[2] = tmp[2] * scale;

      if (has_nrm) {
        int ni = c[k * 3 + 2];
        if (ni < 0) ni = 0;
        float raw_n[3] = { nrm.d[ni * 3], nrm.d[ni * 3 + 1], nrm.d[ni * 3 + 2] };
        remap_axis(raw_n, tmp, up);
        float l = sqrtf(tmp[0] * tmp[0] + tmp[1] * tmp[1] + tmp[2] * tmp[2]);
        if (l > 1e-6f) { tmp[0] /= l; tmp[1] /= l; tmp[2] /= l; } else { tmp[0] = 0; tmp[1] = 1; tmp[2] = 0; }
        memcpy(o + 3, tmp, 3 * sizeof(float));
      } else {
        const float* fn = &face_n.d[t * 3];
        float l = sqrtf(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
        if (l > 1e-6f) { o[3] = fn[0] / l; o[4] = fn[1] / l; o[5] = fn[2] / l; }
        else { o[3] = 0.0f; o[4] = 1.0f; o[5] = 0.0f; }
      }

      int ti = c[k * 3 + 1];
      if (has_uv && ti >= 0) { o[6] = tex.d[ti * 2]; o[7] = tex.d[ti * 2 + 1]; }
      else { o[6] = 0.0f; o[7] = 0.0f; }

      for (int j = 0; j < 3; j++) {
        if (o[j] < mn2[j]) mn2[j] = o[j];
        if (o[j] > mx2[j]) mx2[j] = o[j];
      }
    }
  }

  float cx = (mn2[0] + mx2[0]) * 0.5f, cy = (mn2[1] + mx2[1]) * 0.5f, cz = (mn2[2] + mx2[2]) * 0.5f;
  float r2 = 0.0f;
  for (int i = 0; i < tris * 3; i++) {
    float* o = out->v + i * 8;
    o[0] -= cx; o[1] -= cy; o[2] -= cz;
    float d2 = o[0] * o[0] + o[1] * o[1] + o[2] * o[2];
    if (d2 > r2) r2 = d2;
  }
  out->radius = sqrtf(r2);
  if (out->radius < 1e-4f) out->radius = 1.0f;
  /* spans after remap, handy for framing and diagnostics - from the remapped
     box mn2/mx2, not from ext, which was measured on the original axes. mn2
     and mx2 are already scaled by `scale`, so no extra factor here. */
  for (int k = 0; k < 3; k++) out->span[k] = mx2[k] - mn2[k];

  free(tmp); free(face_n.d); free(pos.d); free(tex.d); free(nrm.d); free(tri.d);

  out->vcount = tris * 3;
  out->triangles = tris;
  out->flags = OBJ_HAS_NORMALS | (has_uv ? OBJ_HAS_TEXCOORDS : 0);
  dup_str(name, strlen(name), out->name, sizeof out->name);
  return 0;
}

int obj_parse_string(const char* text, size_t len, const char* name, obj_mesh* out, char* err, size_t errcap) {
  memset(out, 0, sizeof *out);
  if (!text || len == 0) { snprintf(err, errcap, "empty OBJ data"); return -1; }
  return parse_into(text, len, name ? name : "string", out, err, errcap);
}

int obj_parse_file(const char* path, obj_mesh* out, char* err, size_t errcap) {
  FILE* f = fopen(path, "rb");
  if (!f) { snprintf(err, errcap, "cannot open '%s'", path); return -1; }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) { fclose(f); snprintf(err, errcap, "'%s' is empty", path); return -1; }

  char* data = malloc((size_t)size);
  if (!data) { fclose(f); snprintf(err, errcap, "out of memory"); return -1; }
  size_t got = fread(data, 1, (size_t)size, f);
  fclose(f);

  int rc = obj_parse_string(data, got, path, out, err, errcap);
  free(data);
  return rc;
}

void obj_mesh_free(obj_mesh* m) {
  if (!m) return;
  free(m->v);
  memset(m, 0, sizeof *m);
}
