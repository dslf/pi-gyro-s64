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
   Env overrides (HUD_FONT_SCALE, HUD_W, HUD_HELP_W) exist for tuning - the pixel selftest
   reports WCAG contrast, ink coverage and the ink-to-panel-edge gap for whatever profile is
   in effect. Heights are never set: each panel auto-fits its text, so there are no empty rows. */
#define HUD_FONT_BASE 17.0f
#define HUD_FONT_SCALE 1.0f
#define HUD_MARGIN 8.0f
#define HUD_STATS_W 0.0f    /* 0 = auto width; HUD_W pins it when set */
#define HUD_HELP_MIN_W 300.0f   /* floor: width normally comes from the hint strings themselves */
#define HUD_HELP_MAX_W 760.0f   /* ceiling: wider hints wrap into more lines, not a full-width bar */

/* Hotkey hints live in the help panel, not in the statistics panel. */
static const char* HUD_HINT_KEYS =
  "LMB drag - rotate   MMB drag - pan   wheel - zoom   keys 1..6 - presets   F - fullscreen   "
  "W - wire   A/B/S - shading   O - outline   C - reset   H - HUD   P - screenshot   Esc - quit";
static const char* HUD_HINT_PICK =
  "drag - nearest face under cursor   pin: LMB on face   R - release pin   T - track nearest   "
  "Ctrl - pick per frame   Shift - 5x picking rate   Alt - drag without picking";

static float env_f(const char* name, float fallback) {
  const char* v = getenv(name);
  if (!v || !*v) return fallback;
  float x = strtof(v, NULL);
  return x > 0.0f ? x : fallback;
}

/* Width of the help panel from the hint strings: the panel is centred horizontally, so the
   width has to be known before Begin. Height is left to ImGui (auto-fit), which also accounts
   for wrapping at whatever width the frame allows. */
static float measure_help_width(void) {
  const ImGuiStyle* st = igGetStyle();
  ImVec2 s1 = igCalcTextSize(HUD_HINT_KEYS, NULL, false, -1.0f);
  ImVec2 s2 = igCalcTextSize(HUD_HINT_PICK, NULL, false, -1.0f);
  float w = (s1.x > s2.x ? s1.x : s2.x) + st->WindowPadding.x * 2.0f;
  return w > HUD_HELP_MIN_W ? w : HUD_HELP_MIN_W;
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
  const float sw = env_f("HUD_STATS_W", HUD_STATS_W);

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

  /* One dummy frame builds the atlas, after which glyph metrics are available for measuring. */
  int fw = 0, fh = 0;
  glfwGetFramebufferSize((GLFWwindow*)window, &fw, &fh);
  if (fw <= 0 || fh <= 0) { fw = 1024; fh = 768; }
  io->DisplaySize = (ImVec2){ (float)fw, (float)fh };
  io->DisplayFramebufferScale = (ImVec2){ 1.0f, 1.0f };
  igNewFrame();
  igEndFrame();

  float hw = measure_help_width();
  if (hw > HUD_HELP_MAX_W) hw = HUD_HELP_MAX_W;
  hw = env_f("HUD_HELP_W", hw);

  h->margin = HUD_MARGIN;
  h->stats_w = sw;
  h->help_w = hw;
  printf("hud: imgui %s, font scale %.2f, stats width %s, help %.0f px wide (heights auto-fit)\n",
         igGetVersion(), scale, sw > 0.0f ? "pinned by HUD_W" : "auto-fit to text", hw);
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

/* An info card that hugs its text: no title bar, no input focus, no dragging, position pinned
   by pivot (0,0 = top-left corner at the point, 0.5/1 = bottom-centre at the point). Width is
   set by the API; a zero height component is not "set by API" for ImGui
   (deps/imgui/imgui/imgui.cpp:7743), so AlwaysAutoResize fits the height to the text. */
static int begin_panel(const char* title, float x, float y, float pw, float pivot_x, float pivot_y) {
  igSetNextWindowPos((ImVec2){ x, y }, ImGuiCond_Always, (ImVec2){ pivot_x, pivot_y });
  igSetNextWindowSize((ImVec2){ pw, 0.0f }, ImGuiCond_Always);
  if (!igBegin(title, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize))
    return 0;
  return 1;
}

/* The rect ImGui actually used, in frame-buffer pixels (pixel-y from the top). */
static void record_rect(int* rect) {
  ImVec2 pos = igGetWindowPos();
  ImVec2 size = igGetWindowSize();
  rect[0] = (int)pos.x;
  rect[1] = (int)pos.y;
  rect[2] = (int)(pos.x + size.x);
  rect[3] = (int)(pos.y + size.y);
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

  /* Statistics panel: pinned at the top-left corner, width fixed, height = the lines it has. */
  if (begin_panel("hud_stats", h->margin, h->margin, h->stats_w, 0.0f, 0.0f)) {
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

      if (info->selected >= 0) {
        snprintf(line, sizeof line, "%s  face %d  n=(%.2f, %.2f, %.2f)  pt=(%.1f, %.1f, %.1f)",
                 info->pinned ? "pinned" : "hover", info->hovered ? info->hovered : info->selected,
                 info->pin_nx, info->pin_ny, info->pin_nz, info->pin_cx, info->pin_cy, info->pin_cz);
        igTextUnformatted(line, NULL);
      }
    }
    record_rect(h->stats_rect);
    igEnd();
  }

  /* Hotkey help: bottom-centre, and the bottom edge is pinned by the pivot, so the auto-fit
     height is ImGui's. Width comes from the hint text, capped to what the frame can hold; a
     narrow frame wraps the hints into more lines instead of clipping them (ImGui does not
     wrap by default - PushTextWrapPos(0) wraps at the window's content edge, which works
     because the width here is set by the API). DisplaySize is re-set every frame, so the
     panel follows window resizes. */
  float avail = (float)w - 2.0f * h->margin;
  float pw = h->help_w < avail ? h->help_w : avail;
  if (pw < 64.0f) pw = 64.0f;
  if (begin_panel("hud_help", (float)w * 0.5f, (float)h_px - h->margin, pw, 0.5f, 1.0f)) {
    igPushTextWrapPos(0.0f);
    igTextUnformatted(HUD_HINT_KEYS, NULL);
    igTextUnformatted(HUD_HINT_PICK, NULL);
    igPopTextWrapPos();
    record_rect(h->help_rect);
    igEnd();
  }

  igRender();
  h->frames++;
}

void hud_render(hud* h) {
  if (!h->ctx) return;
  ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
}

int hud_rect(const hud* h, int panel, int* x0, int* y0, int* x1, int* y1) {
  if (!h->ctx || h->frames == 0) return 0;
  const int* r = panel == HUD_PANEL_HELP ? h->help_rect : h->stats_rect;
  if (r[2] <= r[0] || r[3] <= r[1]) return 0;
  *x0 = r[0]; *y0 = r[1]; *x1 = r[2]; *y1 = r[3];
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
