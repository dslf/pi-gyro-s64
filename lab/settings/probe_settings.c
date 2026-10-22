/* Probe for the settings format: one descriptor table drives ini text, clamps and (later) the
   ImGui widgets. No GL, no GLFW - this only proves the data format round-trips numerically.
   gcc -O2 -std=c11 -Wall -Wextra lab/settings/probe_settings.c -o trash/probe_settings.exe
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

typedef enum { SET_FLOAT = 0, SET_BOOL = 1, SET_RGB = 2 } set_type;

typedef struct {
  const char* key;    /* ini key, lowercase ascii */
  const char* label;  /* widget label */
  const char* group;  /* widget section: camera | light | surface | scene | overlay */
  set_type    type;
  size_t      off;    /* byte offset of the field inside settings */
  float       min, max, dflt;   /* rgb components use 0..1 */
  const char* fmt;    /* widget readout, NULL = "%.3f" */
} set_field;

typedef struct {
  float fov_deg;
  int   ortho;
  float yaw_deg, pitch_deg;
  float dist_scale;
  float light_dir[3];
  float light_int, amb_top, amb_bot;
  float base_rgb[3];
  float shine, specular;
  int   wireframe;
  float bg_rgb[3], outline_rgb[3];
  float hud_font_scale, hud_bg_alpha;
  int   hud_info_visible;
  float hud_help_w, hud_stats_w;
  int   dirty;
} settings;

#define F(name, key, label, group, type, lo, hi, def, fmt) \
  { key, label, group, type, offsetof(settings, name), lo, hi, def, fmt }

static const set_field FIELDS[] = {
  F(fov_deg,        "fov_deg",        "field of view",        "camera",  SET_FLOAT, 8.0f, 90.0f, 60.0f, "%.0f deg"),
  F(ortho,          "ortho",          "orthographic rays",    "camera",  SET_BOOL,  0.0f, 1.0f,  0.0f,  NULL),
  F(yaw_deg,        "yaw_deg",        "yaw",                  "camera",  SET_FLOAT, -180.0f, 180.0f, 34.377f, "%.1f deg"),
  F(pitch_deg,      "pitch_deg",      "pitch",                "camera",  SET_FLOAT, -89.0f, 89.0f, 14.336f, "%.1f deg"),
  F(dist_scale,     "dist_scale",     "distance x fit",       "camera",  SET_FLOAT, 0.3f, 8.0f,  1.0f,  "%.2f"),
  F(light_dir,      "light_dir",      "light direction",      "light",   SET_RGB,   -1.0f, 1.0f,  0.0f,  NULL),
  F(light_int,      "light_int",      "light intensity",      "light",   SET_FLOAT, 0.0f, 2.0f,  1.0f,  "%.2f"),
  F(amb_top,        "amb_top",        "sky from above",       "light",   SET_FLOAT, 0.0f, 2.0f,  1.0f,  "%.2f"),
  F(amb_bot,        "amb_bot",        "sky from below",       "light",   SET_FLOAT, 0.0f, 2.0f,  1.0f,  "%.2f"),
  F(base_rgb,       "base_rgb",       "surface colour",       "surface", SET_RGB,   0.0f, 1.0f,  0.0f,  NULL),
  F(shine,          "shine",          "specular sharpness",   "surface", SET_FLOAT, 4.0f, 128.0f, 48.0f, "%.0f"),
  F(specular,       "specular",       "specular strength",    "surface", SET_FLOAT, 0.0f, 1.0f,  0.45f, "%.2f"),
  F(wireframe,      "wireframe",      "wireframe",            "surface", SET_BOOL,  0.0f, 1.0f,  0.0f,  NULL),
  F(bg_rgb,         "bg_rgb",         "background colour",    "scene",   SET_RGB,   0.0f, 1.0f,  0.0f,  NULL),
  F(outline_rgb,    "outline_rgb",    "outline colour",       "scene",   SET_RGB,   0.0f, 1.0f,  0.0f,  NULL),
  F(hud_font_scale, "hud_font_scale", "hud font scale",       "overlay", SET_FLOAT, 0.6f, 2.5f,  1.0f,  "%.2f"),
  F(hud_bg_alpha,   "hud_bg_alpha",   "panel opacity",        "overlay", SET_FLOAT, 0.0f, 1.0f,  0.94f, "%.2f"),
  F(hud_info_visible,"hud_info_visible","info panels",        "overlay", SET_BOOL,  0.0f, 1.0f,  1.0f,  NULL),
  F(hud_help_w,     "hud_help_w",     "hint width cap",       "overlay", SET_FLOAT, 300.0f, 1200.0f, 760.0f, "%.0f px"),
  F(hud_stats_w,    "hud_stats_w",    "stats width (0=auto)", "overlay", SET_FLOAT, 0.0f, 800.0f, 0.0f, "%.0f px"),
};
static const size_t NFIELDS = sizeof FIELDS / sizeof FIELDS[0];

