#define CIMGUI_USE_GLFW
#define CIMGUI_USE_OPENGL3
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimgui_impl.h"

#include "hud.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* Readability profile: ImGui glyphs at font scale 1 are far cleaner than the old GDI atlas.
   Env overrides (HUD_FONT_SCALE, HUD_W, HUD_HELP_W) exist for tuning - the pixel selftest
   reports WCAG contrast, ink coverage and the ink-to-panel-edge gap for whatever profile is
   in effect. Heights are never set: each panel auto-fits its text, so there are no empty rows. */
#define HUD_FONT_BASE 17.0f       /* px at scale 1.0; settings.hud_font_scale multiplies it */
#define HUD_FONT_SCALE 0.78f      /* built-in default = settings_default: 13.26 px glyph (ADR PROJECT_MEMORY 4.24) */
#define HUD_MARGIN 8.0f
#define HUD_STATS_W 0.0f    /* 0 = auto width; HUD_W pins it when set */
#define HUD_HELP_MIN_W 300.0f   /* floor: width normally comes from the hint strings themselves */
#define HUD_HELP_MAX_W 760.0f   /* ceiling: wider hints wrap into more lines, not a full-width bar */
#define HUD_SETTINGS_W 460.0f   /* pinned widget-panel width; HUD_SETTINGS_W overrides it; settings.ini default is the same */
/* Below this much space under the statistics card the settings panel is not drawn at all: a
   panel covering the card or the hints is worse than a panel that is absent. */
#define HUD_SETTINGS_MIN_H 120.0f

/* Hotkey hints live in the help panel, not in the statistics panel. */
static const char* HUD_HINT_KEYS =
  "LMB drag - rotate   RMB drag - pan   wheel - zoom   Ctrl+wheel - viewing angle   [ ] - viewing angle   "
  "= / - zoom   W - wireframe   O - ortho / perspective   R - reset camera and pin   S - screenshot   "
  "H - information panels   P - settings panel   Esc - quit";
