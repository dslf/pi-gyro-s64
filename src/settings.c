#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

/* Defaults reproduce the frame the regression already measures: the light vector is the
   normalised (0.45, 0.80, 0.55), colours are the shader constants, and yaw/pitch are the
   camera reset pose (0.6 rad, 0.25 rad) expressed in degrees. A settings file that changes
   nothing therefore renders exactly the pixels tests/run_tests.ps1 compares against. */
#define F(name, key, label, group, type, lo, hi, def, fmt) \
  { key, label, group, type, offsetof(settings, name), lo, hi, def, fmt }

static const set_field FIELDS[] = {
  F(fov_deg,         "fov_deg",         "field of view",     "camera",  SET_FLOAT, 8.0f, 90.0f, 60.0f,     "%.0f deg"),
  F(ortho,           "ortho",           "orthographic rays", "camera",  SET_BOOL,  0.0f, 1.0f,  0.0f,      NULL),
  F(yaw_deg,         "yaw_deg",         "yaw",               "camera",  SET_FLOAT, -180.0f, 180.0f, 34.37747f, "%.1f deg"),
  F(pitch_deg,       "pitch_deg",       "pitch",             "camera",  SET_FLOAT, -89.0f, 89.0f, 14.32394f, "%.1f deg"),
  F(dist_scale,      "dist_scale",      "distance x fit",    "camera",  SET_FLOAT, 0.3f, 8.0f,  1.0f,      "%.2f"),
  F(light_dir,       "light_dir",       "light direction",   "light",   SET_RGB,   -1.0f, 1.0f,  0.0f,      NULL),
  F(light_int,       "light_int",       "light intensity",   "light",   SET_FLOAT, 0.0f, 2.0f,  1.0f,      "%.2f"),
  F(amb_top,         "amb_top",         "sky from above",    "light",   SET_FLOAT, 0.0f, 2.0f,  1.0f,      "%.2f"),
  F(amb_bot,         "amb_bot",         "sky from below",    "light",   SET_FLOAT, 0.0f, 2.0f,  1.0f,      "%.2f"),
  F(base_rgb,        "base_rgb",        "surface colour",    "surface", SET_RGB,   0.0f, 1.0f,  0.0f,      NULL),
  F(shine,           "shine",           "specular sharpness","surface", SET_FLOAT, 4.0f, 128.0f, 48.0f,    "%.0f"),
  F(specular,        "specular",        "specular strength", "surface", SET_FLOAT, 0.0f, 1.0f,  0.45f,     "%.2f"),
  F(wireframe,       "wireframe",       "wireframe",         "surface", SET_BOOL,  0.0f, 1.0f,  0.0f,      NULL),
  F(bg_rgb,          "bg_rgb",          "background colour", "scene",   SET_RGB,   0.0f, 1.0f,  0.0f,      NULL),
  F(outline_rgb,     "outline_rgb",     "outline colour",    "scene",   SET_RGB,   0.0f, 1.0f,  0.0f,      NULL),
  F(hud_font_px,     "hud_font_px",     "hud font size",     "overlay", SET_FLOAT, 6.0f, 32.0f, 13.26f,    "%.2f px"),
  F(hud_bg_alpha,    "hud_bg_alpha",    "panel opacity",     "overlay", SET_FLOAT, 0.0f, 1.0f,  0.94f,     "%.2f"),
  F(hud_info_visible,"hud_info_visible","info panels",       "overlay", SET_BOOL,  0.0f, 1.0f,  1.0f,      NULL),
  F(hud_help_w,      "hud_help_w",      "hint width cap",    "overlay", SET_FLOAT, 300.0f, 1200.0f, 760.0f, "%.0f px"),
  F(hud_stats_w,     "hud_stats_w",     "stats width (0=auto)", "overlay", SET_FLOAT, 0.0f, 800.0f, 0.0f,  "%.0f px"),
  /* Panel geometry and visibility are settings like everything else: a width pinned by a drag or
     a panel hidden by RMB survives the next start instead of living only in the HUD. sky_visible
     switches the two sky terms off (ambience only), outline_visible switches the picked outline off. */
  F(settings_w,          "settings_w",          "panel width",        "overlay", SET_FLOAT, 120.0f, 900.0f, 460.0f, "%.0f px"),
  F(hud_settings_visible,"hud_settings_visible","widget panel shown", "overlay", SET_BOOL,  0.0f, 1.0f,   1.0f,     NULL),
  F(sky_visible,         "sky_visible",         "sky light",          "light",   SET_BOOL,  0.0f, 1.0f,   1.0f,     NULL),
  F(outline_visible,     "outline_visible",     "picked outline",     "overlay", SET_BOOL,  0.0f, 1.0f,   1.0f,     NULL),
};

#define NFIELDS (sizeof FIELDS / sizeof FIELDS[0])

