#define CIMGUI_USE_GLFW
#define CIMGUI_USE_OPENGL3
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimgui_impl.h"

#include "hud.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Readability profile: ImGui glyphs at font scale 1 are far cleaner than the old GDI atlas.
   Env overrides (HUD_FONT_SCALE, HUD_W, HUD_H) exist for tuning - the pixel selftest reports
   WCAG contrast and ink coverage for whatever profile is in effect. */
#define HUD_FONT_BASE 17.0f
#define HUD_FONT_SCALE 1.0f
#define HUD_MARGIN 8.0f
#define HUD_W 380.0f
#define HUD_H 96.0f

static float env_f(const char* name, float fallback) {
  const char* v = getenv(name);
  if (!v || !*v) return fallback;
  float x = strtof(v, NULL);
  return x > 0.0f ? x : fallback;
}

int hud_init(hud* h, void* window, char* err, size_t errcap) {
  memset(h, 0, sizeof *h);
  if (!window) {
    snprintf(err, errcap, "hud_init: no GLFW window");
    return -1;
  }
  ImGuiContext* ctx = igCreateContext(NULL);
  if (!ctx) {
    snprintf(err, errcap, "hud_init: igCreateContext returned NULL (imgui %s)", igGetVersion());
    return -2;
  }
  h->ctx = ctx;

  const float scale = env_f("HUD_FONT_SCALE", HUD_FONT_SCALE);
  const float w = env_f("HUD_W", HUD_W);
  const float hh = env_f("HUD_H", HUD_H);

  ImGuiStyle* st = igGetStyle();
  igStyleColorsDark(st);
  st->FontSizeBase = HUD_FONT_BASE;
  st->FontScaleMain = scale;
  st->Colors[ImGuiCol_WindowBg] = (ImVec4){ 0.08f, 0.10f, 0.14f, 0.94f };
  st->Colors[ImGuiCol_Text] = (ImVec4){ 0.95f, 0.97f, 1.00f, 1.00f };
  /* Tight info card: no border, small rounding, compact line spacing. */
  st->WindowPadding = (ImVec2){ 8.0f, 6.0f };
  st->ItemSpacing = (ImVec2){ 0.0f, 2.0f };
  st->WindowRounding = 4.0f;
  st->WindowBorderSize = 0.0f;

  if (!ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)window, true)) {
    snprintf(err, errcap, "hud_init: GLFW backend failed (imgui %s)", igGetVersion());
    return -3;
  }
  if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
    snprintf(err, errcap, "hud_init: OpenGL3 backend failed (imgui %s)", igGetVersion());
    return -4;
  }
  h->backends = 1;

  /* No saved layout: every run starts with the panel where the code puts it. */
  ImGuiIO* io = igGetIO_Nil();
  io->IniFilename = NULL;
  ImFont* font = ImFontAtlas_AddFontDefault(io->Fonts, NULL);
  if (!font) {
    snprintf(err, errcap, "hud_init: default font not created (imgui %s)", igGetVersion());
    return -5;
  }
  h->margin = HUD_MARGIN;
  h->panel_w = w;
  h->panel_h = hh;
  printf("hud: imgui %s, font scale %.2f, panel %.0fx%.0f px\n",
         igGetVersion(), scale, w, hh);
  return 0;
}

void hud_shutdown(hud* h) {
  if (!h->ctx) return;
  if (h->backends) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    h->backends = 0;
  }
  igDestroyContext((ImGuiContext*)h->ctx);
  h->ctx = NULL;
  h->frames = 0;
}

void hud_frame(hud* h, int w, int h_px, const hud_info* info) {
  if (!h->ctx) return;
  ImGuiIO* io = igGetIO_Nil();
  h->text_ready = io->FontDefault != NULL && io->Fonts->TexIsBuilt;

  ImGui_ImplGlfw_NewFrame();
  ImGui_ImplOpenGL3_NewFrame();
  /* The GLFW backend reads the display size from the window. Offscreen frames render into a
     framebuffer of a different size (and a hidden window reports an unusable size), so the
     caller's w/h is authoritative here. */
  io->DisplaySize = (ImVec2){ (float)w, (float)h_px };
  io->DisplayFramebufferScale = (ImVec2){ 1.0f, 1.0f };
  igNewFrame();

  igSetNextWindowPos((ImVec2){ h->margin, h->margin }, ImGuiCond_Always, (ImVec2){ 0.0f, 0.0f });
  igSetNextWindowSize((ImVec2){ h->panel_w, h->panel_h }, ImGuiCond_Always);
  /* Info-only panel: it must never take mouse or keyboard focus from the viewer. */
  if (!igBegin("hud", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoInputs))
    return;

  if (info) {
    char line[256];
    snprintf(line, sizeof line, "%s  fov %.0f deg  dist %.2f  fps %.1f",
             info->projection ? info->projection : "?", info->fov_deg, info->cam_dist, info->fps);
    igTextUnformatted(line, NULL);

    if (info->selected >= 0)
      snprintf(line, sizeof line, "%d tris  selected %d%s%s", info->triangles, info->selected,
               info->pinned ? " (pinned)" : " (hover)", info->wireframe ? "  wire" : "");
    else
      snprintf(line, sizeof line, "%d tris  selected none%s", info->triangles,
               info->wireframe ? "  wire" : "");
    igTextUnformatted(line, NULL);
  }

  igTextUnformatted("LMB orbit   wheel zoom   Ctrl+wheel fov   [ ] fov", NULL);
  igTextUnformatted("O ortho   W wire   R reset   S screenshot   Esc quit", NULL);

  ImVec2 pos = igGetWindowPos();
  ImVec2 size = igGetWindowSize();
  h->rect[0] = (int)pos.x;
  h->rect[1] = (int)pos.y;
  h->rect[2] = (int)(pos.x + size.x);
  h->rect[3] = (int)(pos.y + size.y);
  igEnd();
  igRender();
  h->frames++;
}

void hud_render(hud* h) {
  if (!h->ctx) return;
  ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
}

int hud_rect(const hud* h, int* x0, int* y0, int* x1, int* y1) {
  if (!h->ctx || h->frames == 0) return 0;
  *x0 = h->rect[0]; *y0 = h->rect[1]; *x1 = h->rect[2]; *y1 = h->rect[3];
  return 1;
}

/* Palette needed by the pixel selftest: panel background (blended over the scene by the
   blend state) and text colour, both in 0..1 sRGB floats. */
void hud_colors(const hud* h, float bg[4], float text[4]) {
  if (!h->ctx) { memset(bg, 0, 4 * sizeof(float)); memset(text, 0, 4 * sizeof(float)); return; }
  const ImGuiStyle* st = igGetStyle();
  bg[0] = st->Colors[ImGuiCol_WindowBg].x; bg[1] = st->Colors[ImGuiCol_WindowBg].y;
  bg[2] = st->Colors[ImGuiCol_WindowBg].z; bg[3] = st->Colors[ImGuiCol_WindowBg].w;
  text[0] = st->Colors[ImGuiCol_Text].x; text[1] = st->Colors[ImGuiCol_Text].y;
  text[2] = st->Colors[ImGuiCol_Text].z; text[3] = st->Colors[ImGuiCol_Text].w;
}
