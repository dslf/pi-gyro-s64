#ifndef VIEWER_HUD_H
#define VIEWER_HUD_H

#include <stddef.h>
#include "settings.h"

/* Text overlay built by Dear ImGui (vendored cimgui, C ABI). The viewer stays C11:
   every C++ symbol is hidden behind this header. hud.c owns the ImGui context and the
   GLFW/OpenGL3 backends; the caller supplies only data. */

#define HUD_PANEL_STATS 0   /* camera / triangles / selection - top-left */
#define HUD_PANEL_HELP  1   /* hotkey hints - bottom-centre */
#define HUD_PANEL_SETTINGS 2 /* widgets driving settings - under the stats card, top-left */

typedef struct {
  const char* projection;  /* "persp" | "ortho" */
  float fov_deg;
  float cam_dist;
  int triangles;           /* triangles in the loaded mesh */
  int selected;            /* highlighted triangle index, -1 = nothing selected */
  int pinned;              /* 1 = selection pinned by a click, 0 = hover */
  int wireframe;
  double fps;              /* measured frames/second; 0 = not measured yet */
  float pin_nx, pin_ny, pin_nz;   /* normal of the selected face */
  float pin_cx, pin_cy, pin_cz;   /* centroid of the selected face, model space */
  int   face_id;                  /* 0-based ordinal of the OBJ face, -1 = unknown */
  int   faces;                    /* faces parsed from the file */
  int   vert_id[3];               /* 1-based vertex numbers of the selected triangle */
  int   normals_authored;         /* 1 = vn came from the file, 0 = computed by the parser */
  float vn_dev_deg;               /* max angle vertex normal vs face normal, degrees */
} hud_info;

typedef struct {
  void* ctx;               /* ImGuiContext* (opaque here) */
  int backends;            /* 1 = GLFW + OpenGL3 backends initialised */
  int frames;              /* frames submitted to ImGui */
  int text_ready;          /* font atlas built: text is drawn from the 2nd frame on */
  float margin;            /* gap from the frame edge, pixels, for both panels */
  float stats_w;           /* >0 pins the stats width (HUD_W); 0 = the panel hugs its text */
  float help_w;            /* help panel width cap, measured from the hint text; height follows the wrap */
  int stats_rect[4];       /* x0,y0,x1,y1 as ImGui drew it, pixel-y from the top */
  int help_rect[4];        /* same, for the help panel */
  /* Settings panel: widgets that drive settings. Own rect and lifetime flags, same
     convention as the info panels - geometry is resolved before the widgets are drawn,
     so mouse ownership never touches widget state. */
  int   settings_rect[4];  /* same layout as stats_rect/help_rect */
  int   settings_visible;
  int   settings_placed;   /* 0 = the panel is still where hud_frame put it */
  float settings_x, settings_y;
  float settings_w;        /* pinned width in px; 0 = auto-fit to the widgets */
  float settings_room;     /* space left under the statistics card, 0 = no panel this frame */
  int   settings_collapsed;/* 1 = the last frame drew the title-bar tab instead of the widgets */
  int   hint_lines;        /* hint lines that fit this frame (a small frame drops the tail) */
  int   settings_settled;  /* a widget was released after an edit -> worth persisting */
  /* Set on the frame a title-bar drag moved the window: the log line is printed once the
     button is released, not on every frame of the drag. */
  int   settings_moved;
  /* 1 = ImGui is holding a drag (slider, scrollbar or title bar) that started inside the HUD.
     It stays set while the cursor wanders outside the panel - which is the whole point: the
     end of a long slider drag happens over the 3D field. */
  int   mouse_busy;
  float settings_scroll;   /* scroll_y of the settings window, 0 = widget list at the top */
  int   settings_pos_forced; /* next frame applies settings_x/y with ImGuiCond_Always */
  /* Mouse state injected into ImGui instead of the GLFW backend (selftest only). */
  int   inj_active;
  float inj_x, inj_y;
  int   inj_down;          /* left button held */
  float inj_wheel;         /* injected wheel, >0 scrolls the content up */
  float font_scale;        /* baked glyph size multiplier */
  int   info_visible;      /* 0 hides both information panels ([H]) */
} hud;

