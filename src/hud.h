#ifndef VIEWER_HUD_H
#define VIEWER_HUD_H

#include <stddef.h>

/* Text overlay built by Dear ImGui (vendored cimgui, C ABI). The viewer stays C11:
   every C++ symbol is hidden behind this header. hud.c owns the ImGui context and the
   GLFW/OpenGL3 backends; the caller supplies only data. */

typedef struct {
  const char* projection;  /* "persp" | "ortho" */
  float fov_deg;
  float cam_dist;
  int triangles;           /* triangles in the loaded mesh */
  int selected;            /* highlighted triangle index, -1 = nothing selected */
  int pinned;              /* 1 = selection pinned by a click, 0 = hover */
  int wireframe;
  double fps;              /* measured frames/second; 0 = not measured yet */
} hud_info;

typedef struct {
  void* ctx;               /* ImGuiContext* (opaque here) */
  int backends;            /* 1 = GLFW + OpenGL3 backends initialised */
  int frames;              /* frames submitted to ImGui */
  int rect[4];             /* last panel rect in pixel space: x0,y0,x1,y1 (top-left origin) */
  int text_ready;          /* font atlas built: text is drawn from the 2nd frame on */
  float margin;            /* panel offset from the top-left corner, in pixels */
  float panel_w, panel_h;  /* requested panel size in pixels (env-tunable) */
} hud;

/* Initialise ImGui + backends on an existing GLFW window. Returns 0 on success,
   non-zero with a message in err. */
int  hud_init(hud* h, void* window, char* err, size_t errcap);
void hud_shutdown(hud* h);

/* Build the panel content for this frame (ImGui NewFrame + widgets). Call before
   hud_render. w/h are the framebuffer size in pixels. */
void hud_frame(hud* h, int w, int h_px, const hud_info* info);

/* Submit the drawn panel to OpenGL. Does not swap buffers. */
void hud_render(hud* h);

/* Panel rect of the last frame; returns 0 if nothing was drawn yet. */
int  hud_rect(const hud* h, int* x0, int* y0, int* x1, int* y1);

/* Panel background and text colours (0..1 sRGB floats) for pixel metrics. */
void hud_colors(const hud* h, float bg[4], float text[4]);

#endif