static const char* HUD_HINT_PICK =
  "hover - outlines the triangle under the cursor   click - pin it   click again - release   "
  "Tab / Shift+Tab - step through faces   the selection is listed in the statistics card";

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
  float w = s1.x;
  if (s2.x > w) w = s2.x;
  if (s2.x > w) w = s2.x;
  w += st->WindowPadding.x * 2.0f;
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
  h->font_scale = scale;

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
  /* A window is grabbed by its title bar only. Without this the settings panel would follow the
     cursor whenever the left button is pressed anywhere inside it - including on a slider, where
     the drag is the widget's own gesture (deps/imgui/imgui/imgui.h: io.ConfigWindowsMoveFromTitleBarOnly). */
  io->ConfigWindowsMoveFromTitleBarOnly = 1;
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
  h->info_visible = 1;
  h->settings_visible = 1;
  h->settings_placed = 0;
  h->settings_w = env_f("HUD_SETTINGS_W", HUD_SETTINGS_W);
  printf("hud: imgui %s, font scale %.2f (%.2f px glyph), stats width %s, help %.0f px wide (heights auto-fit), settings panel %.0f px wide\n",
         igGetVersion(), scale, hud_font_px(h), sw > 0.0f ? "pinned by HUD_W" : "auto-fit to text", hw, h->settings_w);
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
   (deps/imgui/imgui/imgui.cpp:7743), so AlwaysAutoResize fits the height to the text.
   The return value only says whether items should be submitted: Begin can succeed on a collapsed
   window and still return false, and ImGui requires an End() in that case too
   (deps/imgui/imgui/imgui.h:7611: "You always need to call ImGui::End() even if false is
   returned"), so callers draw conditionally and End unconditionally. NoCollapse because these
   cards have no title bar to toggle from - a hidden card would have no way back. */
static int begin_panel(const char* title, float x, float y, float pw, float pivot_x, float pivot_y) {
  igSetNextWindowPos((ImVec2){ x, y }, ImGuiCond_Always, (ImVec2){ pivot_x, pivot_y });
  igSetNextWindowSize((ImVec2){ pw, 0.0f }, ImGuiCond_Always);
  if (!igBegin(title, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_AlwaysAutoResize))
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

/* One widget per settings field. Values are read from the settings struct every frame, so the
   struct is the source of truth and a widget never holds its own copy. Returns 1 when the
   widget was released after an edit - the moment a value is worth writing to settings.ini. */
static int widget_field(settings* s, const set_field* f) {
  switch (f->type) {
    case SET_BOOL: {
      bool v = settings_get(s, f) != 0;
      if (igCheckbox(f->label, &v)) settings_set(s, f, v ? 1 : 0);
      break;
    }
    case SET_FLOAT: {
      float v = settings_get(s, f);
      const char* fmt = f->fmt && *f->fmt ? f->fmt : "%.3f";
      if (igSliderFloat(f->label, &v, f->min, f->max, fmt, ImGuiSliderFlags_AlwaysClamp))
        settings_set(s, f, v);
      break;
    }
    case SET_RGB: {
      float c[3];
      settings_get_rgb(s, f, c);
      if (igColorEdit3(f->label, c, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float))
        settings_set_rgb(s, f, c);
      break;
    }
  }
  return igIsItemDeactivatedAfterEdit() ? 1 : 0;
}

/* The settings window: grouped widgets over the field table, pinned width, auto-fit height capped
   to the room under the statistics card. The mouse belongs to ImGui inside this window: the wheel
   scrolls the widget list, the title bar moves the window, the widgets edit settings.
   The collapse triangle makes Begin return false (deps/imgui/imgui/imgui.h:7611), so the widget
   list is skipped but End() still runs: skipping it aborts the process inside ImGui
   ("Missing End()", deps/imgui/imgui/imgui.cpp:11325). While collapsed, ImGui sizes the window to
   its title bar (deps/imgui/imgui/imgui.cpp:7956), so the rect recorded here is the tab the user
   clicks to expand again - and main.c keeps the mouse over that tab away from the camera.
   Position: hud_frame pins the window under the statistics card until the user grabs it. Forcing
   the position with ImGuiCond_Always every frame makes the title-bar drag impossible (ImGui moves
   the window, the next frame snaps it back), so after the first grab the position is read back
   from the window and hud_settings_place() is the only thing that pins it again. */
static void draw_settings_window(hud* h, settings* s, float pw, float room) {
  /* Ownership of the position transfers to the user the moment the left button is held with the
     cursor over the panel (the rect ImGui used on the previous frame): from then on hud_frame does
     not pin it. This is checked before SetNextWindowPos below - forcing the position with
     ImGuiCond_Always while ImGui is moving the window is exactly what cancels the title-bar drag
     (deps/imgui/imgui/imgui.h: io.ConfigWindowsMoveFromTitleBarOnly, and the move is applied in
     NewFrame before our Begin). */
  if (!h->settings_placed && igIsMouseDown_Nil(ImGuiMouseButton_Left)) {
    const ImVec2 m = igGetMousePos();
    if (m.x >= (float)h->settings_rect[0] && m.x <= (float)h->settings_rect[2] &&
        m.y >= (float)h->settings_rect[1] && m.y <= (float)h->settings_rect[3])
      h->settings_placed = 1;
  }
  if (!h->settings_placed || h->settings_pos_forced)
    igSetNextWindowPos((ImVec2){ h->settings_x, h->settings_y }, ImGuiCond_Always,
                       (ImVec2){ 0.0f, 0.0f });
  h->settings_pos_forced = 0;
  /* Width is pinned by the drag gesture, height fits the widgets but never past the room left
     under the statistics card: constraining both axes to the same width keeps the panel
     one-column. Content that does not fit is scrolled by the wheel, not painted over the hints. */
  if (pw > 0.0f)
    igSetNextWindowSizeConstraints((ImVec2){ pw, 0.0f }, (ImVec2){ pw, room }, NULL, NULL);
  const int open = igBegin("Settings", NULL,
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoSavedSettings);
  h->settings_collapsed = open ? 0 : 1;
  if (open) {
    const char* group = NULL;
    const size_t nfields = settings_field_count();
    for (size_t i = 0; i < nfields; i++) {
      const set_field* f = settings_field(i);
      if (!group || strcmp(group, f->group) != 0) {
        if (group) igSeparator();
        igText("%s", f->group);
        group = f->group;
      }
      if (widget_field(s, f)) h->settings_settled = 1;
    }
    igSeparator();
    igTextDisabled("RMB click - hide, drag it - move/resize, P - show again   wheel - scroll the list");
    h->settings_scroll = igGetScrollY();
  } else {
    h->settings_scroll = 0.0f;
  }
  const ImVec2 win = igGetWindowPos();
  if (win.x != h->settings_x || win.y != h->settings_y) {
    h->settings_x = win.x;
    h->settings_y = win.y;
    h->settings_placed = 1;
    h->settings_moved = 1;
  }
  record_rect(h->settings_rect);
  igEnd();
  /* One line per gesture, printed when the button is released - a drag moves the window on every
     frame and logging each of them buries the rest of the console. */
  if (h->settings_moved && !igIsMouseDown_Nil(ImGuiMouseButton_Left)) {
    printf("settings: panel moved by the title bar to (%d, %d)\n",
           (int)h->settings_x, (int)h->settings_y);
    h->settings_moved = 0;
  }
}

void hud_frame(hud* h, int w, int h_px, const hud_info* info, settings* s) {
  if (!h->ctx) return;
  ImGuiIO* io = igGetIO_Nil();
  h->text_ready = io->FontDefault != NULL && io->Fonts->TexIsBuilt;
  h->settings_settled = 0;

  ImGui_ImplGlfw_NewFrame();
  ImGui_ImplOpenGL3_NewFrame();
  /* The GLFW backend reads the display size from the window. Offscreen frames render into a
     framebuffer of a different size (and a hidden window reports an unusable size), so the
     caller's w/h is authoritative here. */
  io->DisplaySize = (ImVec2){ (float)w, (float)h_px };
  io->DisplayFramebufferScale = (ImVec2){ 1.0f, 1.0f };
  /* Selftest input: an offscreen framebuffer has no real cursor, so the caller injects the mouse
     state into the event queue after the GLFW backend has filled its own - queued events are
     consumed by igNewFrame in queue order, so the injected ones win.
     mouse_busy is read before igNewFrame: ImGui clears the active id inside NewFrame when the
     button was released, so the value is exactly "a HUD gesture was still held when this frame
     started", which is what main.c needs to keep the camera out of it. */
  if (h->inj_active) {
    ImGuiIO_AddMousePosEvent(io, h->inj_x, h->inj_y);
    ImGuiIO_AddMouseButtonEvent(io, ImGuiMouseButton_Left, h->inj_down != 0);
    if (h->inj_wheel != 0.0f) ImGuiIO_AddMouseWheelEvent(io, 0.0f, h->inj_wheel);
  }
  h->mouse_busy = igGetActiveID() != 0 ? 1 : 0;
  h->inj_wheel = 0.0f; /* a wheel step is one frame of input, not a held state */
  igNewFrame();

  /* Statistics panel: pinned at the top-left corner, width fixed, height = the lines it has. */
  if (h->info_visible) {
    const int open = begin_panel("hud_stats", h->margin, h->margin, h->stats_w, 0.0f, 0.0f);
    if (open && info) {
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
        /* Identity of the highlighted triangle: which face of the file it came from and which
           authored vertex numbers it uses. face_id counts valid faces, in file order. */
        snprintf(line, sizeof line, "%s  face %d/%d  tri %d  v %d,%d,%d",
                 info->pinned ? "pinned" : "hover", info->face_id, info->faces,
                 info->selected, info->vert_id[0], info->vert_id[1], info->vert_id[2]);
        igTextUnformatted(line, NULL);

        snprintf(line, sizeof line, "n=(%.2f, %.2f, %.2f)  pt=(%.1f, %.1f, %.1f)",
                 info->pin_nx, info->pin_ny, info->pin_nz, info->pin_cx, info->pin_cy, info->pin_cz);
        igTextUnformatted(line, NULL);

        /* Where the normal came from and how far the vertex normals bend away from it. */
        snprintf(line, sizeof line, "vn %s%s", info->normals_authored ? "file" : "computed",
                 info->normals_authored ? "  dev %.1f deg" : "");
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
     panel follows window resizes.

     The lines are also limited by the height that is left above them: the statistics card is
     drawn first and its bottom edge is known here, so on a small frame the least important
     hints are dropped rather than painted over the card. hud_hint_lines reports how many are
     shown, which is what the pixel selftest checks. */
  float avail = (float)w - 2.0f * h->margin;
  float pw = h->help_w < avail ? h->help_w : avail;
  if (pw < 64.0f) pw = 64.0f;
  float room = (float)h_px - 2.0f * h->margin;
  if (h->info_visible && h->stats_rect[3] > 0) room -= h->stats_rect[3] + h->margin;
  if (h->info_visible) {
    const int open = begin_panel("hud_help", (float)w * 0.5f, (float)h_px - h->margin, pw, 0.5f, 1.0f);
    if (!open) h->hint_lines = 0;   /* no items submitted this frame: nothing to measure */
    if (open) {
      const ImGuiStyle* st = igGetStyle();
      const float content_w = pw - 2.0f * st->WindowPadding.x;
      const float room_inner = room - 2.0f * st->WindowPadding.y;
      const char* hints[2] = { HUD_HINT_KEYS, HUD_HINT_PICK };
      float used = 0.0f;
      int shown = 0;
      igPushTextWrapPos(0.0f);
      for (int i = 0; i < 2; i++) {
        /* Conservative height of this hint at the panel width: the unwrapped text width divided by
           the content width (word wrapping rounds up), times the line height, plus the gap between
           items. Deliberately pessimistic - underestimating would paint over the statistics card. */
        ImVec2 sz = igCalcTextSize(hints[i], NULL, false, -1.0f);
        float lines = content_w > 0.0f && sz.x > content_w ? (float)ceil(sz.x / content_w) : 1.0f;
        float need = lines * sz.y;
        if (used + need > room_inner) break;
        igTextUnformatted(hints[i], NULL);
        used += need + st->ItemSpacing.y;
        shown++;
      }
      igPopTextWrapPos();
      h->hint_lines = shown;
    }
    record_rect(h->help_rect);
    igEnd();
  } else {
    h->hint_lines = 0;
  }

  /* Settings panel: placed under the statistics card unless the drag gesture moved it. The
     rect is recorded here so main.c can resolve mouse ownership on the next frame. It is drawn
     only when there is room between the card and the bottom edge - on a small window the panel
     would cover the statistics card or the hints, and a panel that overlaps is worse than a
     panel that is not there. Height is capped to that room so it can never cross the bottom. */
  if (s && h->settings_visible) {
    if (!h->settings_placed) {
      h->settings_x = h->margin;
      h->settings_y = h->info_visible && h->stats_rect[3] > 0 ? h->stats_rect[3] + h->margin : h->margin;
    }
    float bottom = (float)h_px - h->margin;
    /* The hints are drawn before this panel, so their top edge is known: the gap between the
     statistics card and the hints is what the settings panel may occupy. The panel is drawn
     whenever that gap is at least HUD_SETTINGS_MIN_H; a taller widget list gets a scrollbar
     instead of disappearing (main.c reports the rect and the room each frame). */
    if (h->info_visible && h->help_rect[3] > h->help_rect[1]) bottom = (float)h->help_rect[1] - h->margin;
    const float room = bottom - h->settings_y - h->margin;
    float avail = (float)w - 2.0f * h->margin;
    float pw = h->settings_w > 0.0f ? (h->settings_w < avail ? h->settings_w : avail) : 0.0f;
    if (pw > 0.0f && pw < 64.0f) pw = 64.0f;
    if (pw > 0.0f && room >= HUD_SETTINGS_MIN_H) {
      draw_settings_window(h, s, pw, room);
      h->settings_room = room;
    } else {
      memset(h->settings_rect, 0, sizeof h->settings_rect);
      h->settings_room = 0.0f;
      h->settings_collapsed = 0;
    }
  } else {
    memset(h->settings_rect, 0, sizeof h->settings_rect);
    h->settings_room = 0.0f;
    h->settings_collapsed = 0;
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
  const int* r = panel == HUD_PANEL_HELP ? h->help_rect
               : panel == HUD_PANEL_SETTINGS ? h->settings_rect
               : h->stats_rect;
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

void hud_set_info_visible(hud* h, int visible) { h->info_visible = visible ? 1 : 0; }
int  hud_info_visible(const hud* h) { return h->info_visible; }

void hud_settings_show(hud* h, int visible) {
  h->settings_visible = visible ? 1 : 0;
  if (!visible) memset(h->settings_rect, 0, sizeof h->settings_rect);
  /* Collapse state is left alone on purpose: [P] shows the window, and a window the user folded
     with the title-bar tab comes back as that tab - the same behaviour as any other ImGui
     window, and hud_settings_collapse() is the only place that changes it. Forcing an expand
     here would reopen the panel on every orbit, because orbit marks the settings dirty. */
}
int hud_settings_visible(const hud* h) { return h->settings_visible && h->settings_rect[2] > h->settings_rect[0]; }
int hud_hint_lines(const hud* h) { return h->hint_lines; }

/* Effective glyph size in px: the style bakes FontSizeBase and FontScaleMain and
   io.DisplayFramebufferScale is 1, so GetFontSize() = HUD_FONT_BASE * font_scale. */
float hud_font_px(const hud* h) { return h->font_scale * HUD_FONT_BASE; }

/* The base glyph size this module bakes at scale 1.0 - exposed so the selftest can check that
   the settings default and the HUD profile describe the same pixel size. */
float hud_font_base(void) { return HUD_FONT_BASE; }
float hud_settings_room(const hud* h) { return h->settings_room; }
float hud_settings_scroll(const hud* h) { return h->ctx ? h->settings_scroll : 0.0f; }
int hud_mouse_busy(const hud* h) { return h->ctx ? h->mouse_busy : 0; }

void hud_warp_mouse(hud* h, double x, double y, int left_down, float wheel) {
  if (!h->ctx) return;
  if (x < 0.0 || y < 0.0) { /* same convention as io.MousePos: negative means no mouse at all */
    h->inj_active = 0;
    h->inj_down = 0;
    h->inj_wheel = 0.0f;
    return;
  }
  h->inj_active = 1;
  h->inj_x = (float)x;
  h->inj_y = (float)y;
  h->inj_down = left_down;
  h->inj_wheel = wheel;
}

/* Collapse or expand the way the title-bar triangle does: the state lives in ImGui, which queues
   the toggle and applies it inside the next Begin. Only the window name is needed, so the caller
   never touches widget state - and hud_settings_collapsed() reports what was really drawn. */
void hud_settings_collapse(hud* h, int collapsed) {
  if (!h->ctx) return;
  igSetWindowCollapsed_Str("Settings", collapsed != 0, ImGuiCond_Always);
}

int hud_settings_collapsed(const hud* h) { return h->settings_collapsed; }

void hud_settings_place(hud* h, float x, float y, float w) {
  h->settings_placed = 1;
  h->settings_pos_forced = 1; /* the caller owns the position this frame, ImGui's own value is not used */
  h->settings_x = x;
  h->settings_y = y;
  if (w > 0.0f) h->settings_w = w;
}

/* A width restored from settings.ini is already the user's choice: pin it immediately instead
   of waiting for a drag release. Without this the panel would auto-fit for the first frames. */
void hud_settings_settle(hud* h) {
  if (!h->ctx || h->settings_w <= 0.0f) return;
  h->settings_settled = 1;
}

/* Apply a width coming from settings.ini. Values flow one way: the drag gesture writes the width
   into settings, settings writes it into the HUD. Called once after the load - doing it every
   frame would fight the drag gesture. */
void hud_set_panel_width(hud* h, float w) {
  if (!h->ctx || w <= 0.0f) return;
  h->settings_w = w;
  h->settings_settled = 1;
}

/* Re-bake the glyph atlas at a new size. ImGui scales AddFontDefault by FontScaleMain, so the
   scale is applied to the style first and the fonts are then replaced: with the
   renderer-has-textures backend the atlas is rebuilt inside the next NewFrame, so no explicit
   Build() call exists (and none is needed) in this vendored ImGui. */
int hud_set_font_scale(hud* h, float scale) {
  if (!h->ctx || scale <= 0.0f) return 0;
  if (fabsf(scale - h->font_scale) < 0.01f) return 0;
  ImGuiStyle* st = igGetStyle();
  st->FontScaleMain = scale;
  ImGuiIO* io = igGetIO_Nil();
  ImFontAtlas_ClearFonts(io->Fonts);
  if (!ImFontAtlas_AddFontDefault(io->Fonts, NULL)) {
    fprintf(stderr, "hud: font scale %.2f - default font not rebuilt, keeping %.2f\n", scale, h->font_scale);
    return -1;
  }
  h->font_scale = scale;
  h->text_ready = io->FontDefault != NULL;
  printf("hud: font scale %.2f (%.2f px glyph) - atlas rebuilt on the next frame\n", scale, hud_font_px(h));
  return 1;
}

/* Panel background from settings: the frame colours follow the same base so widgets and the
   information cards stay one palette. */
void hud_set_panel_colors(hud* h, const float rgb[3], float alpha) {
  if (!h->ctx) return;
  ImGuiStyle* st = igGetStyle();
  float a = alpha < 0.35f ? 0.35f : (alpha > 1.0f ? 1.0f : alpha);
  st->Colors[ImGuiCol_WindowBg] = (ImVec4){ rgb[0], rgb[1], rgb[2], a };
  st->Colors[ImGuiCol_FrameBg] = (ImVec4){
    rgb[0] * 1.35f + 0.05f, rgb[1] * 1.35f + 0.05f, rgb[2] * 1.35f + 0.05f, a };
  st->Colors[ImGuiCol_FrameBgHovered] = (ImVec4){
    rgb[0] * 1.6f + 0.12f, rgb[1] * 1.6f + 0.12f, rgb[2] * 1.6f + 0.12f, a };
}