/* Initialise ImGui + backends on an existing GLFW window. Returns 0 on success,
   non-zero with a message in err. */
int  hud_init(hud* h, void* window, char* err, size_t errcap);
void hud_shutdown(hud* h);

/* Build both panels for this frame (ImGui NewFrame + widgets). Call before hud_render.
   w/h are the framebuffer size in pixels; both panels are auto-sized to their text and
   re-positioned on every frame (stats top-left, help bottom-centre by pivot). */
void hud_frame(hud* h, int w, int h_px, const hud_info* info, settings* s);

/* Submit the drawn panels to OpenGL. Does not swap buffers. */
void hud_render(hud* h);

/* Rect of one panel as ImGui drew it (HUD_PANEL_STATS / HUD_PANEL_HELP), in pixels with
   pixel-y from the top. Returns 0 if that panel was not drawn yet. */
int  hud_rect(const hud* h, int panel, int* x0, int* y0, int* x1, int* y1);

/* Visibility: information panels ([H]) and the settings panel (RMB click). */
void hud_set_info_visible(hud* h, int visible);
int  hud_info_visible(const hud* h);
void hud_settings_show(hud* h, int visible);
int  hud_settings_visible(const hud* h);
/* How many hint lines the last frame showed: 2 = all of them, fewer on a small frame. */
int  hud_hint_lines(const hud* h);
/* Space left under the statistics card on the last frame; 0 means no room for the panel. */
float hud_settings_room(const hud* h);
/* Scroll offset of the settings window on the last frame (0 = list at the top). Non-zero also
   means the widget list is taller than the room and the panel is scrolled down. */
float hud_settings_scroll(const hud* h);
/* 1 while ImGui holds a drag that started in a HUD widget (slider, scrollbar, title bar). The
   cursor can be anywhere - including over the 3D field - so main.c keeps the camera out of a
   gesture the HUD is still owning. */
int  hud_mouse_busy(const hud* h);
/* Feeds mouse state to ImGui on the next hud_frame, bypassing the GLFW backend: no real cursor
   exists over an offscreen framebuffer, and the drag-ownership / wheel-scroll regressions need
   a cursor that leaves the window while the button is still held. wheel >0 scrolls up and is one
   frame of input; a negative x or y stops injecting and hands the mouse back to the backend. */
void hud_warp_mouse(hud* h, double x, double y, int left_down, float wheel);
/* Collapse or expand the settings window exactly the way the title-bar triangle does: the state
   lives in ImGui and applies on the next frame. hud_settings_collapsed() reports what the last
   frame actually drew. The selftest drives this path because a collapse makes igBegin return
   false - the crash regression is an abort there, not a wrong pixel. */
void hud_settings_collapse(hud* h, int collapsed);
int hud_settings_collapsed(const hud* h);
/* Move the settings panel (window pixels, pixel-y from the top); w <= 0 keeps the current
   width. The drag gesture in main.c owns the position. */
void hud_settings_place(hud* h, float x, float y, float w);
/* Pin the settings panel width now (a width restored from settings.ini). */
void hud_settings_settle(hud* h);
/* Same, with an explicit width from settings.ini: called once after settings_load. */
void hud_set_panel_width(hud* h, float w);
/* Rebuild the glyph atlas at a new size scale; returns 1 when it was rebuilt. */
int  hud_set_font_scale(hud* h, float scale);
/* Panel background colour and opacity (0..1 sRGB floats), driven by settings. */
void hud_set_panel_colors(hud* h, const float rgb[3], float alpha);

/* Panel background and text colours (0..1 sRGB floats) shared by both panels. */
void hud_colors(const hud* h, float bg[4], float text[4]);

#endif
