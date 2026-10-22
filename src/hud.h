#ifndef VIEWER_HUD_H
#define VIEWER_HUD_H

#include <stddef.h>

/* Text overlay built by Dear ImGui (vendored cimgui, C ABI). The viewer stays C11:
   every C++ symbol is hidden behind this header. hud.c owns the ImGui context and the
   GLFW/OpenGL3 backends; the caller supplies only data. */

#define HUD_PANEL_STATS 0   /* camera / triangles / selection - top-left */
#define HUD_PANEL_HELP  1   /* hotkey hints - bottom-centre */

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
} hud;

/* Initialise ImGui + backends on an existing GLFW window. Returns 0 on success,
   non-zero with a message in err. */
int  hud_init(hud* h, void* window, char* err, size_t errcap);
void hud_shutdown(hud* h);

/* Build both panels for this frame (ImGui NewFrame + widgets). Call before hud_render.
   w/h are the framebuffer size in pixels; both panels are auto-sized to their text and
   re-positioned on every frame (stats top-left, help bottom-centre by pivot). */
void hud_frame(hud* h, int w, int h_px, const hud_info* info);

/* Submit the drawn panels to OpenGL. Does not swap buffers. */
void hud_render(hud* h);

/* Rect of one panel as ImGui drew it (HUD_PANEL_STATS / HUD_PANEL_HELP), in pixels with
   pixel-y from the top. Returns 0 if that panel was not drawn yet. */
int  hud_rect(const hud* h, int panel, int* x0, int* y0, int* x1, int* y1);

/* Panel background and text colours (0..1 sRGB floats) shared by both panels. */
void hud_colors(const hud* h, float bg[4], float text[4]);

#endif