static const float LIGHT_DIR_DEFAULT[3] = { 0.45f, 0.80f, 0.55f };
static const float BASE_COLOR_DEFAULT[3] = { 0.72f, 0.74f, 0.78f };
static const float BG_COLOR_DEFAULT[3]   = { 0.22f, 0.24f, 0.28f };   /* linear */
static const float HL_COLOR_DEFAULT[3]   = { 0.95f, 0.55f, 0.18f };   /* linear */

void settings_default(settings* s) {
  memset(s, 0, sizeof *s);
  for (size_t i = 0; i < NFIELDS; i++) {
    const set_field* f = &FIELDS[i];
    if (f->type == SET_RGB) continue;
    if (f->type == SET_BOOL) *(int*)((char*)s + f->off) = (int)f->dflt;
    else *(float*)((char*)s + f->off) = f->dflt;
  }
  float len = sqrtf(LIGHT_DIR_DEFAULT[0] * LIGHT_DIR_DEFAULT[0] +
                    LIGHT_DIR_DEFAULT[1] * LIGHT_DIR_DEFAULT[1] +
                    LIGHT_DIR_DEFAULT[2] * LIGHT_DIR_DEFAULT[2]);
  for (int i = 0; i < 3; i++) s->light_dir[i] = len > 1e-6f ? LIGHT_DIR_DEFAULT[i] / len : 0.0f;
  for (int i = 0; i < 3; i++) {
    s->base_rgb[i] = BASE_COLOR_DEFAULT[i];
    s->bg_rgb[i] = BG_COLOR_DEFAULT[i];
    s->outline_rgb[i] = HL_COLOR_DEFAULT[i];
  }
  s->dirty = 0;
}

size_t settings_field_count(void) { return NFIELDS; }

const set_field* settings_field(size_t i) { return i < NFIELDS ? &FIELDS[i] : NULL; }

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

const set_field* settings_field_by_key(const char* key) {
  for (size_t i = 0; i < NFIELDS; i++) if (str_eq_ci(key, FIELDS[i].key)) return &FIELDS[i];
  return NULL;
}

/* Keys an older version wrote and the current table no longer has: read-only aliases. The value is
   multiplied before it lands in the current field, so the old `hud_font_scale = 0.78` still means
   13.26 px. Saving never writes an alias - only the current key comes back out. */
typedef struct {
  const char* key;    /* legacy ini key */
  const char* into;   /* current key the value is stored in */
  float       mult;   /* value * mult, in the current field's unit */
} set_alias;

static const set_alias ALIASES[] = {
  { "hud_font_scale", "hud_font_px", SETTINGS_FONT_BASE },
};

#define NALIASES (sizeof ALIASES / sizeof ALIASES[0])

static const set_alias* alias_by_key(const char* key) {
  for (size_t i = 0; i < NALIASES; i++) if (str_eq_ci(key, ALIASES[i].key)) return &ALIASES[i];
  return NULL;
}