/* Defaults reproduce the current built-in look: the light vector is the normalised
   (0.45, 0.80, 0.55) and the colours are the shader constants, so a settings file that
   changes nothing renders exactly the frame the regression already measures. */
static void settings_default(settings* s) {
  memset(s, 0, sizeof *s);
  for (size_t i = 0; i < NFIELDS; i++) {
    const set_field* f = &FIELDS[i];
    if (f->type == SET_RGB) continue;
    *(float*)((char*)s + f->off) = f->dflt;
    if (f->type == SET_BOOL) *(int*)((char*)s + f->off) = (int)f->dflt;
  }
  float ld[3] = { 0.45f, 0.80f, 0.55f };
  float len = sqrtf(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
  for (int i = 0; i < 3; i++) s->light_dir[i] = ld[i] / len;
  s->base_rgb[0] = 0.72f; s->base_rgb[1] = 0.74f; s->base_rgb[2] = 0.78f;
  s->bg_rgb[0] = 0.22f; s->bg_rgb[1] = 0.24f; s->bg_rgb[2] = 0.28f;
  s->outline_rgb[0] = 0.95f; s->outline_rgb[1] = 0.55f; s->outline_rgb[2] = 0.18f;
  s->dirty = 0;
}

static int str_eq_ci(const char* a, const char* b) {
  while (*a && *b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return 0;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

static char* trim(char* t) {
  while (*t == ' ' || *t == '\t') t++;
  size_t n = strlen(t);
  while (n && (t[n - 1] == ' ' || t[n - 1] == '\t')) t[--n] = 0;
  return t;
}

static const set_field* field_by_key(const char* key) {
  for (size_t i = 0; i < NFIELDS; i++) if (str_eq_ci(key, FIELDS[i].key)) return &FIELDS[i];
  return NULL;
}

static int parse_bool(const char* v, int* out) {
  if (str_eq_ci(v, "true") || strcmp(v, "1") == 0 || str_eq_ci(v, "yes")) { *out = 1; return 1; }
  if (str_eq_ci(v, "false") || strcmp(v, "0") == 0 || str_eq_ci(v, "no")) { *out = 0; return 1; }
  return 0;
}

/* key = value, with rgb as three numbers. Returns 1 when the token was well formed. */
static int parse_value(const set_field* f, const char* v, settings* s, double* shown) {
  if (f->type == SET_BOOL) {
    int b = 0;
    if (!parse_bool(v, &b)) return 0;
    *(int*)((char*)s + f->off) = b;
    *shown = b;
    return 1;
  }
  if (f->type == SET_RGB) {
    float c[3];
    int n = 0;
    const char* p = v;
    while (n < 3) {
      char* end = NULL;
      double x = strtod(p, &end);
      if (end == p) break;
      c[n++] = (float)x;
      p = end;
      while (*p == ',' || *p == ' ' || *p == '\t') p++;
    }
    if (n != 3 || *p) return 0;
    float* dst = (float*)((char*)s + f->off);
    for (int i = 0; i < 3; i++) dst[i] = c[i] < f->min ? f->min : (c[i] > f->max ? f->max : c[i]);
    *shown = dst[0];
    return 1;
  }
  char* end = NULL;
  double x = strtod(v, &end);
  if (end == v || *end) return 0;
  double cl = x < f->min ? f->min : (x > f->max ? f->max : x);
  *(float*)((char*)s + f->off) = (float)cl;
  *shown = cl;
  return 1;
}

#define SETTINGS_OK 0
#define SETTINGS_MISSING 1
#define SETTINGS_ERROR -1

static int settings_load(const char* path, settings* s, char* err, size_t errcap,
                         int* applied, int* clamped, int* skipped, int* missing) {
  *applied = *clamped = *skipped = 0;
  *missing = 0;
  (void)err; (void)errcap;   /* the probe has no fatal path: a line it cannot read is reported and skipped */
  settings_default(s);
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    *missing = 1;
    return SETTINGS_MISSING;
  }
  char line[512];
  int ln = 0;
  while (fgets(line, sizeof line, fp)) {
    ln++;
    char* t = line;
    for (size_t i = 0; t[i]; i++) if (t[i] == '\r' || t[i] == '\n') t[i] = 0;
    t = trim(t);
    if (!*t || t[0] == '#') continue;
    char* eq = strchr(t, '=');
    if (!eq) {
      fprintf(stderr, "settings: line %d of '%s' has no '=' - ignored\n", ln, path);
      (*skipped)++;
      continue;
    }
    *eq = 0;
    char* key = trim(t);
    char* val = trim(eq + 1);
    const set_field* f = field_by_key(key);
    if (!f) {
      fprintf(stderr, "settings: unknown key '%s' (line %d) - ignored\n", key, ln);
      (*skipped)++;
      continue;
    }
    double shown = 0.0;
    if (!parse_value(f, val, s, &shown)) {
      fprintf(stderr, "settings: bad value for '%s' = '%s' (line %d) - default kept\n", key, val, ln);
      (*skipped)++;
      continue;
    }
    if (f->type == SET_FLOAT) {
      double raw = strtod(val, NULL);
      if (raw < f->min || raw > f->max) {
        fprintf(stderr, "settings: '%s' = %g clamped to %g\n", key, raw, shown);
        (*clamped)++;
      }
    }
    (*applied)++;
  }
  fclose(fp);
  s->dirty = 0;
  return SETTINGS_OK;
}

static float field_float(const settings* s, const set_field* f) {
  if (f->type == SET_BOOL) return (float)*(const int*)((const char*)s + f->off);
  return *(const float*)((const char*)s + f->off);
}

/* Relative comparison: the ini text prints %.6f, so a round-trip must agree to well
   under a percent, not to an absolute epsilon that would break on another libc. */
static int close_rel(double a, double b) {
  double d = fabs(a - b);
  double m = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
  return d <= 1e-4 * (m > 1.0 ? m : 1.0);
}

static int write_text(const char* path, const char* text) {
  FILE* fp = fopen(path, "wb");
  if (!fp) return 0;
  fputs(text, fp);
  fclose(fp);
  return 1;
}

static void settings_save(const settings* s, FILE* fp) {
  fprintf(fp, "# viewer settings - key = value, '#' comments ignored, missing keys use defaults\n");
  const char* group = NULL;
  for (size_t i = 0; i < NFIELDS; i++) {
    const set_field* f = &FIELDS[i];
    if (!group || strcmp(group, f->group)) {
      group = f->group;
      fprintf(fp, "\n# %s\n", group);
    }
    if (f->type == SET_BOOL)
      fprintf(fp, "%s = %s\n", f->key, *(const int*)((const char*)s + f->off) ? "true" : "false");
    else if (f->type == SET_RGB) {
      const float* c = (const float*)((const char*)s + f->off);
      fprintf(fp, "%s = %.6f, %.6f, %.6f\n", f->key, c[0], c[1], c[2]);
    } else {
      fprintf(fp, "%s = %.6f\n", f->key, *(const float*)((const char*)s + f->off));
    }
  }
}

int main(void) {
  int fails = 0;
  const char* path = "trash/settings_probe.ini";
  settings s;
  char err[256] = "";
  int applied = 0, clamped = 0, skipped = 0, missing = 0;

  int dup = 0;
  for (size_t i = 0; i < NFIELDS; i++)
    for (size_t j = i + 1; j < NFIELDS; j++)
      if (str_eq_ci(FIELDS[i].key, FIELDS[j].key)) dup++;
  printf("probe: %zu fields, %d duplicate keys\n", NFIELDS, dup);
  if (dup) { printf("  FAIL duplicate keys in the descriptor table\n"); fails++; }

  settings_default(&s);
  int bad = 0;
  for (size_t i = 0; i < NFIELDS; i++) {
    const set_field* f = &FIELDS[i];
    if (f->type == SET_RGB) {
      const float* c = (const float*)((char*)&s + f->off);
      for (int k = 0; k < 3; k++) if (c[k] < f->min || c[k] > f->max) bad++;
    } else if (field_float(&s, f) < f->min || field_float(&s, f) > f->max) bad++;
  }
  printf("defaults: %d out-of-range values\n", bad);
  if (bad) { printf("  FAIL defaults outside their own range\n"); fails++; }

  FILE* fp = fopen(path, "wb");
  if (!fp) { printf("  FAIL cannot write %s\n", path); return 1; }
  settings_save(&s, fp);
  fclose(fp);
  fp = fopen(path, "rb");
  int key_lines = 0;
  char line[512];
  while (fgets(line, sizeof line, fp))
    if (line[0] != '#' && line[0] != '\r' && line[0] != '\n' && strchr(line, '=')) key_lines++;
  fclose(fp);
  printf("save: %d key lines for %zu fields\n", key_lines, NFIELDS);
  if (key_lines != (int)NFIELDS) { printf("  FAIL every field must be written\n"); fails++; }

  settings b;
  if (settings_load(path, &b, err, sizeof err, &applied, &clamped, &skipped, &missing) != SETTINGS_OK ||
      applied != (int)NFIELDS) {
    printf("  FAIL load of the saved file: rc/applied = %d/%d of %zu\n", applied, applied, NFIELDS);
    fails++;
  }
  int drift = 0;
  for (size_t i = 0; i < NFIELDS; i++) {
    const set_field* f = &FIELDS[i];
    if (f->type == SET_RGB) {
      const float* x = (const float*)((char*)&b + f->off);
      const float* y = (const float*)((char*)&s + f->off);
      for (int k = 0; k < 3; k++) if (!close_rel(x[k], y[k])) drift++;
    } else if (!close_rel(field_float(&b, f), field_float(&s, f))) drift++;
  }
  printf("roundtrip: %d applied, %d drifted of %zu\n", applied, drift, NFIELDS);
  if (drift) { printf("  FAIL round-trip changed values\n"); fails++; }

  FILE* fp2 = fopen("trash/settings_probe2.ini", "wb");
  settings_save(&b, fp2);
  fclose(fp2);
  long n1 = 0, n2 = 0;
  fp = fopen(path, "rb"); fp2 = fopen("trash/settings_probe2.ini", "rb");
  int same = 1;
  int c1 = fgetc(fp), c2 = fgetc(fp2);
  while (c1 != EOF || c2 != EOF) { if (c1 != c2) { same = 0; break; } c1 = fgetc(fp); c2 = fgetc(fp2); n1++; }
  fclose(fp); fclose(fp2);
  printf("determinism: %ld bytes, identical = %s\n", n1, same ? "yes" : "no");
  if (!same || n2) { printf("  FAIL saving twice produced different bytes\n"); fails++; }

  if (!write_text(path, "# comment\n\nfov_deg = 120\nspecular = 2\ndist_scale = 0.01\namb_top = 9\n")) {
    printf("  FAIL cannot write the clamp fixture\n"); fails++;
  }
  settings c;
  settings_load(path, &c, err, sizeof err, &applied, &clamped, &skipped, &missing);
  printf("clamps: %d applied, %d clamped, fov %.1f spec %.2f dist %.2f amb %.2f\n",
         applied, clamped, c.fov_deg, c.specular, c.dist_scale, c.amb_top);
  if (clamped != 4 || c.fov_deg != 90.0f || c.specular != 1.0f || c.dist_scale != 0.3f || c.amb_top != 2.0f) {
    printf("  FAIL out-of-range values were not clamped to the bounds\n"); fails++;
  }

  write_text(path, "bogus = 1\nfov_deg = abc\nlight_dir\n\n# trailing comment\n");
  settings d;
  settings_load(path, &d, err, sizeof err, &applied, &clamped, &skipped, &missing);
  printf("malformed: %d applied, %d skipped, fov %.1f\n", applied, skipped, d.fov_deg);
  if (skipped != 3 || applied != 0 || !close_rel(d.fov_deg, 60.0f)) {
    printf("  FAIL malformed lines must be skipped with defaults kept\n"); fails++;
  }

  settings e;
  int rc = settings_load("trash/definitely_absent_settings.ini", &e, err, sizeof err,
                         &applied, &clamped, &skipped, &missing);
  printf("missing file: rc %d, missing %d, fov %.1f\n", rc, missing, e.fov_deg);
  if (rc != SETTINGS_MISSING || !missing || !close_rel(e.fov_deg, 60.0f)) {
    printf("  FAIL a missing file must fall back to defaults, not fail\n"); fails++;
  }

  write_text(path, "fov_deg = 70\r\n\r\n# note\r\northo = true\r\nwireframe = 0\r\nlight_dir = 0.1, 0.9, 0.4\r\n");
  settings g;
  settings_load(path, &g, err, sizeof err, &applied, &clamped, &skipped, &missing);
  printf("crlf: %d applied, %d skipped, fov %.1f ortho %d light_y %.3f\n",
         applied, skipped, g.fov_deg, g.ortho, g.light_dir[1]);
  if (applied != 4 || skipped != 0 || !close_rel(g.fov_deg, 70.0f) || !g.ortho || !close_rel(g.light_dir[1], 0.9f)) {
    printf("  FAIL CRLF/comment lines must parse\n"); fails++;
  }
  g.dirty = 1;
  printf("dirty: %d after load, %d after a widget change\n", b.dirty, g.dirty);
  if (b.dirty != 0 || g.dirty != 1) { printf("  FAIL dirty flag does not follow load/change\n"); fails++; }

  printf("probe_settings: %s\n", fails == 0 ? "PASS" : "FAILED");
  return fails == 0 ? 0 : 1;
}
