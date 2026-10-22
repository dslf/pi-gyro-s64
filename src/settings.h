#ifndef VIEWER_SETTINGS_H
#define VIEWER_SETTINGS_H

#include <stddef.h>

/* One descriptor table is the single source of truth for a setting: the ini key, the widget
   label, the group the widget lives in, the clamp range and the default. Widgets, ini text and
   the selftest all read the same entry, so adding a setting cannot leave a key behind. */
typedef enum { SET_FLOAT = 0, SET_BOOL = 1, SET_RGB = 2 } set_type;

typedef struct {
  const char* key;    /* ini key, lowercase ascii */
  const char* label;  /* widget label */
  const char* group;  /* widget section: camera | light | surface | scene | overlay */
  set_type    type;
  size_t      off;    /* byte offset of the field inside settings */
  float       min, max, dflt;   /* rgb components use min..max per component */
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
  float settings_w;
  int   hud_settings_visible;
  int   sky_visible;
  int   outline_visible;
  int   dirty;
} settings;

#define SETTINGS_FILE "settings.ini"   /* next to the exe, git-ignored (ADR §4.21) */

#define SETTINGS_OK 0        /* file read, values applied */
#define SETTINGS_MISSING 1   /* no file yet: defaults kept, not an error */
#define SETTINGS_ERROR -1    /* file exists but could not be read */

void  settings_default(settings* s);
/* Loads over the defaults. applied/clamped/skipped count the lines that were used, clamped to
   the range, and rejected (unknown key, bad value, no '=') - each rejection prints its key. */
int   settings_load(settings* s, const char* path, char* err, size_t errcap,
                    int* applied, int* clamped, int* skipped);
int   settings_save(const settings* s, const char* path, char* err, size_t errcap);

size_t         settings_field_count(void);
const set_field* settings_field(size_t i);
const set_field* settings_field_by_key(const char* key);

float settings_get(const settings* s, const set_field* f);           /* bool reads back as 0/1 */
void  settings_get_rgb(const settings* s, const set_field* f, float c[3]);
void  settings_set(settings* s, const set_field* f, double v);       /* clamped, marks dirty */
void  settings_set_rgb(settings* s, const set_field* f, const float c[3]); /* clamped per component */
int   settings_dirty(const settings* s);
void  settings_clear_dirty(settings* s);

/* One log line per live profile: what the user is looking at right now. */
void  settings_log(const settings* s, const char* origin);

#endif