static char* trim(char* t) {
  while (*t == ' ' || *t == '\t') t++;
  size_t n = strlen(t);
  while (n && (t[n - 1] == ' ' || t[n - 1] == '\t')) t[--n] = 0;
  return t;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float settings_get(const settings* s, const set_field* f) {
  if (f->type == SET_BOOL) return (float)*(const int*)((const char*)s + f->off);
  return *(const float*)((const char*)s + f->off);
}

void settings_get_rgb(const settings* s, const set_field* f, float c[3]) {
  const float* src = (const float*)((const char*)s + f->off);
  for (int i = 0; i < 3; i++) c[i] = src[i];
}

void settings_set(settings* s, const set_field* f, double v) {
  if (f->type == SET_BOOL) {
    *(int*)((char*)s + f->off) = v >= 0.5 ? 1 : 0;
  } else {
    *(float*)((char*)s + f->off) = clampf((float)v, f->min, f->max);
  }
  s->dirty = 1;
}

void settings_set_rgb(settings* s, const set_field* f, const float c[3]) {
  float* dst = (float*)((char*)s + f->off);
  for (int i = 0; i < 3; i++) dst[i] = clampf(c[i], f->min, f->max);
  s->dirty = 1;
}

int settings_dirty(const settings* s) { return s->dirty; }

void settings_clear_dirty(settings* s) { s->dirty = 0; }

static int parse_bool(const char* v, int* out) {
  if (str_eq_ci(v, "true") || strcmp(v, "1") == 0 || str_eq_ci(v, "yes")) { *out = 1; return 1; }
  if (str_eq_ci(v, "false") || strcmp(v, "0") == 0 || str_eq_ci(v, "no")) { *out = 0; return 1; }
  return 0;
}

/* Parses `key = value` into the field. RGB takes three numbers, bool takes true/false/1/0, a
   float is clamped to the field range. Returns 0 when the text is not a value for that field. */
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
    for (int i = 0; i < 3; i++) dst[i] = clampf(c[i], f->min, f->max);
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

int settings_load(settings* s, const char* path, char* err, size_t errcap,
                  int* applied, int* clamped, int* skipped) {
  *applied = *clamped = *skipped = 0;
  settings_default(s);
  FILE* fp = fopen(path, "r");
  if (!fp) {
    if (errno == ENOENT) return SETTINGS_MISSING;
    snprintf(err, errcap, "cannot read '%s': %s", path, strerror(errno));
    return SETTINGS_ERROR;
  }
  char line[512];
  int ln = 0;
  while (fgets(line, sizeof line, fp)) {
    ln++;
    for (size_t i = 0; line[i]; i++) if (line[i] == '\r' || line[i] == '\n') line[i] = 0;
    char* t = trim(line);
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
    const set_field* f = settings_field_by_key(key);
    const set_alias* al = NULL;
    if (!f) {
      al = alias_by_key(key);
      if (!al) {
        fprintf(stderr, "settings: unknown key '%s' (line %d of '%s') - ignored\n", key, ln, path);
        (*skipped)++;
        continue;
      }
      f = settings_field_by_key(al->into);
    }
    /* A legacy value is converted into the current unit before it is parsed, so the clamp range
       and the readout are the current field's numbers, not the old multiplier's. */
    double legacy_raw = 0.0;
    char conv[64];
    const char* valc = val;
    if (al) {
      char* end = NULL;
      legacy_raw = strtod(val, &end);
      if (end == val || *end) {
        fprintf(stderr, "settings: legacy key '%s' = '%s' (line %d of '%s') is not a number - default kept\n",
                key, val, ln, path);
        (*skipped)++;
        continue;
      }
      snprintf(conv, sizeof conv, "%.6f", legacy_raw * (double)al->mult);
      valc = conv;
    }
    double shown = 0.0;
    if (!parse_value(f, valc, s, &shown)) {
      fprintf(stderr, "settings: bad value for '%s' = '%s' (line %d of '%s') - default kept\n",
              key, val, ln, path);
      (*skipped)++;
      continue;
    }
    if (f->type == SET_FLOAT) {
      double raw = strtod(valc, NULL);
      if (raw < f->min || raw > f->max) {
        if (al)
          fprintf(stderr, "settings: legacy '%s' = %g -> %g is outside [%g, %g] - clamped to %g\n",
                  key, legacy_raw, raw, (double)f->min, (double)f->max, shown);
        else
          fprintf(stderr, "settings: '%s' = %g is outside [%g, %g] - clamped to %g\n",
                  key, raw, (double)f->min, (double)f->max, shown);
        (*clamped)++;
      }
    }
    if (al) {
      fprintf(stderr, "settings: legacy key '%s' = %g read as '%s' = %.6g (line %d of '%s')\n",
              key, legacy_raw, al->into, shown, ln, path);
      s->legacy_keys++;
    }
    (*applied)++;
  }
  if (ferror(fp)) {
    snprintf(err, errcap, "read error in '%s' (line %d): %s", path, ln, strerror(errno));
    fclose(fp);
    settings_default(s);
    return SETTINGS_ERROR;
  }
  fclose(fp);
  settings_clear_dirty(s);
  return SETTINGS_OK;
}

int settings_save(const settings* s, const char* path, char* err, size_t errcap) {
  FILE* fp = fopen(path, "w");
  if (!fp) {
    snprintf(err, errcap, "cannot write '%s': %s", path, strerror(errno));
    return SETTINGS_ERROR;
  }
  fprintf(fp, "# viewer settings - key = value, '#' comments ignored, missing keys use defaults\n");
  fprintf(fp, "# ranges are enforced on load; delete this file to go back to built-in defaults\n");
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
  if (fflush(fp) != 0 || ferror(fp)) {
    snprintf(err, errcap, "write error to '%s': %s", path, strerror(errno));
    fclose(fp);
    return SETTINGS_ERROR;
  }
  if (fclose(fp) != 0) {
    snprintf(err, errcap, "close error on '%s': %s", path, strerror(errno));
    return SETTINGS_ERROR;
  }
  return 0;
}

void settings_log(const settings* s, const char* origin) {
  printf("settings [%s]: fov %.0f deg %s, yaw %.1f pitch %.1f dist %.2fx fit, "
         "light %.2f x, sky %.2f/%.2f, shine %.0f specular %.2f, %s, "
         "font %.2f px, panel alpha %.2f, info %s\n",
         origin, s->fov_deg, s->ortho ? "orthographic" : "perspective",
         s->yaw_deg, s->pitch_deg, s->dist_scale, s->light_int, s->amb_top, s->amb_bot,
         s->shine, s->specular, s->wireframe ? "wireframe" : "shaded",
         s->hud_font_px, s->hud_bg_alpha, s->hud_info_visible ? "shown" : "hidden");
}
