/* pi-gyro viewer: OBJ -> OpenGL 4.6 core, orbit camera, screenshot export, HUD overlay. */

#include "gl.h"
#include "vmath.h"
#include "obj.h"
#include "mesh.h"
#include "camera.h"
#include "render.h"
#include "img.h"
#include "hud.h"
#include "settings.h"

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define VERT_SHADER "shaders/model.vert"
#define FRAG_SHADER "shaders/model.frag"

typedef struct {
  GLFWwindow* win;
  camera cam;
  gpu_mesh mesh;
  obj_mesh cpu;      /* kept after upload: cursor picking and the outline read vertices */
  renderer rend;
  hud hudl;           /* ImGui text overlay, top-left of the viewport */
  int width, height;
  int wireframe;
  int hud_overlay;      /* HUD is drawn only in interactive frames; offscreen renders stay HUD-free */
  double last_draw;     /* timestamp of the previous rendered frame, for the FPS readout */
  double fps;           /* smoothed frames/second shown in the HUD */
  unsigned long long frames; /* frames drawn since the loop started */
  unsigned long long refreshes; /* repaints driven by the OS (WM_PAINT), e.g. mid-drag resize */
  int logged_w, logged_h;     /* size last printed, so one resize burst logs one line */
  double idle_report;   /* --idle-report <sec>: redraw without input and report the frame rate */
  int dirty;
  int pressing;      /* left button held down, so a release can be read as a pick */
  double pressx, pressy;
  float fov_req;      /* --fov, applied after each model load */
  int fov_given;
  int ortho_req;      /* --ortho: parallel rays instead of perspective */
  float yaw_req, pitch_req;   /* --yaw / --pitch in degrees, for reproducible shots */
  int yaw_given, pitch_given;
  double lastx, lasty;
  int hover;          /* triangle under the cursor, -1 for none */
  int locked;         /* triangle pinned by a click, -1 for none */
  char model_path[512];
  /* Settings: the ini values drive the live app through the module setters, and the widgets
     read the same struct back, so there is exactly one source of truth. */
  settings set;
  int settings_dirty;              /* a hotkey changed settings: worth persisting */
  int rpress;                      /* right button held: drag the panel or pan the camera */
  double rpressx, rpressy;
  int settings_drag;               /* the right drag started over the settings panel */
  double panel_x, panel_y;         /* panel position at the start of that drag */
  float ap_fov, ap_dist_scale;     /* framing inputs already pushed to the camera */
  int ap_ortho;
} app;

static app* of(GLFWwindow* win) { return glfwGetWindowUserPointer(win); }

/* Re-apply the command line camera request after a reset. --yaw/--pitch make
   headless shots reproducible, so a mouse convention can be checked by diffing
   two images instead of eyeballing a window. */
static void apply_camera_req(camera* c, const app* a) {
  if (a->fov_given) camera_set_fov(c, a->fov_req);
  c->ortho = a->ortho_req;
  if (a->yaw_given) c->yaw = a->yaw_req * 0.01745329252f;
  if (a->pitch_given) c->pitch = a->pitch_req * 0.01745329252f;
}

static void update_title(app* a) {
  char t[160];
  snprintf(t, sizeof t, "pi-gyro OBJ viewer - %s %.0f deg, dist %.2f%s",
           a->cam.ortho ? "orthographic" : "perspective", a->cam.fov_deg, a->cam.dist,
           a->wireframe ? " wire" : "");
  glfwSetWindowTitle(a->win, t);
}

/* The draw target's size is read from the window, not cached from a callback. Windows runs a
   modal message loop inside DefWindowProc while the user drags a window border, so glfwPollEvents
   and every GLFW callback are unreachable until the drag ends; a cached size would leave a black
   band over the newly exposed client area. glfwGetFramebufferSize is cheap, so this runs every
   frame and the viewport/aspect/HUD always match the real client size. */
static int sync_framebuffer_size(app* a) {
  int fw = a->width, fh = a->height;
  glfwGetFramebufferSize(a->win, &fw, &fh);
  if (fw < 1) fw = 1;
  if (fh < 1) fh = 1;
  if (fw == a->width && fh == a->height) return 0;
  a->width = fw;
  a->height = fh;
  a->dirty = 1;
  return 1;
}

static void on_resize(GLFWwindow* win, int w, int h) {
  app* a = of(win);
  a->width = w < 1 ? 1 : w;
  a->height = h < 1 ? 1 : h;
  a->dirty = 1;
}

/* Is the cursor over the settings panel? The rect comes from the frame ImGui already drew,
   so mouse ownership is resolved before any widget is touched (dragging a slider must never
   orbit the camera, pan, zoom or pin a triangle). */
static int mouse_in_settings(const app* a, double x, double y) {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &x0, &y0, &x1, &y1)) return 0;
  return x >= (double)x0 && x < (double)x1 && y >= (double)y0 && y < (double)y1;
}

/* Settings -> live app state, through the module setters only: the panel never reaches into
   the renderer or the camera directly. Distance is reframed only when a framing input changes,
   so a wheel zoom survives an orientation slider. */
static void apply_settings(app* a) {
  settings* s = &a->set;

  if (a->ap_fov != s->fov_deg) camera_set_fov(&a->cam, s->fov_deg);
  if (a->ap_ortho != s->ortho) camera_set_proj(&a->cam, s->ortho);
  if (a->ap_dist_scale != s->dist_scale) camera_reframe(&a->cam, s->dist_scale);
  a->ap_fov = s->fov_deg;
  a->ap_ortho = s->ortho;
  a->ap_dist_scale = s->dist_scale;
  camera_set_orientation(&a->cam, s->yaw_deg, s->pitch_deg);

  renderer_set_light(&a->rend, s->light_dir, s->light_int);
  /* Sky visibility is a multiplier, not a shader branch: turning it off leaves the directional
     light and the surface colour, so the frame is still lit and never black. */
  if (s->sky_visible) renderer_set_sky(&a->rend, s->amb_top, s->amb_bot);
  else renderer_set_sky(&a->rend, 0.0f, 0.0f);
  renderer_set_surface(&a->rend, s->base_rgb, s->shine, s->specular);
  renderer_set_background(&a->rend, s->bg_rgb);
  renderer_set_outline_color(&a->rend, s->outline_rgb);
  renderer_set_outline_visible(&a->rend, s->outline_visible);

  if (a->hudl.ctx) {
    float bgc[4], txtc[4];
    hud_colors(&a->hudl, bgc, txtc);
    hud_set_panel_colors(&a->hudl, bgc, s->hud_bg_alpha);
    hud_set_font_px(&a->hudl, s->hud_font_px);
    hud_set_info_visible(&a->hudl, s->hud_info_visible);
    hud_settings_show(&a->hudl, s->hud_settings_visible);
    hud_set_panel_width(&a->hudl, s->settings_w);
    a->hudl.help_w = s->hud_help_w;
    a->hudl.stats_w = s->hud_stats_w;
  }

  if (a->wireframe != s->wireframe) {
    a->wireframe = s->wireframe;
    a->dirty = 1;
  }
}

/* The camera moves on its own (orbit, wheel, hotkeys), so its pose is written back into the
   settings struct: the widgets then show the state the window is really in. Distance stays a
   framing input - a wheel zoom must not move the distance slider. */
static void sync_settings_camera(app* a) {
  settings* s = &a->set;
  const float yaw = a->cam.yaw * 57.29577951308232f;
  const float pitch = a->cam.pitch * 57.29577951308232f;
  if (fabsf(yaw - s->yaw_deg) > 0.05f) { s->yaw_deg = yaw; s->dirty = 1; }
  if (fabsf(pitch - s->pitch_deg) > 0.05f) { s->pitch_deg = pitch; s->dirty = 1; }
  if (fabsf(a->cam.fov_deg - s->fov_deg) > 0.05f) { s->fov_deg = a->cam.fov_deg; s->dirty = 1; }
  if (a->cam.ortho != s->ortho) { s->ortho = a->cam.ortho; s->dirty = 1; }
}

/* Persist only when something actually changed; a frame that changed nothing writes nothing. */
static void save_settings(app* a, const char* reason) {
  if (!settings_dirty(&a->set)) return;
  char err[160] = "";
  if (settings_save(&a->set, SETTINGS_FILE, err, sizeof err) != SETTINGS_OK) {
    fprintf(stderr, "settings: cannot write '%s': %s\n", SETTINGS_FILE, err);
    return;
  }
  printf("settings: saved %zu keys to '%s' (%s)\n", settings_field_count(), SETTINGS_FILE, reason);
  settings_clear_dirty(&a->set);
  a->settings_dirty = 0;
}

static void on_scroll(GLFWwindow* win, double sx, double sy) {
  app* a = of(win);
  (void)sx;
  /* Ctrl+wheel changes the viewing angle instead of the distance: that is what
     makes perspective strong (wide fov, close eye) or flat (narrow fov, far eye). */
  /* The wheel over the settings panel scrolls the widget list (ImGui owns it there); a gesture
     ImGui is still holding also belongs to it. Everywhere else the wheel stays zoom, or fov with
     Ctrl. */
  double cx = 0.0, cy = 0.0;
  glfwGetCursorPos(win, &cx, &cy);
  if (mouse_in_settings(a, cx, cy) || hud_mouse_busy(&a->hudl)) {
    a->lastx = cx;
    a->lasty = cy;
    a->dirty = 1;
    return;
  }
  if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
    camera_fov_step(&a->cam, (float)sy * 2.0f);
  else
    camera_zoom(&a->cam, (float)sy);
  /* Keep the widgets truthful about what the camera just did. */
  a->set.fov_deg = a->cam.fov_deg;
  a->set.dirty = 1;
  a->dirty = 1;
}

static int snapshot(app* a, const char* path) {
  char err[256] = "";
  framebuffer target; memset(&target, 0, sizeof target);
  target.fbo = 0;                      /* the window's default framebuffer */
  target.w = a->width; target.h = a->height;

  unsigned char* px = malloc((size_t)target.w * (size_t)target.h * 4);
  if (!px) { fprintf(stderr, "out of memory\n"); return -1; }
  int rc = fb_read_rgba(&target, px, err, sizeof err);
  if (rc == 0) rc = img_write_bmp(path, target.w, target.h, px, err, sizeof err);
  free(px);
  if (rc < 0) { fprintf(stderr, "screenshot failed: %s\n", err); return -1; }
  printf("saved %s (%dx%d)\n", path, target.w, target.h);
  return 0;
}

/* Which triangle Tab walks to next. `cur` is the pinned triangle (-1 = nothing pinned),
   `dir` is +1 forward / -1 backward. Wrapping makes the walk a loop, so stepping past the
   last face lands on the first one and past the first lands on the last. Returns -1 when
   there is nothing to walk. */
static int face_step(int cur, int total, int dir) {
  if (total <= 0) return -1;
  /* Nothing pinned yet: forward enters at the first face, backward at the last. */
  if (cur < 0) return dir < 0 ? total - 1 : 0;
  int next = cur + (dir < 0 ? -1 : 1);
  if (next >= total) next = 0;
  if (next < 0) next = total - 1;
  return next;
}

static void on_key(GLFWwindow* win, int key, int scancode, int action, int mods) {
  app* a = of(win);
  (void)scancode;
  if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

  if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q) glfwSetWindowShouldClose(win, 1);
  else if (key == GLFW_KEY_H) {
    int vis = !hud_info_visible(&a->hudl);
    hud_set_info_visible(&a->hudl, vis);
    a->set.hud_info_visible = vis;
    a->settings_dirty = 1;
    printf("hud: information panels %s\n", vis ? "shown" : "hidden");
    a->dirty = 1;
  }
  else if (key == GLFW_KEY_P) {
    /* The settings panel needs a keyboard route: once it is hidden by RMB its rect is gone,
       so a mouse click can no longer bring it back. */
    int vis = !hud_settings_visible(&a->hudl);
    hud_settings_show(&a->hudl, vis);
    a->set.hud_settings_visible = vis;
    a->settings_dirty = 1;
    printf("settings: panel %s (P)\n", vis ? "shown" : "hidden");
    a->dirty = 1;
  }
  else if (key == GLFW_KEY_W) {
    a->set.wireframe = !a->set.wireframe;
    a->settings_dirty = 1;
    a->dirty = 1;
  }
  else if (key == GLFW_KEY_O) {
    a->set.ortho = !a->set.ortho;
    a->settings_dirty = 1;
    a->dirty = 1;
  }
  else if (key == GLFW_KEY_LEFT_BRACKET) { camera_fov_step(&a->cam, -5.0f); a->set.fov_deg = a->cam.fov_deg; a->dirty = 1; }
  else if (key == GLFW_KEY_RIGHT_BRACKET) { camera_fov_step(&a->cam, 5.0f); a->set.fov_deg = a->cam.fov_deg; a->dirty = 1; }
  else if (key == GLFW_KEY_R) {
    /* Reset also releases the pin: a reset frame should not keep an old selection. */
    if (a->locked >= 0) printf("reset: released the pin on triangle %d\n", a->locked);
    a->locked = -1;
    a->hover = -1;
    renderer_set_highlight(&a->rend, &a->cpu, -1);
    /* Reset writes the settings struct too, so the widgets show the state the window is in. */
    camera_reset(&a->cam, a->mesh.fit);
    apply_camera_req(&a->cam, a);
    a->set.fov_deg = a->cam.fov_deg;
    a->set.ortho = a->cam.ortho;
    a->set.yaw_deg = a->cam.yaw * 57.29577951308232f;
    a->set.pitch_deg = a->cam.pitch * 57.29577951308232f;
    a->set.dist_scale = 1.0f;
    a->set.wireframe = 0;
    a->set.dirty = 1;
    a->ap_fov = -1.0f;
    a->ap_dist_scale = -1.0f;
    a->ap_ortho = -1;
    a->settings_dirty = 1;
    a->dirty = 1;
  }
  else if (key == GLFW_KEY_S) snapshot(a, "screenshot.bmp");
  else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) { camera_zoom(&a->cam, 1.0f); a->dirty = 1; }
  else if (key == GLFW_KEY_MINUS) { camera_zoom(&a->cam, -1.0f); a->dirty = 1; }
  else if (key == GLFW_KEY_TAB) {
    int dir = (mods & GLFW_MOD_SHIFT) ? -1 : 1;
    int next = face_step(a->locked, a->cpu.triangles, dir);
    if (next < 0) {
      fprintf(stderr, "Tab: nothing to step through - no model loaded\n");
      return;
    }
    a->locked = next;
    a->hover = next;
    renderer_set_highlight(&a->rend, &a->cpu, next);
    int face = -1, v0 = -1, v1 = -1, v2 = -1;
    char err[256] = "";
    if (obj_triangle_identity(&a->cpu, next, &face, &v0, &v1, &v2, err, sizeof err) == 0) {
      float n[3] = { 0, 0, 0 };
      float len = obj_face_normal(&a->cpu, next, n);
      printf("face %d of %d (triangle %d): vertices %d,%d,%d, normal (%.3f, %.3f, %.3f) len %.3f\n",
             face, a->cpu.faces, next, v0, v1, v2, n[0], n[1], n[2], len);
    } else {
      printf("Tab: triangle %d of %d has no identity - %s\n", next, a->cpu.triangles, err);
    }
    a->dirty = 1;
  }
}

static void handle_mouse(app* a) {
  double lx = 0, ly = 0;
  glfwGetCursorPos(a->win, &lx, &ly);
  int left = glfwGetMouseButton(a->win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  int right = glfwGetMouseButton(a->win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

  /* Right button over the settings panel: a drag moves the panel, a short press hides or
     shows it. Anywhere else the right button stays the pan gesture. */
  const int over_panel = mouse_in_settings(a, lx, ly);
  if (right && !a->rpress) {
    a->rpress = 1;
    a->rpressx = lx;
    a->rpressy = ly;
    int px0 = 0, py0 = 0, px1 = 0, py1 = 0;
    if (over_panel && hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &px0, &py0, &px1, &py1)) {
      a->settings_drag = 1;
      a->panel_x = px0;
      a->panel_y = py0;
    }
  }
  if (a->settings_drag) {
    hud_settings_place(&a->hudl, a->panel_x + (lx - a->rpressx),
                       a->panel_y + (ly - a->rpressy), -1.0f);
    a->dirty = 1;
    if (!right) {
      int px0 = 0, py0 = 0, px1 = 0, py1 = 0;
      const double moved = fabs(lx - a->rpressx) + fabs(ly - a->rpressy);
      if (moved < 4.0) {
        const int vis = !hud_settings_visible(&a->hudl);
        hud_settings_show(&a->hudl, vis);
        /* The visibility goes into settings, otherwise a panel hidden here is gone for good on
           the next start and there is no way back while the rect is not drawn. */
        a->set.hud_settings_visible = vis;
        printf("settings: panel %s\n", vis ? "shown" : "hidden");
        a->settings_dirty = 1;
      } else if (hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &px0, &py0, &px1, &py1) && px1 > px0) {
        /* Release over the panel pins the width it ended at: resizing is a panel gesture. */
        hud_settings_place(&a->hudl, a->panel_x + (lx - a->rpressx),
                           a->panel_y + (ly - a->rpressy), (float)(px1 - px0));
        /* The pinned width is a setting: it survives the restart, the position does not. */
        a->set.settings_w = (float)(px1 - px0);
        printf("settings: panel moved to (%d, %d) and pinned to %d px wide\n",
               px0, py0, px1 - px0);
        a->settings_dirty = 1;
      }
      a->rpress = 0;
      a->settings_drag = 0;
    }
    a->lastx = lx;
    a->lasty = ly;
    return;
  }
  if (!right) a->rpress = 0;
  /* The mouse belongs to the HUD while it is over the panel *or* while ImGui still holds a
     gesture started there: a slider or a scrollbar drag keeps its active item after the cursor
     leaves the window, and without this the tail of that drag would rotate the model. */
  if (over_panel || hud_mouse_busy(&a->hudl)) { a->lastx = lx; a->lasty = ly; return; }

  if (!left && !right) {
    /* A left press that never moved is a pick: it pins the triangle, or unpins
       it when the same one is clicked again. */
    if (a->pressing) {
      a->pressing = 0;
      double moved = fabs(lx - a->pressx) + fabs(ly - a->pressy);
      if (moved < 4.0) {
        float aspect = (float)a->width / (float)a->height;
        float depth = 0.0f;
        int tri = pick_triangle(&a->cam, &a->cpu, lx, ly, a->width, a->height, aspect, &depth);
        if (tri >= 0 && tri == a->locked) a->locked = -1;
        else a->locked = tri;
        renderer_set_highlight(&a->rend, &a->cpu, a->locked >= 0 ? a->locked : a->hover);
        if (tri >= 0) printf("pick: triangle %d of %d, ndc depth %.4f%s\n", tri, a->cpu.triangles, depth,
                             a->locked >= 0 ? " (pinned)" : " (unpinned)");
        else printf("pick: nothing under the cursor\n");
        a->dirty = 1;
      }
    } else {
      /* Hover outlines the triangle nearest the eye under the cursor. */
      float aspect = (float)a->width / (float)a->height;
      float depth = 0.0f;
      int tri = pick_triangle(&a->cam, &a->cpu, lx, ly, a->width, a->height, aspect, &depth);
      if (tri != a->hover && a->locked < 0) {
        a->hover = tri;
        renderer_set_highlight(&a->rend, &a->cpu, tri);
        a->dirty = 1;
      } else if (tri != a->hover) {
        a->hover = tri;   /* pinned: the outline stays on the pinned triangle */
      }
    }
    a->lastx = lx; a->lasty = ly; return;
  }

  if (left && !a->pressing) { a->pressing = 1; a->pressx = lx; a->pressy = ly; }

  float dx = (float)(lx - a->lastx);
  float dy = (float)(ly - a->lasty);
  a->lastx = lx; a->lasty = ly;

  if (left) camera_orbit(&a->cam, dx, dy);
  else if (right) camera_pan(&a->cam, dx, dy);
  a->dirty = 1;
}

/* Everything the statistics card shows, computed from the app state. Kept out of draw_scene so
   the selftest can fill the same struct and check the numbers behind the rows. */
/* Which triangle Tab walks to next is defined above on_key; the selftest below exercises it. */
static void hud_info_fill(const app* a, int sel, hud_info* info) {
  info->projection = a->cam.ortho ? "ortho" : "persp";
  info->fov_deg = a->cam.fov_deg;
  info->cam_dist = a->cam.dist;
  info->triangles = a->cpu.triangles;
  info->selected = sel;
  info->pinned = a->locked >= 0;
  info->wireframe = a->wireframe;
  info->fps = a->fps;
  info->pin_nx = info->pin_ny = info->pin_nz = 0.0f;
  info->pin_cx = info->pin_cy = info->pin_cz = 0.0f;
  info->face_id = -1;
  info->faces = a->cpu.faces;
  info->vert_id[0] = info->vert_id[1] = info->vert_id[2] = 0;
  info->normals_authored = 0;
  info->vn_dev_deg = 0.0f;
  if (sel >= 0 && sel < a->cpu.triangles && a->cpu.v) {
    const float* v = a->cpu.v + (size_t)sel * 24;   /* 3 vertices, stride 8 */
    info->pin_cx = (v[0] + v[8] + v[16]) / 3.0f;
    info->pin_cy = (v[1] + v[9] + v[17]) / 3.0f;
    info->pin_cz = (v[2] + v[10] + v[18]) / 3.0f;
    float fn[3];
    obj_face_normal(&a->cpu, sel, fn);
    info->pin_nx = fn[0]; info->pin_ny = fn[1]; info->pin_nz = fn[2];
    if (a->cpu.face_ids) info->face_id = a->cpu.face_ids[sel];
    if (a->cpu.vert_ids) {
      info->vert_id[0] = a->cpu.vert_ids[sel * 3];
      info->vert_id[1] = a->cpu.vert_ids[sel * 3 + 1];
      info->vert_id[2] = a->cpu.vert_ids[sel * 3 + 2];
    }
    info->normals_authored = obj_normal_deviation_deg(&a->cpu, sel, &info->vn_dev_deg);
  }
}

/* Draw one frame into the default framebuffer or an offscreen target. */
static void draw_scene(app* a, const framebuffer* fb) {
  int w = fb ? fb->w : a->width;
  int h = fb ? fb->h : a->height;
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  if (fb) my_glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
  my_glViewport(0, 0, w, h);
  my_glClearColor(a->rend.bg[0], a->rend.bg[1], a->rend.bg[2], 1.0f);
  my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  float aspect = (float)w / (float)h;
  camera_set_aspect(&a->cam, aspect);      /* keeps the model framed on resize */
  mat4 proj = camera_projection(&a->cam, aspect);
  mat4 view = camera_view(&a->cam);
  mat4 vp = m4_mul(&proj, &view);
  mat4 model = m4_identity();
  mat3 nmat = m3_normal(&model);
  vec3 eye = camera_eye(&a->cam);

  my_glEnable(GL_DEPTH_TEST);
  renderer_draw(&a->rend, &a->mesh, &vp, &model, &nmat, eye, a->wireframe);
  my_glDisable(GL_DEPTH_TEST);

  /* HUD: statistics panel top-left, hotkey help panel bottom-centre (both in pixel space). */
  if (a->hud_overlay) {
    hud_info info;
    hud_info_fill(a, a->locked >= 0 ? a->locked : a->hover, &info);
    hud_frame(&a->hudl, w, h, &info, a->hud_overlay ? &a->set : NULL);
    hud_render(&a->hudl);
  }

  if (fb) my_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Repaint on demand. GLFW calls this from WM_PAINT, which while Windows is inside the modal
   resize loop is the only chance to draw: without it the enlarged window shows the old frame over
   a black band until the user releases the border. */
static void on_refresh(GLFWwindow* win) {
  app* a = of(win);
  a->refreshes++;
  sync_framebuffer_size(a);
  draw_scene(a, NULL);
  glfwSwapBuffers(win);
}

static int load_model(app* a, const char* path) {
  obj_mesh_free(&a->cpu);
  memset(&a->cpu, 0, sizeof a->cpu);
  char err[256] = "";
  if (obj_parse_file(path, &a->cpu, err, sizeof err) < 0) {
    fprintf(stderr, "cannot load '%s': %s\n", path, err);
    return -1;
  }
  printf("model %s: %d faces -> %d triangles, %d vertices, %s%s\n", path, a->cpu.faces,
         a->cpu.triangles, a->cpu.vcount,
         (a->cpu.flags & OBJ_NORMALS_AUTHORED) ? "normals (from vn)" : "normals (computed)",
         (a->cpu.flags & OBJ_HAS_TEXCOORDS) ? " + uv" : "");

  if (mesh_upload(&a->cpu, &a->mesh, err, sizeof err) < 0) {
    fprintf(stderr, "mesh upload failed: %s\n", err);
    obj_mesh_free(&a->cpu);
    return -1;
  }
  camera_reset(&a->cam, a->mesh.fit);
  apply_camera_req(&a->cam, a);
  a->hover = -1;
  a->locked = -1;
  renderer_set_highlight(&a->rend, &a->cpu, -1);
  a->dirty = 1;
  return 0;
}

/* --------------------------------------------------------------------------- */

static int init_gl(app* a, int w, int h, int visible) {
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return -1; }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);

  a->win = glfwCreateWindow(w, h, "pi-gyro OBJ viewer", NULL, NULL);
  if (!a->win) {
    fprintf(stderr, "failed to create an OpenGL 4.6 core context (%dx%d)\n", w, h);
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(a->win);
  glfwSwapInterval(visible ? 1 : 0);

  char err[256] = "";
  if (gl_load(glfwGetProcAddress, err, sizeof err) < 0) {
    fprintf(stderr, "%s\n", err);
    glfwDestroyWindow(a->win);
    glfwTerminate();
    return -1;
  }

  const char* ver = gl_version_string(err, sizeof err);
  const char* gpu = gl_renderer_string(err, sizeof err);
  if (!ver) { fprintf(stderr, "%s\n", err); return -1; }
  printf("GL  : %s\n", ver);
  printf("GPU : %s\n", gpu ? gpu : "unknown");

  char hud_err[256] = "";
  if (hud_init(&a->hudl, a->win, hud_err, sizeof hud_err) != 0) {
    fprintf(stderr, "HUD init failed: %s\n", hud_err);
    glfwDestroyWindow(a->win);
    glfwTerminate();
    return -1;
  }
  return 0;
}

static int run_interactive(app* a, const char* model_path) {
  int w = 800, h = 600;
  glfwGetWindowSize(a->win, &w, &h);
  a->width = w; a->height = h;
  a->logged_w = w; a->logged_h = h;   /* the first frame is not a resize */
  glfwSetWindowUserPointer(a->win, a);
  glfwSetFramebufferSizeCallback(a->win, on_resize);
  glfwSetWindowRefreshCallback(a->win, on_refresh);
  glfwSetScrollCallback(a->win, on_scroll);
  glfwSetKeyCallback(a->win, on_key);

  char err[256] = "";
  if (renderer_init(&a->rend, VERT_SHADER, FRAG_SHADER, err, sizeof err) < 0) {
    fprintf(stderr, "shader error: %s\n", err);
    return -1;
  }
  if (load_model(a, model_path) < 0) return -2;

  /* Settings live next to the binary (settings.ini): defaults first, then the file when it is
     there. The command line wins over the file, because it is the explicit request. */
  settings_default(&a->set);
  char serr[160] = "";
  int applied = 0, clamped = 0, skipped = 0;
  int sres = settings_load(&a->set, SETTINGS_FILE, serr, sizeof serr, &applied, &clamped, &skipped);
  if (sres == SETTINGS_ERROR)
    fprintf(stderr, "settings: %s - continuing with defaults\n", serr);
  else if (sres == SETTINGS_MISSING)
    printf("settings: no '%s' yet - built-in defaults (written on the first change)\n", SETTINGS_FILE);
  else
    printf("settings: loaded '%s' - %d keys applied, %d clamped to range, %d ignored\n",
           SETTINGS_FILE, applied, clamped, skipped);

  /* A legacy file is rewritten in current keys immediately: the alias is read-only, so an old
     key left on disk would be re-migrated and re-reported on every start. The widgets are not
     marked dirty by this - the values the user sees are unchanged. */
  if (sres == SETTINGS_OK && a->set.legacy_keys > 0) {
    char werr[160] = "";
    if (settings_save(&a->set, SETTINGS_FILE, werr, sizeof werr) == SETTINGS_ERROR)
      fprintf(stderr, "settings: '%s' not rewritten - %s (legacy keys kept for this run)\n",
              SETTINGS_FILE, werr);
    else
      printf("settings: '%s' rewritten in current keys - %d legacy key(s) converted\n",
             SETTINGS_FILE, a->set.legacy_keys);
  }
  a->ap_fov = -1.0f;
  a->ap_dist_scale = -1.0f;
  a->ap_ortho = -1;
  apply_settings(a);
  apply_camera_req(&a->cam, a);

  /* The HUD owns its own defaults (HUD_* env included); the widgets start from what it built. */
  if (a->hudl.ctx) {
    float bgc[4], txtc[4];
    hud_colors(&a->hudl, bgc, txtc);
    a->set.hud_bg_alpha = bgc[3];
    a->set.hud_font_px = hud_font_px(&a->hudl);
    a->set.hud_help_w = a->hudl.help_w;
    a->set.hud_stats_w = a->hudl.stats_w;
    a->set.hud_info_visible = a->hudl.info_visible;
  }
  sync_settings_camera(a);
  if (a->fov_given) a->set.fov_deg = a->cam.fov_deg;
  if (a->ortho_req) a->set.ortho = a->cam.ortho;
  if (a->yaw_given) a->set.yaw_deg = a->cam.yaw * 57.29577951308232f;
  if (a->pitch_given) a->set.pitch_deg = a->cam.pitch * 57.29577951308232f;
  a->ap_fov = a->cam.fov_deg;
  a->ap_ortho = a->cam.ortho;
  a->ap_dist_scale = a->set.dist_scale;
  settings_clear_dirty(&a->set);
  settings_log(&a->set, "startup");

  printf("resize: live repaint - frame size read from the window every frame, redraw from WM_PAINT\n");
  printf("controls: LMB rotate | RMB pan | wheel zoom | Ctrl+wheel fov | [ ] fov | O ortho/perspective\n");
  printf("          W wire | R reset | S screenshot | Tab / Shift+Tab step faces | hover outlines, click pins | Esc/Q quit\n");
  printf("          H information panels | P settings panel\n");
  printf("settings: '%s' beside the binary - RMB click over the panel shows/hides it, drag moves it,\n", SETTINGS_FILE);
  printf("          release over it pins the width; the wheel and picking stay theirs while the cursor is inside\n");
  a->dirty = 1;
  a->hud_overlay = 1; /* the overlay belongs to the interactive window, not to measurement frames */

  /* The HUD is live text (fps, cursor pick), so the loop redraws every frame, with or without
     input. No artificial pacing: the swap/vsync and GPU set the real rate, and the fps readout
     counts frames inside a time window. */
  double t0 = glfwGetTime();
  double win_t = t0;
  unsigned long long win_frames = 0;
  while (!glfwWindowShouldClose(a->win)) {
    glfwPollEvents();
    sync_framebuffer_size(a);   /* catches a resize even if the callback was never dispatched */
    handle_mouse(a);

    double now = glfwGetTime();
    a->last_draw = now;
    a->frames++;
    win_frames++;

    /* fps counts frames inside a time window: an EMA of per-frame dt drifts whenever the OS
       wakes the loop early, so it can read higher than the frames actually swapped. */
    double el = now - win_t;
    if (el >= 0.5) {
      a->fps = (double)win_frames / el;
      win_frames = 0;
      win_t = now;
    }

    /* Widgets write into the settings struct inside hud_frame (the previous draw_scene); push
     those values to the app, then let the camera write its own pose back so the sliders stay
     truthful about orbit, wheel zoom and hotkeys. Saving happens only on a real change. */
    if (settings_dirty(&a->set)) apply_settings(a);
    sync_settings_camera(a);
    if (a->hudl.settings_settled || a->settings_dirty)
      save_settings(a, a->hudl.settings_settled ? "widget released" : "hotkey change");

    draw_scene(a, NULL);
    if (a->width != a->logged_w || a->height != a->logged_h) {
      printf("resize: %dx%d (repaints during the drag: %llu)\n", a->width, a->height, a->refreshes);
      a->logged_w = a->width;
      a->logged_h = a->height;
    }
    if (a->dirty) { update_title(a); a->dirty = 0; }
    glfwSwapBuffers(a->win);
    GLenum e = gl_check_error();
    if (e != GL_NO_ERROR) fprintf(stderr, "GL error 0x%x in frame\n", (unsigned)e);

    if (a->idle_report > 0.0 && now - t0 >= a->idle_report) {
      printf("idle: %llu frames in %.2f s, fps %.1f, resize repaints %llu\n",
             a->frames, now - t0, a->fps, a->refreshes);
      break;
    }
  }
  save_settings(a, "window closing");
  return 0;
}

static int run_render(app* a, const char* in, const char* out, int w, int h) {
  char err[256] = "";
  if (out && !out[0]) { fprintf(stderr, "no output path given\n"); return -2; }
  if (w < 16 || h < 16) { fprintf(stderr, "bad size %dx%d (minimum 16x16)\n", w, h); return -2; }
  if (w > 4096) w = 4096;
  if (h > 4096) h = 4096;

  if (renderer_init(&a->rend, VERT_SHADER, FRAG_SHADER, err, sizeof err) < 0) {
    fprintf(stderr, "shader error: %s\n", err);
    return -1;
  }
  if (load_model(a, in) < 0) return -2;
  printf("camera: %s, fov %.0f deg, dist %.2f\n",
         a->cam.ortho ? "orthographic" : "perspective", a->cam.fov_deg, a->cam.dist);

  framebuffer fb;
  if (fb_create(&fb, w, h, err, sizeof err) < 0) {
    fprintf(stderr, "offscreen target failed: %s\n", err);
    return -1;
  }
  draw_scene(a, &fb);

  unsigned char* px = malloc((size_t)w * (size_t)h * 4);
  if (!px) { fprintf(stderr, "out of memory\n"); fb_free(&fb); return -1; }

  int rc = fb_read_rgba(&fb, px, err, sizeof err);
  if (rc == 0) rc = img_write_bmp(out, w, h, px, err, sizeof err);
  double frac = img_changed_fraction(px, w, h, 8);
  printf("wrote %s (%dx%d, %.1f%% non-background)\n", out, w, h, frac * 100.0);
  free(px);
  fb_free(&fb);

  if (rc < 0) { fprintf(stderr, "render failed: %s\n", err); return -1; }
  return 0;
}

static int expect_mesh(const char* path, int want_tris, int want_flags, int* fails) {
  obj_mesh m;
  char err[256] = "";
  if (obj_parse_file(path, &m, err, sizeof err) < 0) {
    printf("  FAIL %s: %s\n", path, err);
    (*fails)++;
    return -1;
  }
  int ok = m.triangles == want_tris && (m.flags & want_flags) == want_flags && m.radius > 0.0f;
  printf("  %s %s: %d tris, %d verts, radius %.3f, span %.2f x %.2f x %.2f\n",
         ok ? "ok  " : "FAIL", path, m.triangles, m.vcount, m.radius,
         m.span[0], m.span[1], m.span[2]);
  if (!ok) (*fails)++;
  obj_mesh_free(&m);
  return 0;
}

static int expect_parse_failure(const char* path, int* fails) {
  obj_mesh m;
  char err[256] = "";
  int rc = obj_parse_file(path, &m, err, sizeof err);
  int ok = rc < 0;
  printf("  %s %s: %s\n", ok ? "ok  " : "FAIL", path, ok ? err : "parsed garbage without error");
  if (!ok) (*fails)++;
  obj_mesh_free(&m);
  return 0;
}

/* Does the bounding sphere fit inside the tighter half-angle of the frustum? */
static int sphere_fits(const camera* c, float aspect) {
  float half = deg2rad(c->fov_deg) * 0.5f;
  float apparent = asinf(c->fit / c->dist);
  float vert = half;
  float hor = atanf(tanf(half) * aspect);
  float tight = vert < hor ? vert : hor;
  return apparent <= tight + 0.02f;
}

static float ndc_x(const camera* c, float aspect, vec3 p) {
  mat4 proj = camera_projection(c, aspect);
  mat4 view = camera_view(c);
  mat4 vp = m4_mul(&proj, &view);
  float w = 0.0f;
  vec3 q = m4_map_point(&vp, p, &w);
  return w > 0.0f ? q.x / w : 1e9f;
}

static float ndc_z(const camera* c, float aspect, vec3 p) {
  mat4 proj = camera_projection(c, aspect);
  mat4 view = camera_view(c);
  mat4 vp = m4_mul(&proj, &view);
  float w = 0.0f;
  vec3 q = m4_map_point(&vp, p, &w);
  return w > 0.0f ? q.z / w : 1e9f;
}

static float ndc_y(const camera* c, float aspect, vec3 p) {
  mat4 proj = camera_projection(c, aspect);
  mat4 view = camera_view(c);
  mat4 vp = m4_mul(&proj, &view);
  float w = 0.0f;
  vec3 q = m4_map_point(&vp, p, &w);
  return w > 0.0f ? q.y / w : 1e9f;
}

/* The point on the model that faces the eye - the surface the user "grabs". */
static vec3 near_surface_point(const camera* c) {
  vec3 eye = camera_eye(c);
  vec3 dir = vec3_normalize(vec3_sub(eye, c->target));
  vec3 p = c->target;
  p.x += dir.x * c->fit;
  p.y += dir.y * c->fit;
  p.z += dir.z * c->fit;
  return p;
}

/* Mouse convention, checked for both projections: dragging must move the side
   of the model facing you in the direction you drag (grab-and-turn, not a
   camera that flies after the cursor). A mirrored orbit is only noticeable in
   perspective, where foreshortening makes the near surface the reference. */
static void orbit_drag_selftest(int* fails) {
  printf("selftest: mouse conventions\n");

  const float yaws[] = { 0.6f, -1.2f, 2.4f, 3.1f };
  const float pitches[] = { 0.25f, -0.5f, 1.1f };
  const float fovs[] = { 20.0f, 60.0f, 80.0f };
  int horizontal_ok = 1, vertical_ok = 1, pan_ok = 1;
  float worst_h = 1e9f, worst_v = 1e9f;
  int shown = 0;

  for (int ortho = 0; ortho <= 1; ortho++) {
    for (size_t i = 0; i < sizeof yaws / sizeof yaws[0]; i++) {
      for (size_t k = 0; k < sizeof pitches / sizeof pitches[0]; k++) {
        for (size_t j = 0; j < sizeof fovs / sizeof fovs[0]; j++) {
          camera c;
          camera_reset(&c, 1.0f);
          camera_set_fov(&c, fovs[j]);
          c.yaw = yaws[i];
          c.pitch = pitches[k];
          c.ortho = ortho;

          /* Track the same world point - the patch you grabbed - through the
             camera change; it must land in the direction of the drag. */
          vec3 p0 = near_surface_point(&c);
          float x0 = ndc_x(&c, 1.0f, p0);
          float y0 = ndc_y(&c, 1.0f, p0);

          camera c2 = c;
          camera_orbit(&c2, 60.0f, 0.0f);          /* drag right */
          float x1 = ndc_x(&c2, 1.0f, p0);
          if (!(x1 > x0 + 1e-3f)) {
            horizontal_ok = 0;
            if (shown < 6) { printf("    left instead of right: yaw %.2f pitch %.2f fov %.0f %s, x %.3f -> %.3f\n",
                                   c.yaw, c.pitch, c.fov_deg, c.ortho ? "ortho" : "persp", x0, x1); shown++; }
          }
          if (x1 - x0 < worst_h) worst_h = x1 - x0;

          camera c3 = c;
          camera_orbit(&c3, 0.0f, 60.0f);          /* drag down */
          float y1 = ndc_y(&c3, 1.0f, p0);
          if (!(y1 < y0 - 1e-3f)) {
            vertical_ok = 0;
            if (shown < 12) { printf("    up instead of down: yaw %.2f pitch %.2f fov %.0f %s, y %.3f -> %.3f\n",
                                     c.yaw, c.pitch, c.fov_deg, c.ortho ? "ortho" : "persp", y0, y1); shown++; }
          }
          if (y0 - y1 < worst_v) worst_v = y0 - y1;

          camera c4 = c;
          float px0 = ndc_x(&c4, 1.0f, p0);
          camera_pan(&c4, 60.0f, 0.0f);            /* drag right with RMB */
          float px1 = ndc_x(&c4, 1.0f, p0);
          if (!(px1 > px0 + 1e-3f)) pan_ok = 0;
        }
      }
    }
  }

  printf("  %s drag right turns the facing side right (both projections, fov 20/60/80), worst dx %+.4f\n",
         horizontal_ok ? "ok  " : "FAIL", worst_h);
  if (!horizontal_ok) (*fails)++;
  printf("  %s drag down turns the facing side down (both projections), worst dy %+.4f\n",
         vertical_ok ? "ok  " : "FAIL", worst_v);
  if (!vertical_ok) (*fails)++;
  printf("  %s right-drag pans the model to the right\n", pan_ok ? "ok  " : "FAIL");
  if (!pan_ok) (*fails)++;
}

static void projection_selftest(int* fails) {
  printf("selftest: projection\n");

  const float fovs[] = { FOV_MIN, 45.0f, FOV_MAX };
  const float aspects[] = { 1.0f, 16.0f / 9.0f, 0.5f };
  for (size_t i = 0; i < sizeof fovs / sizeof fovs[0]; i++) {
    camera c;
    camera_reset(&c, 1.0f);
    camera_set_fov(&c, fovs[i]);
    int ok = 1;
    for (size_t k = 0; k < sizeof aspects / sizeof aspects[0]; k++) {
      camera_set_aspect(&c, aspects[k]);
      if (!sphere_fits(&c, aspects[k])) ok = 0;
    }
    printf("  %s fov %.0f deg fits aspect 1.00/1.78/0.50 (eye %.2f from centre)\n",
           ok ? "ok  " : "FAIL", fovs[i], c.dist);
    if (!ok) (*fails)++;
  }

  /* Perspective must foreshorten: a point nearer the eye projects larger. */
  camera c;
  camera_reset(&c, 1.0f);
  c.yaw = 0.0f; c.pitch = 0.0f;                 /* eye sits on +z, looking at the origin */
  vec3 near_pt = (vec3){ 0.4f, 0.0f, 0.4f };    /* toward the eye */
  vec3 far_pt  = (vec3){ 0.4f, 0.0f, -0.4f };   /* away from it */
  float nx = ndc_x(&c, 1.0f, near_pt);
  float fx = ndc_x(&c, 1.0f, far_pt);
  int okp = nx > fx * 1.02f;
  printf("  %s perspective foreshortens: near x %.3f > far x %.3f (fov %.0f deg)\n",
         okp ? "ok  " : "FAIL", nx, fx, c.fov_deg);
  if (!okp) (*fails)++;

  /* Depth must sort front-to-back. GL_LESS keeps the SMALLER window depth, so
     the point nearer the eye must map to the smaller ndc z. With the signs of
     m[10]/m[14] flipped, far faces won and the model drew inside-out - the near
     surface was hidden, which is exactly what made mouse rotation look mirrored
     in perspective while orthographic behaved correctly. */
  float nz = ndc_z(&c, 1.0f, near_pt);
  float fz = ndc_z(&c, 1.0f, far_pt);
  int okz = nz < fz && nz > -1.0f && fz < 1.0f;
  printf("  %s perspective depth: near z %.3f < far z %.3f (both inside [-1,1])\n",
         okz ? "ok  " : "FAIL", nz, fz);
  if (!okz) (*fails)++;

  /* Orthographic must not: depth does not change apparent size. */
  c.ortho = 1;
  float ox = ndc_x(&c, 1.0f, near_pt);
  float ox2 = ndc_x(&c, 1.0f, far_pt);
  int oko = fabsf(ox - ox2) < 1e-4f;
  printf("  %s orthographic keeps size with depth (dx %.5f)\n", oko ? "ok  " : "FAIL", fabsf(ox - ox2));
  if (!oko) (*fails)++;

  float oz = ndc_z(&c, 1.0f, near_pt);
  float oz2 = ndc_z(&c, 1.0f, far_pt);
  int okoZ = oz < oz2 && oz > -1.0f && oz2 < 1.0f;
  printf("  %s orthographic depth: near z %.3f < far z %.3f\n", okoZ ? "ok  " : "FAIL", oz, oz2);
  if (!okoZ) (*fails)++;
}

/* The cursor must pick the triangle nearest the eye, in both projections,
   whatever the order of triangles in the mesh, and pixel y must map the way the
   frame is drawn (window top = model top). Four triangles: a far/near pair that
   overlap on screen (depth test), and an upper/lower pair (pixel y direction). */
static void mouse_pick_selftest(int* fails) {
  printf("selftest: cursor picking\n");

  obj_mesh m; memset(&m, 0, sizeof m);
  m.vcount = 12; m.triangles = 4; m.radius = 1.0f;
  m.v = calloc((size_t)m.vcount * 8, sizeof(float));
  if (!m.v) { printf("  FAIL pick mesh: out of memory\n"); (*fails)++; return; }

  const float tri[4][3][3] = {
    /* yaw 0 puts the eye at (0,0,2) looking along -z, so depth runs along z: the
       far plane of this mesh is z=-0.5, the near one z=+0.5. Screen x follows
       world x, screen y follows world y (window top = +y). */
    { { -0.4f, 0.4f, -0.5f }, { 0.4f, 0.4f, -0.5f }, { 0.0f, -0.4f, -0.5f } },    /* 0: far, large */
    { { -0.15f, 0.15f, 0.5f }, { 0.15f, 0.15f, 0.5f }, { 0.0f, -0.15f, 0.5f } },  /* 1: near, small, over it */
    { { -0.9f, 0.2f, -0.5f }, { -0.3f, 0.2f, -0.5f }, { -0.6f, 0.45f, -0.5f } },  /* 2: upper band */
    { { -0.9f, -0.2f, -0.5f }, { -0.3f, -0.2f, -0.5f }, { -0.6f, -0.45f, -0.5f } }, /* 3: lower band */
  };
  for (int t = 0; t < 4; t++)
    for (int i = 0; i < 3; i++) {
      float* v = &m.v[(size_t)(t * 3 + i) * 8u];
      v[0] = tri[t][i][0]; v[1] = tri[t][i][1]; v[2] = tri[t][i][2];
      v[3] = 0.0f; v[4] = 0.0f; v[5] = 1.0f;   /* facing the eye on +z */
      v[6] = 0.0f; v[7] = 0.0f;
    }

  camera c; memset(&c, 0, sizeof c);
  camera_reset(&c, m.radius);
  c.yaw = 0.0f; c.pitch = 0.0f; c.dist = 2.0f; c.aspect = 1.0f;   /* eye at (0,0,2), fov 60 */

  const int W = 100, H = 100;
  /* px 60 / py 40 lies on the far triangle only; px 50 / py 45 lies on both. */
  float dz_far = 0.0f, dz_near = 0.0f;
  int t_far  = pick_triangle(&c, &m, 60.0, 40.0, W, H, 1.0f, &dz_far);
  int t_over = pick_triangle(&c, &m, 50.0, 45.0, W, H, 1.0f, &dz_near);

  /* Where the two overlap, the nearer one must win: a result of 0 would mean the
     scan stopped at the first hit instead of testing depth. */
  int ok_depth = (t_far == 0 && t_over == 1 && dz_near < dz_far);
  printf("  %s depth wins over file order: near tri %d (%.4f) beats far tri %d (%.4f)\n",
         ok_depth ? "ok  " : "FAIL", t_over, dz_near, t_far, dz_far);
  if (!ok_depth) (*fails)++;

  c.ortho = 1;
  float dz_ortho = 0.0f, dz_ortho_far = 0.0f;
  int t_ortho = pick_triangle(&c, &m, 50.0, 45.0, W, H, 1.0f, &dz_ortho);
  int t_ortho_far = pick_triangle(&c, &m, 60.0, 40.0, W, H, 1.0f, &dz_ortho_far);
  int ok_ortho = (t_ortho == 1 && t_ortho_far == 0 && dz_ortho < dz_ortho_far);
  printf("  %s orthographic pick is depth-ordered too: near tri %d (%.4f) < far tri %d (%.4f)\n",
         ok_ortho ? "ok  " : "FAIL", t_ortho, dz_ortho, t_ortho_far, dz_ortho_far);
  if (!ok_ortho) (*fails)++;
  c.ortho = 0;

  /* Window top must be model top: the upper band is triangle 2, the lower is 3. */
  float dz = 0.0f;
  int t_top = pick_triangle(&c, &m, 29.0, 39.0, W, H, 1.0f, &dz);
  int t_bot = pick_triangle(&c, &m, 29.0, 61.0, W, H, 1.0f, &dz);
  int ok_y = (t_top == 2 && t_bot == 3);  printf("  %s pixel y maps as the frame does: top -> tri %d (want 2), bottom -> tri %d (want 3)\n",
         ok_y ? "ok  " : "FAIL", t_top, t_bot);
  if (!ok_y) (*fails)++;

  int t_none = pick_triangle(&c, &m, 99.0, 99.0, W, H, 1.0f, &dz);
  int ok_none = (t_none == -1);
  printf("  %s empty area picks nothing (%d)\n", ok_none ? "ok  " : "FAIL", t_none);
  if (!ok_none) (*fails)++;

  obj_mesh_free(&m);
}

/* Saved images must be upright: img_write_bmp takes rows top-down and writes a bottom-up DIB,
   so the first pixel row in the file is the image bottom. Pin that with known colors - a flip
   anywhere in the chain (readback, writer, atlas) would land HUD text at the wrong end. */
static void expect_bmp_orientation(int* fails) {
  const int w = 4, h = 3;
  const unsigned char want[3][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255} };  /* top, middle, bottom */
  unsigned char px[w * h * 4];
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      unsigned char* p = px + (size_t)(y * w + x) * 4u;
      p[0] = want[y][0]; p[1] = want[y][1]; p[2] = want[y][2]; p[3] = 255;
    }

  mkdir("tests/out");                     /* suite output dir; harmless if it already exists */
  const char* path = "tests/out/selftest_bmp.bmp";
  char err[256] = "";
  if (img_write_bmp(path, w, h, px, err, sizeof err) != 0) {
    printf("  FAIL bmp write: %s\n", err);
    (*fails)++;
    return;
  }

  const int rowbytes = (w * 3 + 3) & ~3;
  unsigned char hdr[54];
  unsigned char rows[3][12];
  FILE* f = fopen(path, "rb");
  if (!f) {
    printf("  FAIL cannot read back '%s'\n", path);
    (*fails)++;
    return;
  }
  size_t hdr_got = fread(hdr, 1, sizeof hdr, f);
  size_t rows_got = fread(rows, 1, sizeof rows, f);
  fclose(f);
  remove(path);

  if (hdr_got != sizeof hdr || rows_got != sizeof rows) {
    printf("  FAIL '%s' is short: %zu header bytes, %zu pixel bytes\n", path, hdr_got, rows_got);
    (*fails)++;
    return;
  }

  const int fh = (int)(hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24));
  const int bpp = hdr[28] | (hdr[29] << 8);
  printf("  header: %c%c %dx%d bpp=%d rowbytes=%d height_sign=%s\n",
         hdr[0], hdr[1], w, fh, bpp, rowbytes, fh >= 0 ? "bottom-up" : "top-down");

  /* BMP pixel bytes are B,G,R. File row fr holds image row h-1-fr, so file row 0 = bottom = blue. */
  int mismatch = 0;
  int first_x = -1, first_fr = -1;
  int got_r = 0, got_g = 0, got_b = 0;
  for (int fr = 0; fr < h; fr++) {
    const unsigned char* src = want[h - 1 - fr];
    for (int x = 0; x < w; x++) {
      const unsigned char* p = rows[fr] + (size_t)x * 3u;
      if (p[0] != src[2] || p[1] != src[1] || p[2] != src[0]) {
        if (mismatch == 0) { first_fr = fr; first_x = x; got_r = p[2]; got_g = p[1]; got_b = p[0]; }
        mismatch++;
      }
    }
  }

  int ok = mismatch == 0 && fh == h && bpp == 24 && hdr[0] == 'B' && hdr[1] == 'M';
  if (ok) {
    printf("  ok   upright: file row 0 = bottom rgb %d,%d,%d, row %d = top rgb %d,%d,%d\n",
           rows[0][2], rows[0][1], rows[0][0], h - 1, rows[h - 1][2], rows[h - 1][1], rows[h - 1][0]);
  } else {
    printf("  FAIL bmp orientation: %d mismatched pixels, first at file row %d x %d = rgb %d,%d,%d "
           "(expected bottom rgb %d,%d,%d), header %dx%d bpp=%d\n",
           mismatch, first_fr, first_x, got_r, got_g, got_b,
           want[h - 1][0], want[h - 1][1], want[h - 1][2], w, fh, bpp);
    (*fails)++;
  }
}

/* Pixels that differ between two top-down RGBA frames inside a rectangle. The two frames
   are the same scene with the HUD hidden and shown, so a difference is a HUD pixel. */
static int region_diff_count(const unsigned char* a, const unsigned char* b, int w, int h,
                             int x0, int y0, int x1, int y1, int tol) {
  int n = 0;
  for (int y = y0; y < y1 && y < h; y++)
    for (int x = x0; x < x1 && x < w; x++) {
      const unsigned char* p = a + ((size_t)y * w + x) * 4;
      const unsigned char* q = b + ((size_t)y * w + x) * 4;
      int dr = p[0] - q[0], dg = p[1] - q[1], db = p[2] - q[2];
      if (dr * dr + dg * dg + db * db > tol * tol) n++;
    }
  return n;
}

/* Perceptual helpers for HUD readability: relative luminance, sRGB linearisation and the
   WCAG contrast ratio. All thresholds are relative so another GPU/AA path still passes. */
static double lum3(unsigned r, unsigned g, unsigned b) {
  return (r * 30.0 + g * 59.0 + b * 11.0) / 100.0;
}

static double srgb_lin(double v) {
  return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double wcag_ratio(double a, double b) {
  double la = srgb_lin(a / 255.0), lb = srgb_lin(b / 255.0);
  if (la < lb) { double t = la; la = lb; lb = t; }
  return (la + 0.05) / (lb + 0.05);
}

/* One panel measured against the same scene drawn without the HUD. Ink is any pixel that is
   brighter than the panel level the blend produced at that pixel; coverage, contrast and
   glyph-run lengths are all relative so another GPU/AA path still passes. */
typedef struct {
  long ink_n, panel_n, short_runs, long_runs;
  double ink_lum, ink_max, panel_lum, coverage, contrast;
  int diff_n;
  double cx, cy;             /* centroid of the differing pixels inside the rect */
  int bx0, by0, bx1, by1;    /* bounding box of the ink; inverted when there is no ink */
} panel_m;

static void measure_panel(const unsigned char* base, const unsigned char* over, int w, int h,
                          const int* rect, const float bg[4], double bg_lum, panel_m* m) {
  memset(m, 0, sizeof *m);
  m->bx0 = rect[2]; m->by0 = rect[3]; m->bx1 = rect[0]; m->by1 = rect[1];
  double ink_sum = 0, panel_sum = 0;
  long sx = 0, sy = 0;
  for (int y = rect[1]; y < rect[3] && y < h; y++) {
    int run = 0;
    for (int x = rect[0]; x < rect[2] && x < w; x++) {
      const unsigned char* p = base + ((size_t)y * w + x) * 4;
      const unsigned char* q = over + ((size_t)y * w + x) * 4;
      int dr = p[0] - q[0], dg = p[1] - q[1], db = p[2] - q[2];
      if (dr * dr + dg * dg + db * db > 8 * 8) { sx += x; sy += y; m->diff_n++; }
      double scene = lum3(p[2], p[1], p[0]);
      double panel_ref = bg[3] * bg_lum + (1.0 - bg[3]) * scene;
      double l = lum3(q[2], q[1], q[0]);
      if (l > panel_ref + 25.0) {
        ink_sum += l; m->ink_n++;
        if (l > m->ink_max) m->ink_max = l;
        if (x < m->bx0) m->bx0 = x;
        if (x > m->bx1) m->bx1 = x;
        if (y < m->by0) m->by0 = y;
        if (y > m->by1) m->by1 = y;
        run++;
      } else {
        panel_sum += l; m->panel_n++;
        if (run > 0) { if (run <= 12) m->short_runs++; else m->long_runs++; run = 0; }
      }
    }
    if (run > 0) { if (run <= 12) m->short_runs++; else m->long_runs++; }
  }
  m->panel_lum = m->panel_n ? panel_sum / (double)m->panel_n : 0.0;
  m->ink_lum = m->ink_n ? ink_sum / (double)m->ink_n : 0.0;
  double area = (double)(rect[2] - rect[0]) * (double)(rect[3] - rect[1]);
  m->coverage = area > 0 ? 100.0 * (double)m->ink_n / area : 0.0;
  m->contrast = wcag_ratio(m->ink_lum, m->panel_lum);
  m->cx = m->diff_n ? (double)sx / (double)m->diff_n : 0.0;
  m->cy = m->diff_n ? (double)sy / (double)m->diff_n : 0.0;
}

/* Pixels changed outside both panels: the overlay must not touch anything else. */
static long outside_panels_diff(const unsigned char* base, const unsigned char* over, int w, int h,
                                const int* r1, const int* r2, const int* r3, int tol) {
  long n = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      if (x >= r1[0] && x < r1[2] && y >= r1[1] && y < r1[3]) continue;
      if (x >= r2[0] && x < r2[2] && y >= r2[1] && y < r2[3]) continue;
      /* The settings panel added a third rect. An all-zero rect means "not drawn this frame"
         and skips nothing, so the check stays strict: ink anywhere else is a leak. */
      if (r3 && x >= r3[0] && x < r3[2] && y >= r3[1] && y < r3[3]) continue;
      const unsigned char* p = base + ((size_t)y * w + x) * 4;
      const unsigned char* q = over + ((size_t)y * w + x) * 4;
      int dr = p[0] - q[0], dg = p[1] - q[1], db = p[2] - q[2];
      if (dr * dr + dg * dg + db * db > tol * tol) n++;
    }
  return n;
}


/* Both panels are measured separately against the same scene drawn without the HUD, so every
   differing pixel is HUD ink. Layout, ink containment, coverage, contrast, stroke structure
   and determinism are all numeric: a misplaced, slab-like or flipped glyph cannot pass. */
static void hud_selftest(app* a, int* fails) {
  printf("selftest: HUD overlay (stats top-left, hotkey help bottom-centre)\n");
  char err[256] = "";
  const int w = 960, h = 320;   /* wide enough for the hint lines, tall enough for both panels */
  framebuffer fb;
  if (fb_create(&fb, w, h, err, sizeof err) < 0) {
    printf("  FAIL hud framebuffer: %s\n", err);
    (*fails)++;
    return;
  }

  static unsigned char px[960 * 320 * 4];
  static unsigned char px2[960 * 320 * 4];
  static unsigned char px3[960 * 320 * 4];
  static unsigned char px4[960 * 320 * 4];
  camera_reset(&a->cam, a->mesh.fit);

  /* An explicit selection: the statistics card must show real identity rows here, not the
     empty state, and the wiring check below needs a triangle it can name. */
  float pick_depth = 0.0f;
  int sel_draw = -1;
  /* The exact centre can land on a seam between two faces, where the triangle test is
     degenerate; scan a few nearby points and take the first real hit. */
  float pox[5] = { 0.0f, 60.0f, -60.0f, 0.0f, 40.0f };
  float pov[5] = { 0.0f, 0.0f, 0.0f, 40.0f, -40.0f };
  int orig_hover = a->hover, orig_locked = a->locked;
  for (int i = 0; i < 5 && sel_draw < 0; i++) {
    float px = (float)(w / 2) + pox[i], py = (float)(h / 2) + pov[i];
    sel_draw = pick_triangle(&a->cam, &a->cpu, px, py, w, h, (float)w / (float)h, &pick_depth);
    if (sel_draw >= 0)
      printf("  picked triangle %d at (%d, %d), depth %.4f, for the card\n", sel_draw, (int)px, (int)py, pick_depth);
  }
  if (sel_draw < 0) {
    printf("  FAIL hud selftest picked nothing around the frame centre of %d x %d\n", w, h);
    (*fails)++;
    fb_free(&fb);
    return;
  }
  a->locked = sel_draw;
  a->hover = sel_draw;

  a->hud_overlay = 0;
  draw_scene(a, &fb);
  int rc = fb_read_rgba(&fb, px, err, sizeof err);
  a->hud_overlay = 1;
  /* ImGui builds the font atlas at EndFrame and the backend uploads it on the next NewFrame,
     so the first overlay frame carries no glyphs. Warm up before measuring pixels. */
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  rc |= fb_read_rgba(&fb, px2, err, sizeof err);
  draw_scene(a, &fb);
  rc |= fb_read_rgba(&fb, px3, err, sizeof err);
  a->hud_overlay = 0;
  if (rc != 0) {
    printf("  FAIL hud readback: %s\n", err);
    (*fails)++;
    a->hover = orig_hover;
    a->locked = orig_locked;
    fb_free(&fb);
    return;
  }

  int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0, hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
  if (!hud_rect(&a->hudl, HUD_PANEL_STATS, &sx0, &sy0, &sx1, &sy1)) {
    printf("  FAIL hud stats panel rect unknown - ImGui drew nothing\n");
    (*fails)++;
    fb_free(&fb);
    return;
  }
  if (!hud_rect(&a->hudl, HUD_PANEL_HELP, &hx0, &hy0, &hx1, &hy1)) {
    printf("  FAIL hud help panel rect unknown - ImGui drew nothing\n");
    (*fails)++;
    fb_free(&fb);
    return;
  }
  int stats_rect[4] = { sx0, sy0, sx1, sy1 };
  int help_rect[4] = { hx0, hy0, hx1, hy1 };
  int settings_rect[4] = { 0, 0, 0, 0 };
  hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &settings_rect[0], &settings_rect[1],
           &settings_rect[2], &settings_rect[3]);
  printf("  ok   stats rect %d..%d x %d..%d (%d x %d px)\n", sx0, sx1, sy0, sy1,
         sx1 - sx0, sy1 - sy0);
  printf("  ok   help rect %d..%d x %d..%d (%d x %d px)\n", hx0, hx1, hy0, hy1,
         hx1 - hx0, hy1 - hy0);

  /* Layout: statistics keeps the top-left corner, help is centred horizontally and sits on
     the bottom margin; the panels must stay inside the frame and not overlap each other. */
  if (sx0 > 12 || sy0 > 12) {
    printf("  FAIL stats panel at (%d, %d) - not the top-left corner\n", sx0, sy0);
    (*fails)++;
  } else {
    printf("  ok   stats pinned top-left at (%d, %d)\n", sx0, sy0);
  }

  double help_cx = (hx0 + hx1) * 0.5;
  double centre_off = fabs(help_cx - w * 0.5);
  if (centre_off > w * 0.02) {
    printf("  FAIL help panel centre x %.1f is %.1f px from the frame centre %.1f\n",
           help_cx, centre_off, w * 0.5);
    (*fails)++;
  } else {
    printf("  ok   help centred: centre x %.1f, off by %.1f px (%.2f%% of width)\n",
           help_cx, centre_off, 100.0 * centre_off / (double)w);
  }

  double bottom_gap = (double)h - (double)hy1;
  if (fabs(bottom_gap - (double)a->hudl.margin) > 6.0) {
    printf("  FAIL help panel bottom edge %d, gap to frame edge %.1f px (margin %.1f)\n",
           hy1, bottom_gap, (double)a->hudl.margin);
    (*fails)++;
  } else {
    printf("  ok   help sits on the bottom margin: edge %d, gap %.1f px\n", hy1, bottom_gap);
  }

  if (hx0 < 0 || hx1 > w || sy1 > hy0) {
    printf("  FAIL panels overlap or leave the frame (stats ..%d, help %d..%d of %d)\n",
           sy1, hx0, hx1, w);
    (*fails)++;
  } else {
    printf("  ok   panels disjoint: %d px gap between them\n", hy0 - sy1);
  }

  /* Determinism: the same frame twice must produce identical HUD pixels. */
  int moved = region_diff_count(px2, px3, w, h, 0, 0, w, h, 8);
  if (moved != 0) {
    printf("  FAIL HUD moved %d pixels between identical redraws\n", moved);
    (*fails)++;
  } else {
    printf("  ok   HUD layout is deterministic (0 pixels moved on redraw)\n");
  }

  float bgc[4], txtc[4];
  hud_colors(&a->hudl, bgc, txtc);
  double bg_lum = lum3((unsigned)(bgc[0] * 255.0f), (unsigned)(bgc[1] * 255.0f),
                       (unsigned)(bgc[2] * 255.0f));
  double txt_lum = lum3((unsigned)(txtc[0] * 255.0f), (unsigned)(txtc[1] * 255.0f),
                        (unsigned)(txtc[2] * 255.0f));
  double requested = wcag_ratio(txt_lum, bg_lum);
  printf("  ok   hud palette: text lum %.1f on panel bg lum %.1f (requested %.2f:1)\n",
         txt_lum, bg_lum, requested);
  if (requested < 4.5) {
    printf("  FAIL hud palette contrast %.2f:1 is below WCAG AA 4.5:1\n", requested);
    (*fails)++;
  }

  panel_m sm, hm;
  measure_panel(px, px2, w, h, stats_rect, bgc, bg_lum, &sm);
  measure_panel(px, px2, w, h, help_rect, bgc, bg_lum, &hm);
  printf("  ok   stats ink %ld px of %d x %d, coverage %.2f%%, ink lum %.1f (max %.1f), "
         "panel lum %.1f, contrast %.2f:1, centroid (%.1f, %.1f)\n",
         sm.ink_n, sx1 - sx0, sy1 - sy0, sm.coverage, sm.ink_lum, sm.ink_max, sm.panel_lum,
         sm.contrast, sm.cx, sm.cy);
  printf("  ok   help ink %ld px of %d x %d, coverage %.2f%%, ink lum %.1f (max %.1f), "
         "panel lum %.1f, contrast %.2f:1, centroid (%.1f, %.1f)\n",
         hm.ink_n, hx1 - hx0, hy1 - hy0, hm.coverage, hm.ink_lum, hm.ink_max, hm.panel_lum,
         hm.contrast, hm.cx, hm.cy);

  if (sm.ink_n < 40 || hm.ink_n < 40) {
    printf("  FAIL hud ink: stats %ld px, help %ld px - an overlay panel is not visible\n",
           sm.ink_n, hm.ink_n);
    (*fails)++;
  }
  if (sm.coverage > 45.0 || hm.coverage > 45.0) {
    printf("  FAIL hud ink covers stats %.2f%% / help %.2f%% of its panel - a slab, not glyphs\n",
           sm.coverage, hm.coverage);
    (*fails)++;
  }
  /* Readability floor: WCAG AA for body text is 4.5:1. */
  if (sm.contrast < 4.5) {
    printf("  FAIL stats text contrast %.2f:1 is below WCAG AA 4.5:1\n", sm.contrast);
    (*fails)++;
  }
  if (hm.contrast < 4.5) {
    printf("  FAIL help text contrast %.2f:1 is below WCAG AA 4.5:1\n", hm.contrast);
    (*fails)++;
  }

  /* Tightness: the ink must reach within the window padding of every panel edge. A reserved
     empty row or field would show up as a large gap between the ink box and the panel edge.
     The help panel's right edge carries word-wrap slack (the last word of a wrapped line),
     so it gets a wider budget. */
  int gap[8] = { sm.bx0 - sx0, sx1 - sm.bx1, sm.by0 - sy0, sy1 - sm.by1,
                 hm.bx0 - hx0, hx1 - hm.bx1, hm.by0 - hy0, hy1 - hm.by1 };
  const char* gap_name[8] = { "stats left", "stats right", "stats top", "stats bottom",
                              "help left", "help right", "help top", "help bottom" };
  int gap_budget[8] = { 14, 14, 14, 14, 14, 80, 14, 14 };
  int worst_gap = 0;
  for (int i = 0; i < 8; i++) {
    if (gap[i] > worst_gap) worst_gap = gap[i];
    if (gap[i] > gap_budget[i]) {
      printf("  FAIL %s edge reserves %d px of empty panel (ink box .. panel edge, budget %d px)\n",
             gap_name[i], gap[i], gap_budget[i]);
      (*fails)++;
    }
  }
  printf("  ok   panels hug their text: worst empty gap %d px\n", worst_gap);

  /* Empty-state proof: with nothing selected the statistics card must shrink - a row that is
     not drawn must not be reserved. ImGui sizes an auto-resizing window from the *previous*
     frame's content, so warm up with two frames before reading the rect. */
  int save_hover = a->hover, save_locked = a->locked;
  a->hover = -1;
  a->locked = -1;
  a->hud_overlay = 0;
  draw_scene(a, &fb);            /* same scene, no HUD: the baseline for the ink count */
  int qrc = fb_read_rgba(&fb, px4, err, sizeof err);
  a->hud_overlay = 1;
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  qrc |= fb_read_rgba(&fb, px3, err, sizeof err);
  a->hud_overlay = 0;
  a->hover = save_hover;
  a->locked = save_locked;
  int qx0 = 0, qy0 = 0, qx1 = 0, qy1 = 0;
  if (qrc != 0 || !hud_rect(&a->hudl, HUD_PANEL_STATS, &qx0, &qy0, &qx1, &qy1)) {
    printf("  FAIL hud empty-state frame: %s\n", err);
    (*fails)++;
  } else {
    int qrect[4] = { qx0, qy0, qx1, qy1 };
    panel_m qm;
    measure_panel(px4, px3, w, h, qrect, bgc, bg_lum, &qm);
    int qgap[4] = { qm.bx0 - qx0, qx1 - qm.bx1, qm.by0 - qy0, qy1 - qm.by1 };
    printf("  ok   stats with no selection: %d x %d px (was %d x %d), ink %ld px, "
           "contrast %.2f:1, gaps L %d R %d T %d B %d px\n",
           qx1 - qx0, qy1 - qy0, sx1 - sx0, sy1 - sy0, qm.ink_n, qm.contrast,
           qgap[0], qgap[1], qgap[2], qgap[3]);
    if ((qx1 - qx0) >= (sx1 - sx0) || (qy1 - qy0) >= (sy1 - sy0)) {
      printf("  FAIL stats card does not shrink without a selection: %d x %d vs %d x %d px\n",
             qx1 - qx0, qy1 - qy0, sx1 - sx0, sy1 - sy0);
      (*fails)++;
    }
    /* The identity rows (face, vertices, normal, vn source) must exist only with a selection:
       the selected card is expected to be about twice as tall as the two-line empty card. */
    double h_ratio = (double)(sy1 - sy0) / (double)(qy1 - qy0);
    printf("  ok   identity rows: selected card is %.2fx taller than the empty card\n", h_ratio);
    if (h_ratio < 1.8) {
      printf("  FAIL selected card %d x %d px vs empty %d x %d px - identity rows are missing\n",
             sx1 - sx0, sy1 - sy0, qx1 - qx0, qy1 - qy0);
      (*fails)++;
    }
    /* The rows are only worth showing if the numbers behind them are the mesh's own numbers. */
    hud_info info;
    hud_info_fill(a, sel_draw, &info);
    double nl = (double)info.pin_nx * info.pin_nx + (double)info.pin_ny * info.pin_ny +
                (double)info.pin_nz * info.pin_nz;
    printf("  ok   card values: sel %d of %d triangles, face %d/%d, vertices %d,%d,%d, "
           "normal (%.2f, %.2f, %.2f) len %.2f, vn %s dev %.1f deg\n", info.selected,
           info.triangles, info.face_id, info.faces, info.vert_id[0], info.vert_id[1],
           info.vert_id[2], info.pin_nx, info.pin_ny, info.pin_nz, nl,
           info.normals_authored ? "file" : "computed", (double)info.vn_dev_deg);
    int wiring_bad = info.selected < 0 || info.face_id < 0 || info.face_id >= info.faces ||
                     info.vert_id[0] < 1 || info.vert_id[1] < 1 || info.vert_id[2] < 1 ||
                     info.vert_id[0] == info.vert_id[1] || info.vert_id[1] == info.vert_id[2] ||
                     info.vert_id[0] == info.vert_id[2] || nl < 0.99 || nl > 1.01 ||
                     info.normals_authored != ((a->cpu.flags & OBJ_NORMALS_AUTHORED) != 0) ||
                     info.vn_dev_deg < 0.0f || info.vn_dev_deg > 180.0f;
    if (wiring_bad) {
      printf("  FAIL card values are not the mesh's own: face %d of %d, vertices %d,%d,%d, "
             "normal length %.2f, vn %s dev %.1f deg\n", info.face_id, info.faces,
             info.vert_id[0], info.vert_id[1], info.vert_id[2], nl,
             info.normals_authored ? "file" : "computed", (double)info.vn_dev_deg);
      (*fails)++;
    }
    if (qm.ink_n < 40 || qm.contrast < 4.5) {
      printf("  FAIL stats empty-state ink %ld px, contrast %.2f:1 - the card must stay readable\n",
             qm.ink_n, qm.contrast);
      (*fails)++;
    }
    for (int i = 0; i < 4; i++) {
      if (qgap[i] > 14) {
        printf("  FAIL stats empty-state reserves %d px of empty panel (budget 14 px)\n", qgap[i]);
        (*fails)++;
      }
    }
  }

  /* Where the ink sits: statistics in the top-left half, help in the bottom half. */
  if (sm.cy >= h * 0.5 || sm.cx >= w * 0.5) {
    printf("  FAIL stats ink centroid (%.1f, %.1f) is outside the top-left half of %dx%d\n",
           sm.cx, sm.cy, w, h);
    (*fails)++;
  } else {
    printf("  ok   stats ink centroid (%.1f, %.1f) sits top-left\n", sm.cx, sm.cy);
  }
  if (hm.cy < h * 0.5) {
    printf("  FAIL help ink centroid y %.1f is outside the bottom half of %d\n", hm.cy, h);
    (*fails)++;
  } else {
    printf("  ok   help ink centroid (%.1f, %.1f) sits bottom-centre\n", hm.cx, hm.cy);
  }

  long outside = outside_panels_diff(px, px2, w, h, stats_rect, help_rect, settings_rect, 8);  if (outside != 0) {
    printf("  FAIL hud ink leaked outside the panel rects: %ld pixels\n", outside);
    (*fails)++;
  } else {
    printf("  ok   hud ink confined to the panel rects (0 pixels outside, settings panel %d x %d)\n",
           settings_rect[2] - settings_rect[0], settings_rect[3] - settings_rect[1]);
  }

  /* The three panels must not overlap: a panel painted over another one reads as noise. */
  long overlap = 0;
  for (int y = settings_rect[1]; y < settings_rect[3]; y++)
    for (int x = settings_rect[0]; x < settings_rect[2]; x++) {
      if (x >= stats_rect[0] && x < stats_rect[2] && y >= stats_rect[1] && y < stats_rect[3]) overlap++;
      if (x >= help_rect[0] && x < help_rect[2] && y >= help_rect[1] && y < help_rect[3]) overlap++;
    }
  if (overlap != 0) {
    printf("  FAIL settings panel overlaps the info panels by %ld px\n", overlap);
    (*fails)++;
  } else {
    printf("  ok   panels are disjoint: stats %d..%d, settings %d..%d x %d..%d, help from %d\n",
           stats_rect[1], stats_rect[3], settings_rect[0], settings_rect[2],
           settings_rect[1], settings_rect[3], help_rect[1]);
  }

  /* Stroke structure: glyph strokes are short runs; a solid bar would give long runs. */
  printf("  ok   stats runs: short(<=12px)=%ld long(>12px)=%ld\n", sm.short_runs, sm.long_runs);
  printf("  ok   help runs: short(<=12px)=%ld long(>12px)=%ld\n", hm.short_runs, hm.long_runs);
  if (sm.short_runs < 20 || hm.short_runs < 20) {
    printf("  FAIL hud glyph runs: stats %ld, help %ld short runs - text is not drawn\n",
           sm.short_runs, hm.short_runs);
    (*fails)++;
  }
  if (sm.long_runs > sm.short_runs || hm.long_runs > hm.short_runs) {
    printf("  FAIL hud ink dominated by long runs (stats %ld/%ld, help %ld/%ld) - solid bars\n",
           sm.long_runs, sm.short_runs, hm.long_runs, hm.short_runs);
    (*fails)++;
  }

  /* A narrow frame must wrap the hints rather than clip them: the help panel caps to the
     frame width and grows vertically. Same checks on a 420 px wide frame. */
  framebuffer narrow;
  if (fb_create(&narrow, 420, 320, err, sizeof err) < 0) {
    printf("  FAIL hud narrow framebuffer: %s\n", err);
    (*fails)++;
  } else {
    const int nw = 420, nh = 320;
    camera_reset(&a->cam, a->mesh.fit);
    a->hud_overlay = 0;
    draw_scene(a, &narrow);
    int nrc = fb_read_rgba(&narrow, px, err, sizeof err);
    a->hud_overlay = 1;
    draw_scene(a, &narrow);
    draw_scene(a, &narrow);
    draw_scene(a, &narrow);
    nrc |= fb_read_rgba(&narrow, px2, err, sizeof err);
    a->hud_overlay = 0;
    if (nrc != 0) {
      printf("  FAIL hud narrow readback: %s\n", err);
      (*fails)++;
    } else {
      int nsx0, nsy0, nsx1, nsy1, nhx0, nhy0, nhx1, nhy1;
      if (!hud_rect(&a->hudl, HUD_PANEL_STATS, &nsx0, &nsy0, &nsx1, &nsy1) ||
          !hud_rect(&a->hudl, HUD_PANEL_HELP, &nhx0, &nhy0, &nhx1, &nhy1)) {
        printf("  FAIL hud panels missing on a %dx%d frame\n", nw, nh);
        (*fails)++;
      } else {
        int nstats[4] = { nsx0, nsy0, nsx1, nsy1 };
        int nhelp[4] = { nhx0, nhy0, nhx1, nhy1 };
        int nsettings[4] = { 0, 0, 0, 0 };
        hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &nsettings[0], &nsettings[1],
                 &nsettings[2], &nsettings[3]);
        printf("  ok   narrow frame %dx%d: stats %d..%d x %d..%d, help %d..%d x %d..%d\n",
               nw, nh, nsx0, nsx1, nsy0, nsy1, nhx0, nhx1, nhy0, nhy1);
        double ncentre = (nhx0 + nhx1) * 0.5;
        if (nhx0 < 0 || nhx1 > nw) {
          printf("  FAIL narrow help panel %d..%d leaves the %d px frame\n", nhx0, nhx1, nw);
          (*fails)++;
        } else if (fabs(ncentre - nw * 0.5) > nw * 0.02) {
          printf("  FAIL narrow help centre x %.1f is %.1f px from the frame centre %.1f\n",
                 ncentre, fabs(ncentre - nw * 0.5), nw * 0.5);
          (*fails)++;
        } else if (fabs((double)nh - (double)nhy1 - (double)a->hudl.margin) > 6.0) {
          printf("  FAIL narrow help bottom edge %d (frame %d, margin %.1f)\n",
                 nhy1, nh, (double)a->hudl.margin);
          (*fails)++;
        } else if (nsy1 > nhy0) {
          printf("  FAIL narrow panels overlap (stats ..%d, help from %d)\n", nsy1, nhy0);
          (*fails)++;
        } else {
          printf("  ok   narrow help stays centred and on the bottom margin, %d px below stats\n",
                 nhy0 - nsy1);
        }
        panel_m nm;
        measure_panel(px, px2, nw, nh, nhelp, bgc, bg_lum, &nm);
        long nout = outside_panels_diff(px, px2, nw, nh, nstats, nhelp, nsettings, 8);
        long nover = 0;
        for (int y = nsettings[1]; y < nsettings[3]; y++)
          for (int x = nsettings[0]; x < nsettings[2]; x++) {
            if (x >= nstats[0] && x < nstats[2] && y >= nstats[1] && y < nstats[3]) nover++;
            if (x >= nhelp[0] && x < nhelp[2] && y >= nhelp[1] && y < nhelp[3]) nover++;
          }
        printf("  ok   narrow frame: hints %d of 2 fit, settings panel %d x %d, overlap %ld px\n",
               hud_hint_lines(&a->hudl), nsettings[2] - nsettings[0], nsettings[3] - nsettings[1], nover);
        if (nover != 0) {
          printf("  FAIL settings panel overlaps the info panels by %ld px on a narrow frame\n", nover);
          (*fails)++;
        }
        if (hud_hint_lines(&a->hudl) < 1) {
          printf("  FAIL no hint lines fit on the narrow frame - the help panel is empty\n");
          (*fails)++;
        }
        printf("  ok   narrow help ink %ld px of %d x %d, coverage %.2f%%, contrast %.2f:1\n",
               nm.ink_n, nhx1 - nhx0, nhy1 - nhy0, nm.coverage, nm.contrast);
        if (nm.coverage > 45.0) {
          printf("  FAIL narrow help ink covers %.2f%% of its panel - a slab, not glyphs\n", nm.coverage);
          (*fails)++;
        }
        if (nm.ink_n < 40) {
          printf("  FAIL narrow help ink %ld px - hints are clipped away\n", nm.ink_n);
          (*fails)++;
        }
        if (nm.contrast < 4.5) {
          printf("  FAIL narrow help contrast %.2f:1 is below WCAG AA 4.5:1\n", nm.contrast);
          (*fails)++;
        }
        /* Tightness on the narrow frame too: wrapping must not leave empty rows. */
        int ngap[4] = { nm.bx0 - nhx0, nhx1 - nm.bx1, nm.by0 - nhy0, nhy1 - nm.by1 };
        const char* ngap_name[4] = { "left", "right", "top", "bottom" };
        int ngap_budget[4] = { 14, 80, 14, 14 };
        for (int i = 0; i < 4; i++) {
          if (ngap[i] > ngap_budget[i]) {
            printf("  FAIL narrow help %s edge reserves %d px of empty panel (budget %d px)\n",
                   ngap_name[i], ngap[i], ngap_budget[i]);
            (*fails)++;
          }
        }
        printf("  ok   narrow help hugs its text: gaps L %d R %d T %d B %d px\n",
               ngap[0], ngap[1], ngap[2], ngap[3]);
        if (nout != 0) {
          printf("  FAIL hud ink leaked outside the panel rects on a narrow frame: %ld px\n", nout);
          (*fails)++;
        }
        /* Wrapping proof: on a frame tall enough for everything the narrow panel is taller and
           narrower. When it is not, hud drops the tail hint rather than painting over the
           statistics card - the card is the more valuable panel - and then the narrow panel is
           shorter. hud_hint_lines says which case happened, so neither is faked. */
        int hints_all = hud_hint_lines(&a->hudl) >= 2;
        if ((nhx1 - nhx0) < (hx1 - hx0) && ((nhy1 - nhy0) > (hy1 - hy0) || !hints_all)) {
          printf("  ok   hints %s: %d x %d -> %d x %d px (narrower, %s)\n",
                 hints_all ? "wrap" : "drop", hx1 - hx0, hy1 - hy0, nhx1 - nhx0, nhy1 - nhy0,
                 hints_all ? "taller" : "shorter, tail hint omitted to keep the card clear");
        } else {
          printf("  FAIL narrow help %d x %d did not wrap from %d x %d\n",
                 nhx1 - nhx0, nhy1 - nhy0, hx1 - hx0, hy1 - hy0);
          (*fails)++;
        }
      }
      fb_free(&narrow);
    }
  }

  a->hover = orig_hover;
  a->locked = orig_locked;
  fb_free(&fb);
}

/* Settings: the ini is the contract between the widgets and the app, so it is checked without
   a mouse - keys, ranges, roundtrip, and the panel pixels the ini produces. */
static void settings_selftest(app* a, int* fails) {
  printf("selftest: settings.ini (keys, clamps, roundtrip) and the widget panel\n");
  char err[256] = "";
  const size_t nf = settings_field_count();
  if (nf < 20) {
    printf("  FAIL settings has only %zu fields - the widget table is incomplete\n", nf);
    (*fails)++;
  }

  settings d;
  settings_default(&d);
  int defaults_ok = d.fov_deg == 60.0f && d.hud_settings_visible == 1 && d.sky_visible == 1 &&
                    d.outline_visible == 1 && d.settings_w == 460.0f && d.hud_bg_alpha == 0.94f &&
                    fabsf(d.hud_font_px - 13.26f) < 1e-6f && !settings_dirty(&d);
  printf("  %s defaults: fov %.0f, panel %d px wide, shown %d, sky %d, outline %d, hud font %.2f px\n",
         defaults_ok ? "ok  " : "FAIL", d.fov_deg, (int)d.settings_w, d.hud_settings_visible,
         d.sky_visible, d.outline_visible, d.hud_font_px);
  if (!defaults_ok) (*fails)++;

  /* The HUD profile baked before settings.ini and the settings default must be the same pixel
     size, or --render/--selftest metrics and the interactive panel would disagree (ADR 4.24).
     HUD_FONT_PX is a tuning knob that overrides the baked profile on purpose, so the equality
     against the settings default is only asserted when no override is in effect. The settings key
     is a px size now, so the readout is in px; SETTINGS_FONT_BASE is what the legacy
     hud_font_scale key multiplied, and it must be the same base the atlas is baked from. */
  const int env_override = getenv("HUD_FONT_PX") != NULL || getenv("HUD_FONT_SCALE") != NULL;
  float px = hud_font_px(&a->hudl);
  int font_agrees = hud_font_base() == SETTINGS_FONT_BASE &&
                    (env_override || fabsf(px - d.hud_font_px) < 0.01f);
  if (env_override)
    printf("  %s hud glyph size: %.2f px (HUD_FONT_PX/HUD_FONT_SCALE override, settings default %.2f px)\n",
           font_agrees ? "ok  " : "FAIL", px, d.hud_font_px);
  else
    printf("  %s hud glyph size: %.2f px == settings default %.2f px (atlas base %.0f px)\n",
           font_agrees ? "ok  " : "FAIL", px, d.hud_font_px, hud_font_base());
  if (!font_agrees) (*fails)++;

  /* Roundtrip over every key: save then read back into a fresh struct. */
  settings s;
  settings_default(&s);
  s.fov_deg = 30.0f; s.dist_scale = 2.5f; s.wireframe = 1; s.settings_w = 450.0f;
  s.hud_settings_visible = 0; s.sky_visible = 0; s.outline_visible = 0;
  s.bg_rgb[0] = 0.11f; s.bg_rgb[1] = 0.22f; s.bg_rgb[2] = 0.33f;
  int saved = settings_save(&s, "build/settings_selftest.ini", err, sizeof err);
  if (saved != SETTINGS_OK) {
    printf("  FAIL settings save: %s\n", err);
    (*fails)++;
  } else {
    int applied = 0, clamped = 0, skipped = 0;
    settings r;
    int rc = settings_load(&r, "build/settings_selftest.ini", err, sizeof err,
                           &applied, &clamped, &skipped);
    int fields_ok = rc == SETTINGS_OK && applied == (int)nf && clamped == 0 && skipped == 0;
    for (size_t i = 0; i < nf && fields_ok; i++) {
      const set_field* f = settings_field(i);
      float got = settings_get(&r, f);
      float want = settings_get(&s, f);
      if (f->type == SET_RGB) {
        float c[3], w3[3];
        settings_get_rgb(&r, f, c);
        settings_get_rgb(&s, f, w3);
        for (int k = 0; k < 3; k++)
          if (fabsf(c[k] - w3[k]) > 1e-4f) { fields_ok = 0; break; }
      } else if (fabsf(got - want) > 1e-4f) fields_ok = 0;
    }
    printf("  %s roundtrip: %d keys applied, %d clamped, %d ignored, values match\n",
           fields_ok ? "ok  " : "FAIL", applied, clamped, skipped);
    if (!fields_ok) (*fails)++;
    if (settings_dirty(&r)) {
      printf("  FAIL settings_load left the struct dirty - a read must not count as a change\n");
      (*fails)++;
    }
  }

  /* A damaged file: unknown key, a line without '=', a bool that is not a bool, a value out of
   range. Every one of them must be reported and must not poison the defaults. */
  FILE* fp = fopen("build/settings_selftest_bad.ini", "w");
  if (!fp) {
    printf("  FAIL cannot write build/settings_selftest_bad.ini\n");
    (*fails)++;
  } else {
    fprintf(fp, "# comment\nfov_deg = 200\nno_equals_here\nbogus_key = 1\northo = maybe\n");
    fprintf(fp, "settings_w = 40\nlight_dir = 0.5 0.5 0.5\n");
    fclose(fp);
    settings b;
    int applied = 0, clamped = 0, skipped = 0;
    int rc = settings_load(&b, "build/settings_selftest_bad.ini", err, sizeof err,
                           &applied, &clamped, &skipped);
    const int expect = 3, expect_clamp = 2, expect_skip = 3;
    const int ok = rc == SETTINGS_OK && applied == expect && clamped == expect_clamp &&
                   skipped == expect_skip && b.fov_deg == 90.0f && b.settings_w == 120.0f &&
                   b.ortho == 0 && b.amb_top == 1.0f;
    printf("  %s damaged file: %d applied (want %d), %d clamped (want %d), %d ignored (want %d), "
           "fov %.0f, panel %.0f px\n", ok ? "ok  " : "FAIL", applied, expect, clamped, expect_clamp,
           skipped, expect_skip, b.fov_deg, b.settings_w);
    if (!ok) (*fails)++;
  }

  /* An ini written by the previous version carries `hud_font_scale`, a multiplier over the atlas
     base. It must land in the current key as the same pixels, and saving must not write the
     legacy key back. */
  FILE* lf = fopen("build/settings_selftest_legacy.ini", "w");
  if (!lf) {
    printf("  FAIL cannot write build/settings_selftest_legacy.ini\n");
    (*fails)++;
  } else {
    fprintf(lf, "hud_font_scale = 0.73\n");
    fclose(lf);
    settings lg;
    int lapp = 0, lcl = 0, lsk = 0;
    int lrc = settings_load(&lg, "build/settings_selftest_legacy.ini", err, sizeof err,
                            &lapp, &lcl, &lsk);
    const float want_px = SETTINGS_FONT_BASE * 0.73f;   /* 12.41 px - the smallest size read on screen */
    const int legacy_ok = lrc == SETTINGS_OK && lapp == 1 && lcl == 0 && lsk == 0 &&
                          lg.legacy_keys == 1 &&
                          fabsf(lg.hud_font_px - want_px) < 1e-3f;
    printf("  %s legacy hud_font_scale 0.73 -> hud_font_px %.2f (want %.2f px, atlas base %.0f px, %d legacy key counted)\n",
           legacy_ok ? "ok  " : "FAIL", lg.hud_font_px, want_px, hud_font_base(), lg.legacy_keys);
    if (!legacy_ok) (*fails)++;

    /* Rewrite through settings_save: the legacy key is gone, so a re-read applies exactly the
       current keys and the pixel size survives. */
    settings ls; settings_default(&ls); ls.hud_font_px = lg.hud_font_px;
    if (settings_save(&ls, "build/settings_selftest_legacy.ini", err, sizeof err) != 0) {
      printf("  FAIL rewriting the legacy file through settings_save\n");
      (*fails)++;
    } else {
      settings lb;
      int a2 = 0, c2 = 0, s2 = 0;
      settings_load(&lb, "build/settings_selftest_legacy.ini", err, sizeof err, &a2, &c2, &s2);
      const int rew_ok = a2 == (int)nf && s2 == 0 && lb.legacy_keys == 0 &&
                         fabsf(lb.hud_font_px - want_px) < 1e-3f;
      printf("  %s after save: %d keys applied (want %zu), %d ignored, %d legacy keys left, hud font %.2f px\n",
             rew_ok ? "ok  " : "FAIL", a2, nf, s2, lb.legacy_keys, lb.hud_font_px);
      if (!rew_ok) (*fails)++;
    }
  }

  /* A file that is not there is not an error: defaults survive. */
  settings m;
  int applied = 0, clamped = 0, skipped = 0;
  int rc = settings_load(&m, "build/settings_does_not_exist.ini", err, sizeof err,
                         &applied, &clamped, &skipped);
  const int missing_ok = rc == SETTINGS_MISSING && applied == 0 && m.fov_deg == 60.0f;
  printf("  %s missing file: rc %d, defaults kept\n", missing_ok ? "ok  " : "FAIL", rc);
  if (!missing_ok) (*fails)++;

  /* The panel itself: drawn, gated, and sized by the ini. An enum, not const int, so the
     readback buffers can be static. */
  enum { PW = 1280, PH = 900 };   /* tall enough that the widget panel fits between the card and the hints */
  framebuffer fb;
  if (fb_create(&fb, PW, PH, err, sizeof err) != 0) {
    printf("  FAIL settings framebuffer: %s\n", err);
    (*fails)++;
    return;
  }
  static unsigned char pxv[PW * PH * 4];
  static unsigned char pxh[PW * PH * 4];
  static unsigned char pxbase[PW * PH * 4];
  settings_default(&a->set);
  apply_settings(a);
  camera_reset(&a->cam, a->mesh.fit);
  a->hud_overlay = 0;
  draw_scene(a, &fb);              /* the scene with no HUD: the baseline for pixel diffs */
  a->hud_overlay = 1;
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
  const int rect_ok = hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &rx0, &ry0, &rx1, &ry1) && rx1 > rx0;
  if (!rect_ok) {
    printf("  FAIL settings panel rect unknown - ImGui drew nothing\n");
    (*fails)++;
    fb_free(&fb);
    return;
  }
  const int pinned = (rx1 - rx0) == (int)a->set.settings_w;
  printf("  %s panel drawn at (%d, %d) %d px wide, ini asks for %.0f px\n",
         pinned ? "ok  " : "FAIL", rx0, ry0, rx1 - rx0, a->set.settings_w);
  if (!pinned) (*fails)++;

  /* The widget panel must not sit on top of the info panels: rect overlap in pixels is the
     numeric version of "the three panels are disjoint". */
  int px0, py0, px1, py1, qx0, qy0, qx1, qy1;
  hud_rect(&a->hudl, HUD_PANEL_STATS, &px0, &py0, &px1, &py1);
  hud_rect(&a->hudl, HUD_PANEL_HELP, &qx0, &qy0, &qx1, &qy1);
  long pover = 0, qover = 0;
  if (px1 > px0 && py1 > py0 && rx1 > rx0 && ry1 > ry0) {
    const int ox = rx1 < px1 ? rx1 - px0 : px1 - rx0;
    const int oy = ry1 < py1 ? ry1 - py0 : py1 - ry0;
    if (ox > 0 && oy > 0) pover = (long)ox * oy;
  }
  if (qx1 > qx0 && qy1 > qy0 && rx1 > rx0 && ry1 > ry0) {
    const int ox = rx1 < qx1 ? rx1 - qx0 : qx1 - rx0;
    const int oy = ry1 < qy1 ? ry1 - qy0 : qy1 - ry0;
    if (ox > 0 && oy > 0) qover = (long)ox * oy;
  }
  if (pover != 0 || qover != 0) {
    printf("  FAIL the widget panel overlaps the info panels: stats by %ld px, hints by %ld px\n",
           pover, qover);
    (*fails)++;
  } else {
    printf("  ok   panels are disjoint: stats %d..%d x %d..%d, settings %d..%d x %d..%d, "
           "hints %d..%d x %d..%d, overlap 0 px\n",
           px0, px1, py0, py1, rx0, rx1, ry0, ry1, qx0, qx1, qy0, qy1);
  }
  if (fb_read_rgba(&fb, pxv, err, sizeof err) != 0) {
    printf("  FAIL settings readback: %s\n", err);
    (*fails)++;
    fb_free(&fb);
    return;
  }

  /* Pixels of the panel: ink inside its rect, contrast against the panel background, and no ink
     anywhere else in the frame. Thresholds are relative so another GPU/AA survives them. */
  a->hud_overlay = 0;
  draw_scene(a, &fb);
  if (fb_read_rgba(&fb, pxbase, err, sizeof err) != 0) {
    printf("  FAIL settings baseline readback: %s\n", err);
    (*fails)++;
    a->hud_overlay = 1;
    fb_free(&fb);
    return;
  }
  a->hud_overlay = 1;
  float bgc[4], txtc[4];
  hud_colors(&a->hudl, bgc, txtc);
  const double bg_lum = lum3((unsigned)(bgc[0] * 255.0f), (unsigned)(bgc[1] * 255.0f),
                             (unsigned)(bgc[2] * 255.0f));
  const int srect[4] = { rx0, ry0, rx1, ry1 };
  panel_m pm;
  measure_panel(pxbase, pxv, PW, PH, srect, bgc, bg_lum, &pm);
  /* Text glyphs are the bright ink; widget chrome (frames, grabbers) sits mid-tone and must not
     be counted as text, or the contrast of the labels would read as too low. Thresholds are
     relative to the palette the HUD asked for. */
  const double txt_lum = lum3((unsigned)(txtc[0] * 255.0f), (unsigned)(txtc[1] * 255.0f),
                              (unsigned)(txtc[2] * 255.0f));
  long glyph = 0, chrome = 0, plain = 0;
  double gsum = 0.0, csum = 0.0, psum = 0.0;
  int gx0 = srect[2], gx1 = srect[0], gy0 = srect[3], gy1 = srect[1];
  for (int y = srect[1]; y < srect[3]; y++)
    for (int x = srect[0]; x < srect[2]; x++) {
      const unsigned char* p = pxbase + ((size_t)y * PW + x) * 4;
      const unsigned char* q = pxv + ((size_t)y * PW + x) * 4;
      const double scene = lum3(p[2], p[1], p[0]);
      const double panel_ref = bgc[3] * bg_lum + (1.0 - bgc[3]) * scene;
      const double l = lum3(q[2], q[1], q[0]);
      if (l > panel_ref + 0.60 * txt_lum) { glyph++; gsum += l; if (x < gx0) gx0 = x; if (x > gx1) gx1 = x; if (y < gy0) gy0 = y; if (y > gy1) gy1 = y; }
      else if (l > panel_ref + 25.0) { chrome++; csum += l; }
      else { plain++; psum += l; }
    }
  const double glyph_lum = glyph ? gsum / (double)glyph : 0.0;
  const double panel_lum = plain ? psum / (double)plain : 0.0;
  const double gcontrast = wcag_ratio(glyph_lum, panel_lum);
  const double area = (double)(rx1 - rx0) * (double)(ry1 - ry0);
  const double gcov = area > 0 ? 100.0 * (double)glyph / area : 0.0;
  const int ink_ok = glyph > 400 && gcov > 0.5 && gcontrast >= 4.5 &&
                     gx0 >= rx0 && gx1 < rx1 && gy0 >= ry0 && gy1 < ry1 &&
                     (double)(gy1 - gy0) >= 0.5 * (double)(ry1 - ry0);
  printf("  %s settings glyph ink %ld px of %d x %d (%.2f%%), chrome %ld px (%.2f%%, lum %.1f), "
         "contrast %.2f:1 (>= 4.5:1), ink box %d..%d x %d..%d covers %.0f%% of the panel height\n",
         ink_ok ? "ok  " : "FAIL", glyph, rx1 - rx0, ry1 - ry0, gcov, chrome,
         100.0 * (double)chrome / area, csum / (double)(chrome ? chrome : 1), gcontrast,
         gx0, gx1, gy0, gy1, 100.0 * (double)(gy1 - gy0) / (double)(ry1 - ry0));
  if (!ink_ok) (*fails)++;

  int st[4] = { 0, 0, 0, 0 }, hp[4] = { 0, 0, 0, 0 };
  hud_rect(&a->hudl, HUD_PANEL_STATS, &st[0], &st[1], &st[2], &st[3]);
  hud_rect(&a->hudl, HUD_PANEL_HELP, &hp[0], &hp[1], &hp[2], &hp[3]);
  const long leak = outside_panels_diff(pxbase, pxv, PW, PH, st, hp, srect, 8);
  printf("  %s hud ink confined to the three panel rects (%ld pixels outside)\n",
         leak == 0 ? "ok  " : "FAIL", leak);
  if (leak != 0) (*fails)++;

  const float room = hud_settings_room(&a->hudl);
  const int fits = (ry1 - ry0) <= (int)(room + 2.0f);
  printf("  %s panel fits the room under the card: rect %d px tall, room %.0f px (no scroll, no clip)\n",
         fits ? "ok  " : "FAIL", ry1 - ry0, room);
  if (!fits) (*fails)++;

  /* The panel inside the interactive window size the viewer opens with (800 x 600): it must be
     present and bounded by the room, so a taller widget list scrolls inside the panel instead of
     reaching past the hints. */
  framebuffer mid;
  memset(&mid, 0, sizeof mid);
  if (fb_create(&mid, 800, 600, err, sizeof err) != 0) {
    printf("  FAIL 800x600 frame: %s\n", err);
    (*fails)++;
  } else {
    draw_scene(a, &mid);
    int mx0 = 0, my0 = 0, mx1 = 0, my1 = 0;
    if (!hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &mx0, &my0, &mx1, &my1) || mx1 <= mx0) {
      printf("  FAIL the widget panel is missing at the default window size 800 x 600\n");
      (*fails)++;
    } else {
      const float mroom = hud_settings_room(&a->hudl);
      const int bounded = (my1 - my0) <= (int)(mroom + 2.0f) && my1 <= 600 && mx1 <= 800;
      printf("  %s panel at the default window 800 x 600: rect %d x %d px inside room %.0f px "
             "(rows below the fold reached by the scrollbar, hints stay clear)\n",
             bounded ? "ok  " : "FAIL", mx1 - mx0, my1 - my0, mroom);
      if (!bounded) (*fails)++;
    }
    fb_free(&mid);
  }

  /* Dump the HUD frame so the layout can be looked at, not only measured. */
  if (img_write_bmp("build/settings_panel.bmp", PW, PH, pxv, err, sizeof err) != 0) {
    printf("  FAIL settings panel dump: %s\n", err);
    (*fails)++;
  } else {
    printf("  ok   hud frame dumped to build/settings_panel.bmp (%d x %d, scene = %d triangles)\n",
           PW, PH, a->cpu.triangles);
  }

  /* Mouse ownership: inside the panel the widgets own the cursor, outside they do not. */
  const int inside = mouse_in_settings(a, (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0);
  const int outside = mouse_in_settings(a, PW - 2.0, PH - 2.0);
  const int gate_ok = inside == 1 && outside == 0;
  printf("  %s mouse gate: inside %d, outside %d\n", gate_ok ? "ok  " : "FAIL", inside, outside);
  if (!gate_ok) (*fails)++;

  /* The wheel over the panel scrolls the widget list. A wheel step only does something when the
     list is taller than the room, so the fixture has to overflow: at the readable default font
     (13.26 px) the 1280x900 room is taller than the list, so this step runs in a 1280x520 frame.
     An offscreen framebuffer has no real pointer, so the cursor and the wheel are injected into
     ImGui's event queue (hud_warp_mouse). Before this existed the panel carried NoScrollWithMouse
     and the wheel did nothing there. The wheel itself is not measured here - scroll_y is, which is
     the state the user sees. */
  const float w_saved = a->set.settings_w;
  hud_set_panel_width(&a->hudl, 170.0f);
  draw_scene(a, &fb);
  int nx0 = 0, ny0 = 0, nx1 = 0, ny1 = 0;
  hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &nx0, &ny0, &nx1, &ny1);

  framebuffer tight;
  memset(&tight, 0, sizeof tight);
  if (fb_create(&tight, PW, 520, err, sizeof err) != 0) {
    printf("  FAIL wheel fixture frame 1280x520: %s\n", err);
    (*fails)++;
  } else {
    draw_scene(a, &tight);
    int wx0 = 0, wy0 = 0, wx1 = 0, wy1 = 0;
    hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &wx0, &wy0, &wx1, &wy1);
    const float wroom = hud_settings_room(&a->hudl);
    /* The panel height is clamped to the room, so overflow is not visible in the rect - the
       scroll step below is what proves the list is taller than what is shown. */
    printf("  wheel fixture 1280 x 520: panel rect %d x %d px clamped to room %.0f px\n",
           wx1 - wx0, wy1 - wy0, (double)wroom);

    hud_warp_mouse(&a->hudl, (wx0 + wx1) * 0.5, (wy0 + wy1) * 0.5, 0, 0.0f);
    draw_scene(a, &tight);
    const float scroll_top = hud_settings_scroll(&a->hudl);
    hud_warp_mouse(&a->hudl, (wx0 + wx1) * 0.5, (wy0 + wy1) * 0.5, 0, -1.0f);
    draw_scene(a, &tight);
    const float scroll_down = hud_settings_scroll(&a->hudl);
    const int scrolled = scroll_down > scroll_top + 5.0f;
    printf("  %s wheel over the panel scrolls the widget list: scroll_y %.1f -> %.1f px "
           "(rect %d x %d, room %.0f px)\n", scrolled ? "ok  " : "FAIL",
           (double)scroll_top, (double)scroll_down, wx1 - wx0, wy1 - wy0, (double)wroom);
    if (!scrolled) (*fails)++;
    fb_free(&tight);
  }

  /* A drag owns the mouse: while a HUD gesture is held the camera must stay out of it. The title
     bar is grabbed first - its geometry is known and it is the window's own hit area - and the
     panel follows the cursor, so ownership here is measured as the active gesture (busy), which
     is the state main.c gates on. */
  hud_warp_mouse(&a->hudl, nx0 + 30.0, ny0 + 8.0, 1, 0.0f);
  draw_scene(a, &fb);
  hud_warp_mouse(&a->hudl, nx0 + 30.0, ny0 + 48.0, 1, 0.0f);
  draw_scene(a, &fb);
  int my0 = 0, my1 = 0, mx0 = 0, mx1 = 0;
  hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &mx0, &my0, &mx1, &my1);
  const int moved_px = my0 - ny0;
  const int held_busy = hud_mouse_busy(&a->hudl);
  hud_warp_mouse(&a->hudl, nx0 + 30.0, ny0 + 48.0, 0, 0.0f);
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  const int freed_busy = hud_mouse_busy(&a->hudl);
  const int drag_ok = moved_px >= 30 && moved_px <= 50 && held_busy == 1 && freed_busy == 0;
  printf("  %s panel moves by its title bar: rect y %d -> %d (%d px for a 40 px drag), "
         "gesture held busy %d, after release busy %d\n", drag_ok ? "ok  " : "FAIL",
         ny0, my0, moved_px, held_busy, freed_busy);
  if (!drag_ok) (*fails)++;

  /* The case the owner hit: a widget drag whose cursor ends up outside the window. The scrollbar
     is the deterministic target for it - unlike the title bar it does not drag the window along,
     so the cursor really leaves the rect and the rect-only gate alone would hand the gesture to
     the orbit. */
  hud_warp_mouse(&a->hudl, mx1 - 6.0, my0 + 60.0, 1, 0.0f);
  draw_scene(a, &fb);
  hud_warp_mouse(&a->hudl, (double)PW - 1.0, (double)PH - 1.0, 1, 0.0f);
  draw_scene(a, &fb);
  const int out_busy = hud_mouse_busy(&a->hudl);
  const int out_gate = mouse_in_settings(a, (double)PW - 1.0, (double)PH - 1.0);
  hud_warp_mouse(&a->hudl, (double)PW - 1.0, (double)PH - 1.0, 0, 0.0f);
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  const int out_freed = hud_mouse_busy(&a->hudl);
  const int own_ok = out_busy == 1 && out_gate == 0 && out_freed == 0;
  printf("  %s scrollbar drag outside the panel keeps the camera out: held busy %d gate %d, "
         "after release busy %d (rect %d..%d x %d..%d, cursor %d x %d)\n",
         own_ok ? "ok  " : "FAIL", out_busy, out_gate, out_freed,
         mx0, mx1, my0, my1, PW - 1, PH - 1);
  if (!own_ok) (*fails)++;

  /* Put the panel back where the layout placed it and hand the mouse back to the GLFW backend,
     so the checks below measure the normal layout again. */
  hud_warp_mouse(&a->hudl, -1.0, -1.0, 0, 0.0f);
  draw_scene(a, &fb);
  hud_settings_place(&a->hudl, (float)rx0, (float)ry0, w_saved);
  draw_scene(a, &fb);

  /* The title-bar triangle folds the window: Begin then returns false while the window stays
     open, and ImGui still requires the matching End() (deps/imgui/imgui/imgui.h:7611) - without
     it the process aborts inside ImGui ("Missing End()",
     deps/imgui/imgui/imgui.cpp:11325), which is exactly the crash the collapse button caused.
     A regression here fails the whole run (an abort is a non-zero exit), and the tab geometry is
     what the mouse gate has to keep away from the camera. */
  hud_settings_collapse(&a->hudl, 1);
  draw_scene(a, &fb);
  int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
  const int collapsed = hud_settings_collapsed(&a->hudl);
  if (!collapsed || !hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &cx0, &cy0, &cx1, &cy1) || cx1 <= cx0) {
    printf("  FAIL folded panel reports no tab (collapsed %d, rect %d x %d)\n", collapsed,
           cx1 - cx0, cy1 - cy0);
    (*fails)++;
  } else {
    /* ImGui sizes a collapsed window to its title bar (deps/imgui/imgui/imgui.cpp:7956): same
       top-left corner, same width, a fraction of the height - and the mouse still belongs to the
       tab, so dragging over it does not orbit the camera. */
    const int tab_gate = mouse_in_settings(a, (double)(cx0 + cx1) / 2.0, (double)(cy0 + cy1) / 2.0);
    const int tab_ok = cy1 > cy0 && cy1 - cy0 <= (ry1 - ry0) / 4 && cx0 == rx0 && cy0 == ry0 &&
                       cx1 == rx1 && cy1 <= PH && tab_gate == 1;
    printf("  %s fold the panel: tab %d x %d px at (%d,%d) where the panel was %d x %d px, "
           "mouse over the tab %d (End() ran - the frame did not abort)\n",
           tab_ok ? "ok  " : "FAIL", cx1 - cx0, cy1 - cy0, cx0, cy0, rx1 - rx0, ry1 - ry0, tab_gate);
    if (!tab_ok) (*fails)++;
  }

  /* Unfolding must bring the widget list back at the same rect, so a fold/unfold cycle leaves the
     layout exactly where the pixel checks above measured it. */
  hud_settings_collapse(&a->hudl, 0);
  draw_scene(a, &fb);
  int ex0 = 0, ey0 = 0, ex1 = 0, ey1 = 0;
  hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &ex0, &ey0, &ex1, &ey1);
  const int unfold_ok = !hud_settings_collapsed(&a->hudl) && ex0 == rx0 && ey0 == ry0 &&
                        abs(ex1 - rx1) <= 2 && abs(ey1 - ry1) <= 2;
  printf("  %s unfold again: rect %d x %d px, was %d x %d px before the fold\n",
         unfold_ok ? "ok  " : "FAIL", ex1 - ex0, ey1 - ey0, rx1 - rx0, ry1 - ry0);
  if (!unfold_ok) (*fails)++;

  /* Hide it through the ini value and re-render: the panel area must become bare background. */
  a->set.hud_settings_visible = 0;
  apply_settings(a);
  draw_scene(a, &fb);
  draw_scene(a, &fb);
  if (fb_read_rgba(&fb, pxh, err, sizeof err) != 0) {
    printf("  FAIL hidden settings readback: %s\n", err);
    (*fails)++;
  } else {
    long changed = 0;
    for (int y = ry0; y < ry1; y++)
      for (int x = rx0; x < rx1; x++) {
        const int i = (y * PW + x) * 4;
        if (abs((int)pxv[i] - (int)pxh[i]) > 8 || abs((int)pxv[i + 1] - (int)pxh[i + 1]) > 8 ||
            abs((int)pxv[i + 2] - (int)pxh[i + 2]) > 8) changed++;
      }
    const double frac = (double)changed / (double)((rx1 - rx0) * (ry1 - ry0));
    const int content_ok = changed > 200 && frac > 0.20 && !hud_rect(&a->hudl, HUD_PANEL_SETTINGS, &rx0, &ry0, &rx1, &ry1);
    printf("  %s hidden panel: %ld px of content in the old rect (%.1f%%), rect no longer reported\n",
           content_ok ? "ok  " : "FAIL", changed, frac * 100.0);
    if (!content_ok) (*fails)++;
  }
  a->set.hud_settings_visible = 1;
  apply_settings(a);
  a->hud_overlay = 0;
  settings_clear_dirty(&a->set);
  fb_free(&fb);
}

/* The draw target must follow the size in the same frame it changes: a cached size would leave
   black pixels wherever the window grew. Checked numerically - the newly exposed area of a grown
   target must be the background, never black. */
static void resize_selftest(app* a, int* fails) {
  printf("selftest: frame size follows the target immediately (resize repaint)\n");
  char err[256] = "";
  int win_w = 0, win_h = 0;
  glfwGetFramebufferSize(a->win, &win_w, &win_h);

  int saved_w = a->width, saved_h = a->height;
  a->width = 7; a->height = 3;              /* pretend the resize callback never fired */
  int changed = sync_framebuffer_size(a);
  int ok = changed == 1 && a->width == win_w && a->height == win_h;
  printf("  %s size re-read from the window: 7x3 -> %dx%d (changed=%d, window says %dx%d)\n",
         ok ? "ok  " : "FAIL", a->width, a->height, changed, win_w, win_h);
  if (!ok) (*fails)++;
  a->width = saved_w; a->height = saved_h;

  const int sw = 256, sh = 192, bw = 512, bh = 384;
  framebuffer small_fb, big_fb;
  memset(&small_fb, 0, sizeof small_fb);
  memset(&big_fb, 0, sizeof big_fb);
  if (fb_create(&small_fb, sw, sh, err, sizeof err) != 0 ||
      fb_create(&big_fb, bw, bh, err, sizeof err) != 0) {
    printf("  FAIL resize framebuffers: %s\n", err);
    (*fails)++;
    fb_free(&small_fb);
    fb_free(&big_fb);
    return;
  }

  static unsigned char px_small[256 * 192 * 4];
  static unsigned char px_big[512 * 384 * 4];
  camera_reset(&a->cam, a->mesh.fit);
  draw_scene(a, &small_fb);
  draw_scene(a, &big_fb);
  int rc = fb_read_rgba(&small_fb, px_small, err, sizeof px_small);
  rc |= fb_read_rgba(&big_fb, px_big, err, sizeof px_big);
  fb_free(&small_fb);
  fb_free(&big_fb);
  if (rc != 0) {
    printf("  FAIL resize readback: %s\n", err);
    (*fails)++;
    return;
  }

  /* Corner of the first target is the clear colour; the grown target must clear to the same
     colour everywhere, including the strip that did not exist before. */
  const unsigned char* bg = px_small;
  const int cx[4] = { 0, bw - 1, 0, bw - 1 };
  const int cy[4] = { 0, 0, bh - 1, bh - 1 };
  int corner_diff = 0;
  for (int i = 0; i < 4; i++) {
    const unsigned char* p = px_big + ((size_t)cy[i] * bw + cx[i]) * 4;
    for (int c = 0; c < 3; c++) {
      int d = (int)p[c] - (int)bg[c];
      if (d < 0) d = -d;
      if (d > corner_diff) corner_diff = d;
    }
  }
  long black_new = 0;
  for (int y = 0; y < bh; y++) {
    for (int x = 0; x < bw; x++) {
      if (x < sw && y < sh) continue;   /* only the newly exposed area */
      const unsigned char* p = px_big + ((size_t)y * bw + x) * 4;
      if (p[0] == 0 && p[1] == 0 && p[2] == 0) black_new++;
    }
  }
  double cov = img_changed_fraction(px_big, bw, bh, 8);
  int ok2 = corner_diff <= 12 && black_new == 0 && cov > 0.02;
  printf("  %s grown target %dx%d -> %dx%d: corner vs background max diff %d (budget 12), "
         "black pixels in the newly exposed area %ld, silhouette coverage %.2f%% (>2%%)\n",
         ok2 ? "ok  " : "FAIL", sw, sh, bw, bh, corner_diff, black_new, cov * 100.0);
  if (!ok2) (*fails)++;
}

/* The statistics card names the face a triangle came from and the authored vertex numbers it
   uses, so those numbers are checked against a file whose content is known exactly. The quad
   (4 corners) fans into 2 triangles; the second face is written with negative indices. */
static void identity_selftest(int* fails) {
  printf("selftest: triangle identity (face ordinal, vertex numbers, normals)\n");
  const char* src =
    "v -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\n"
    "v -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\n"
    "vn 0 0 1\nvn 0 1 0\nvn 0.7071 0 0.7071\n"
    "f 1//1 2//1 3//1 4//1\n"
    "f -3//2 -2//3 -1//2\n";
  obj_mesh m;
  char err[256] = "";
  if (obj_parse_string(src, strlen(src), "identity", &m, err, sizeof err) < 0) {
    printf("  FAIL identity fixture: %s\n", err);
    (*fails)++;
    return;
  }

  const int exp_face[3] = { 0, 0, 1 };
  const int exp_vert[9] = { 1, 2, 3, 1, 3, 4, 5, 6, 7 };
  int bad = 0;
  if (m.faces != 2 || m.triangles != 3 || m.vcount != 9) bad++;
  if (!m.face_ids || !m.vert_ids) {
    printf("  FAIL parser produced no identity arrays (face_ids %p, vert_ids %p)\n",
           (const void*)m.face_ids, (const void*)m.vert_ids);
    (*fails)++;
    obj_mesh_free(&m);
    return;
  }
  for (int t = 0; t < 3; t++) if (m.face_ids[t] != exp_face[t]) bad++;
  for (int i = 0; i < 9; i++) if (m.vert_ids[i] != exp_vert[i]) bad++;
  printf("  ok   faces %d, triangles %d, face ids %d,%d,%d, vertex ids %d,%d,%d / %d,%d,%d / %d,%d,%d\n",
         m.faces, m.triangles, m.face_ids[0], m.face_ids[1], m.face_ids[2],
         m.vert_ids[0], m.vert_ids[1], m.vert_ids[2], m.vert_ids[3], m.vert_ids[4],
         m.vert_ids[5], m.vert_ids[6], m.vert_ids[7], m.vert_ids[8]);
  if (bad) {
    printf("  FAIL identity mismatch: expected 2 faces / 3 triangles, faces 0,0,1, verts "
           "1,2,3 1,3,4 5,6,7\n");
    (*fails)++;
  }

  /* Face normal of the quad, and the authored normals behind it. */
  float fn[3];
  double nlen = obj_face_normal(&m, 0, fn);
  double dot = fn[0] * 0.0 + fn[1] * 0.0 + fn[2] * 1.0;
  printf("  ok   quad face normal (%.3f, %.3f, %.3f), length %.3f, dot with +z %.3f\n",
         fn[0], fn[1], fn[2], nlen, dot);
  if (fabs(nlen - 1.0) > 0.01 || dot < 0.999) {
    printf("  FAIL quad face normal is not +z: (%.3f, %.3f, %.3f)\n", fn[0], fn[1], fn[2]);
    (*fails)++;
  }

  float dev = 0.0f;
  int authored = obj_normal_deviation_deg(&m, 0, &dev);
  printf("  ok   quad normals: authored %d, deviation %.2f deg\n", authored, (double)dev);
  if (!authored || dev > 1.0f) {
    printf("  FAIL quad vertex normals deviate %.2f deg from the face normal (expected 0)\n", (double)dev);
    (*fails)++;
  }

  float dev2 = 0.0f;
  int auth2 = obj_normal_deviation_deg(&m, 2, &dev2);
  float vn[3][3];
  int vauth = obj_vertex_normals(&m, 2, vn);
  printf("  ok   top face normals: authored %d, vertex n0 (%.3f, %.3f, %.3f), deviation %.2f deg\n",
         vauth, vn[0][0], vn[0][1], vn[0][2], (double)dev2);
  if (!auth2 || !vauth || fabs((double)dev2 - 90.0) > 1.0) {
    printf("  FAIL top face deviation %.2f deg, expected 90 (vn of corner 5 is +y)\n", (double)dev2);
    (*fails)++;
  }

  /* A mesh without vn: normals are computed, so the deviation metric must report "not authored". */
  obj_mesh nov;
  char err3[256] = "";
  if (obj_parse_file("assets/nov.obj", &nov, err3, sizeof err3) == 0) {
    float dnov = 1.0f;
    int a3 = obj_normal_deviation_deg(&nov, 0, &dnov);
    float nvn[3][3];
    int v3 = obj_vertex_normals(&nov, 0, nvn);
    printf("  ok   nov.obj (no vn): authored %d, deviation %.2f deg, vertex normals (%.3f, %.3f, %.3f)\n",
           a3, (double)dnov, nvn[0][0], nvn[0][1], nvn[0][2]);
    if (a3 != 0 || v3 != 0 || dnov != 0.0f) {
      printf("  FAIL nov.obj claims authored normals (%d) or deviation %.2f deg\n", a3, (double)dnov);
      (*fails)++;
    }
    obj_mesh_free(&nov);
    if (nov.face_ids || nov.vert_ids || nov.v) {
      printf("  FAIL obj_mesh_free left identity arrays behind\n");
      (*fails)++;
    }
  } else {
    printf("  FAIL cannot load assets/nov.obj: %s\n", err3);
    (*fails)++;
  }

  /* Bad triangle index is an error path, not a silent zero normal. */
  float fbad[3] = { 9, 9, 9 };
  if (obj_face_normal(&m, -1, fbad) != 0 || obj_normal_deviation_deg(&m, 99, &dev) != 0) {
    printf("  FAIL out-of-range triangle accepted silently\n");
    (*fails)++;
  } else {
    printf("  ok   out-of-range triangle rejected (face normal and deviation both 0)\n");
  }

  obj_mesh_free(&m);
  if (m.face_ids || m.vert_ids || m.v) {
    printf("  FAIL obj_mesh_free left identity arrays behind\n");
    (*fails)++;
  }
}

/* Tab stepping: the walk must loop, and backward from the first face must land on the last.
   Pure function, so this is checked without a window. */
static void face_step_selftest(int* fails) {
  struct { int cur, dir, total, want; } cases[] = {
    { -1, 1, 12, 0 },   /* nothing pinned: forward lands on the first face */
    { 0, 1, 12, 1 },
    { 11, 1, 12, 0 },   /* forward past the last face wraps to the first */
    { 0, -1, 12, 11 },  /* backward past the first wraps to the last */
    { 5, -1, 12, 4 },
    { 3, 1, 0, -1 },    /* no model: nothing to step through */
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    int got = face_step(cases[i].cur, cases[i].total, cases[i].dir);
    if (got != cases[i].want) {
      printf("  FAIL face_step from %d %s over %d faces gives %d, expected %d\n",
             cases[i].cur, cases[i].dir < 0 ? "backward" : "forward", cases[i].total, got, cases[i].want);
      (*fails)++;
    }
  }
  printf("  ok   Tab stepping loops: -1->0, 11->0 forward, 0->11 backward\n");

  /* The stepped triangle must still be the file's own face: stepping forward from the first
     triangle of quad_vvn.obj lands on face 1, the second authored face. */
  obj_mesh m;
  char err[256] = "";
  if (obj_parse_file("assets/quad_vvn.obj", &m, err, sizeof err) < 0) {
    printf("  FAIL stepping fixture: %s\n", err);
    (*fails)++;
    return;
  }
  int step = face_step(0, m.triangles, 1);
  int face = -1, v0 = -1, v1 = -1, v2 = -1;
  char ident_err[256] = "";
  if (obj_triangle_identity(&m, step, &face, &v0, &v1, &v2, ident_err, sizeof ident_err) < 0) {
    printf("  FAIL stepped triangle %d has no identity: %s\n", step, ident_err);
    (*fails)++;
  } else {
    printf("  ok   Tab from triangle 0 -> triangle %d, face %d, vertices %d,%d,%d\n", step, face, v0, v1, v2);
    if (face != 1) {
      printf("  FAIL stepping gives face %d, expected face 1\n", face);
      (*fails)++;
    }
  }
  obj_mesh_free(&m);
}

static int run_selftest(app* a) {
  int fails = 0;
  printf("selftest: OBJ parser\n");

  expect_mesh("assets/cube.obj", 12, OBJ_HAS_NORMALS | OBJ_HAS_TEXCOORDS, &fails);
  expect_mesh("assets/quad_vvn.obj", 2, OBJ_HAS_NORMALS, &fails);
  expect_mesh("assets/nov.obj", 2, OBJ_HAS_NORMALS, &fails);   /* normals computed from faces */
  expect_parse_failure("assets/bad.obj", &fails);
  expect_parse_failure("assets/bad_index.obj", &fails);
  expect_mesh("assets/utah_teapot.obj", 1250, OBJ_HAS_NORMALS | OBJ_HAS_TEXCOORDS, &fails);
  identity_selftest(&fails);
  face_step_selftest(&fails);

  printf("selftest: BMP orientation\n");
  expect_bmp_orientation(&fails);

  /* The app's own model: draw_scene, picking and the HUD card all read it, so load it once
     here instead of leaving a->cpu empty for the phases below. */
  printf("selftest: app model for HUD and picking\n");
  if (load_model(a, "assets/cube.obj") < 0) {
    printf("  FAIL cannot load assets/cube.obj into the app\n");
    fails++;
  }

  printf("selftest: shader + offscreen pixels\n");
  char err[256] = "";
  if (renderer_init(&a->rend, VERT_SHADER, FRAG_SHADER, err, sizeof err) < 0) {
    printf("  FAIL shaders: %s\n", err);
    fails++;
  } else {
    obj_mesh m;
    char err2[256] = "";
    if (obj_parse_file("assets/cube.obj", &m, err2, sizeof err2) == 0 &&
        mesh_upload(&m, &a->mesh, err2, sizeof err2) == 0) {
      camera_reset(&a->cam, a->mesh.fit);
      framebuffer fb;
      if (fb_create(&fb, 128, 128, err, sizeof err) == 0) {
        draw_scene(a, &fb);
        unsigned char px[128 * 128 * 4];
        if (fb_read_rgba(&fb, px, err, sizeof err) == 0) {
          double frac = img_changed_fraction(px, 128, 128, 8);
          int ok = frac > 0.02;
          printf("  %s rendered %d%% of 128x128\n", ok ? "ok  " : "FAIL", (int)(frac * 100.0));
          if (!ok) fails++;

          /* The same model through perspective, a narrow and a wide fov, and
             orthographic projection must produce visibly different silhouettes. The model is
             turned off the axis-aligned default view first: facing a cube head-on makes
             perspective and orthographic silhouettes nearly identical, which would prove
             nothing about depth. */
          double cov[4] = { 0, 0, 0, 0 };
          const float fovs[4] = { 45.0f, 15.0f, 80.0f, 45.0f };
          int pixels_ok = 1;
          for (int i = 0; i < 4; i++) {
            camera_reset(&a->cam, a->mesh.fit);
            camera_set_fov(&a->cam, fovs[i]);
            a->cam.ortho = (i == 3);
            camera_orbit(&a->cam, 110.0f, 60.0f);   /* tilt: near faces get closer, far ones shrink */
            draw_scene(a, &fb);
            if (fb_read_rgba(&fb, px, err, sizeof err) != 0) { pixels_ok = 0; break; }
            cov[i] = img_changed_fraction(px, 128, 128, 8);
          }
          a->cam.ortho = 0;
          if (!pixels_ok) {
            printf("  FAIL projection renders: %s\n", err);
            fails++;
          } else {
            int all_visible = cov[0] > 0.02 && cov[1] > 0.02 && cov[2] > 0.02 && cov[3] > 0.02;
            printf("  %s coverage: persp45 %.1f%% | fov15 %.1f%% | fov80 %.1f%% | ortho %.1f%%\n",
                   all_visible ? "ok  " : "FAIL", cov[0] * 100.0, cov[1] * 100.0, cov[2] * 100.0, cov[3] * 100.0);
            if (!all_visible) fails++;

            int wide = fabs(cov[2] - cov[1]) > 0.01;
            printf("  %s wide fov differs from narrow by %.1f%% (> 1%%)\n", wide ? "ok  " : "FAIL",
                   fabs(cov[2] - cov[1]) * 100.0);
            if (!wide) fails++;

            int ortho_diff = fabs(cov[3] - cov[0]) > 0.01;
            printf("  %s ortho differs from perspective by %.1f%% (> 1%%)\n", ortho_diff ? "ok  " : "FAIL",
                   fabs(cov[3] - cov[0]) * 100.0);
            if (!ortho_diff) fails++;

            /* Outline pass: the picked triangle is drawn as three flat-coloured
               lines, so a pick is visible without reading the console. */
            int hl_px = 0, no_hl_px = 0;
            renderer_set_highlight(&a->rend, &m, 0);
            draw_scene(a, &fb);
            if (fb_read_rgba(&fb, px, err, sizeof err) == 0) {
              const int er = (int)(a->rend.hl_rgb[0] * 255.0f + 0.5f);
              const int eg = (int)(a->rend.hl_rgb[1] * 255.0f + 0.5f);
              const int eb = (int)(a->rend.hl_rgb[2] * 255.0f + 0.5f);
              for (int i = 0; i < 128 * 128; i++)
                if (abs((int)px[i * 4] - er) <= 20 && abs((int)px[i * 4 + 1] - eg) <= 20 &&
                    abs((int)px[i * 4 + 2] - eb) <= 20) hl_px++;
              renderer_set_highlight(&a->rend, &m, -1);
              draw_scene(a, &fb);
              if (fb_read_rgba(&fb, px, err, sizeof err) == 0) {
                for (int i = 0; i < 128 * 128; i++)
                  if (abs((int)px[i * 4] - er) <= 20 && abs((int)px[i * 4 + 1] - eg) <= 20 &&
                      abs((int)px[i * 4 + 2] - eb) <= 20) no_hl_px++;
              }
            }
            int outline_ok = hl_px >= 20 && no_hl_px == 0;
            printf("  %s picked outline: %d px drawn, %d px after clearing\n",
                   outline_ok ? "ok  " : "FAIL", hl_px, no_hl_px);
            if (!outline_ok) fails++;

            /* outline_visible=false hides the marker but keeps the pin: the selection must stay
               alive while nothing of it is drawn. */
            renderer_set_highlight(&a->rend, &m, 0);
            renderer_set_outline_visible(&a->rend, 0);
            draw_scene(a, &fb);
            int off_px = 0;
            if (fb_read_rgba(&fb, px, err, sizeof err) == 0) {
              const int er = (int)(a->rend.hl_rgb[0] * 255.0f + 0.5f);
              const int eg = (int)(a->rend.hl_rgb[1] * 255.0f + 0.5f);
              const int eb = (int)(a->rend.hl_rgb[2] * 255.0f + 0.5f);
              for (int i = 0; i < 128 * 128; i++)
                if (abs((int)px[i * 4] - er) <= 20 && abs((int)px[i * 4 + 1] - eg) <= 20 &&
                    abs((int)px[i * 4 + 2] - eb) <= 20) off_px++;
            }
            const int hidden_ok = off_px == 0 && renderer_highlight(&a->rend) == 0;
            printf("  %s outline hidden: %d marker px while triangle %d stays pinned\n",
                   hidden_ok ? "ok  " : "FAIL", off_px, renderer_highlight(&a->rend));
            if (!hidden_ok) fails++;
            renderer_set_outline_visible(&a->rend, 1);
          }
        } else {
          printf("  FAIL read pixels: %s\n", err);
          fails++;
        }
        fb_free(&fb);
      } else {
        printf("  FAIL fbo: %s\n", err);
        fails++;
      }
      obj_mesh_free(&m);
    } else {
      printf("  FAIL cube upload: %s\n", err);
      fails++;
    }
  }

  projection_selftest(&fails);
  orbit_drag_selftest(&fails);
  mouse_pick_selftest(&fails);
  hud_selftest(a, &fails);
  settings_selftest(a, &fails);
  resize_selftest(a, &fails);

  printf("selftest: %s\n", fails == 0 ? "PASS" : "FAILED");
  return fails == 0 ? 0 : 1;
}

static const char* pick_default_model(void) {
  static const char* candidates[] = { "assets/utah_teapot.obj", "assets/cube.obj" };
  for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
    FILE* f = fopen(candidates[i], "rb");
    if (f) { fclose(f); return candidates[i]; }
  }
  return NULL;
}

static void usage(void) {
  printf("usage:\n");
  printf("  viewer                      open a window with the default model\n");
  printf("  viewer <model.obj>          open a window with that model\n");
  printf("  viewer --render <in> <out.bmp> [w h]   headless render (default 800x600)\n");
  printf("  viewer --selftest           parser + shader/pixel checks\n");
  printf("  viewer --wire               start in wireframe\n");
  printf("  viewer --ortho              orthographic (parallel rays); perspective is the default\n");
  printf("  viewer --fov <deg>          viewing angle, %.0f-%.0f (default %.0f)\n", FOV_MIN, FOV_MAX, DEFAULT_FOV);
  printf("  viewer --yaw <deg> --pitch <deg>  camera angles in degrees (for reproducible --render shots)\n");
  printf("  viewer --idle-report <sec>   redraw with no input for N seconds, then print frames/fps\n");
  printf("window: wheel zoom | Ctrl+wheel or [ ] viewing angle | O perspective/ortho\n");
}

static int is_int(const char* s) {
  char* e = NULL;
  strtol(s, &e, 10);
  return *s != '\0' && *e == '\0';
}

int main(int argc, char** argv) {
  /* Unbuffered so the last line printed is still there if something crashes. */
  setvbuf(stdout, NULL, _IONBF, 0);

  const char* model = NULL;
  const char* out = NULL;
  const char* pos[4] = { 0, 0, 0, 0 };
  int npos = 0;
  int headless = 0, selftest = 0, wire = 0, w = 800, h = 600;
  float fov_req = DEFAULT_FOV;
  int fov_given = 0, ortho_req = 0;
  float yaw_req = 0.0f, pitch_req = 0.0f;
  int yaw_given = 0, pitch_given = 0;
  double idle_report = 0.0; /* --idle-report <sec>, interactive mode only */

  /* Flags may appear anywhere, so positional arguments are collected in order:
     model, out, then an optional width/height pair. */
  for (int i = 1; i < argc; i++) {
    const char* s = argv[i];
    if (strcmp(s, "--help") == 0 || strcmp(s, "-h") == 0) { usage(); return 0; }
    else if (strcmp(s, "--render") == 0) headless = 1; else if (strcmp(s, "--selftest") == 0) { selftest = 1; headless = 1; }
    else if (strcmp(s, "--wire") == 0) wire = 1;
    else if (strcmp(s, "--ortho") == 0) ortho_req = 1;
    else if (strcmp(s, "--yaw") == 0 || strcmp(s, "--pitch") == 0) {
      int is_yaw = s[2] == 'y';
      if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", s); usage(); return 2; }
      float v = (float)atof(argv[++i]);
      if (is_yaw) { yaw_req = v; yaw_given = 1; }
      else { pitch_req = v; pitch_given = 1; }
    }
    else if (strcmp(s, "--fov") == 0) {
      if (i + 1 >= argc) { fprintf(stderr, "--fov needs a value\n"); usage(); return 2; }
      float f = (float)atof(argv[++i]);
      if (!(f >= FOV_MIN && f <= FOV_MAX)) {
        fprintf(stderr, "--fov %.1f out of range (%.0f-%.0f)\n", f, FOV_MIN, FOV_MAX);
        return 2;
      }
      fov_req = f;
      fov_given = 1;
    }
    else if (strcmp(s, "--idle-report") == 0) {
      if (i + 1 >= argc) { fprintf(stderr, "--idle-report needs a value\n"); usage(); return 2; }
      double v = atof(argv[++i]);
      if (!(v > 0.0)) { fprintf(stderr, "--idle-report %.2f must be > 0\n", v); return 2; }
      idle_report = v;
    }
    else if (strcmp(s, "--size") == 0 && i + 2 < argc) { w = atoi(argv[++i]); h = atoi(argv[++i]); }
    else if (s[0] == '-') { fprintf(stderr, "unknown option '%s'\n", s); usage(); return 2; }
    else if (npos < 4) pos[npos++] = s;
    else { fprintf(stderr, "extra argument '%s'\n", s); return 2; }
  }

  if (npos > 0) model = pos[0];
  if (headless && !selftest) {
    if (!pos[0] || !pos[1]) { usage(); return 2; }
    model = pos[0];
    out = pos[1];
    if (npos >= 4 && is_int(pos[2]) && is_int(pos[3])) { w = atoi(pos[2]); h = atoi(pos[3]); }
    if (w < 1 || h < 1) { fprintf(stderr, "size %dx%d invalid\n", w, h); return 2; }
  }

  app a; memset(&a, 0, sizeof a);
  a.idle_report = idle_report;
  a.wireframe = wire;
  a.fov_req = fov_req;
  a.fov_given = fov_given;
  a.ortho_req = ortho_req;
  a.yaw_req = yaw_req; a.yaw_given = yaw_given;
  a.pitch_req = pitch_req; a.pitch_given = pitch_given;
  a.width = w; a.height = h;

  if (idle_report > 0.0 && (selftest || headless)) {
    fprintf(stderr, "--idle-report works only in the interactive window\n");
    return 2;
  }

  if (!model && !selftest) {
    model = pick_default_model();
    if (!model) { fprintf(stderr, "no model found; put an .obj next to the exe or pass one\n"); usage(); return 2; }
  }
  if (model) snprintf(a.model_path, sizeof a.model_path, "%s", model);

  if (init_gl(&a, headless ? 1 : w, headless ? 1 : h, headless ? 0 : 1) < 0) return 1;

  int rc = 0;
  if (selftest) rc = run_selftest(&a);
  else if (headless) rc = run_render(&a, model, out, w, h);
  else rc = run_interactive(&a, model);

  renderer_free(&a.rend);
  hud_shutdown(&a.hudl);
  mesh_free(&a.mesh);
  obj_mesh_free(&a.cpu);
  glfwDestroyWindow(a.win);
  glfwTerminate();

  /* -2 = bad arguments / unreadable model (exit 2), -1 = GL or IO failure (exit 1) */
  if (rc < 0) return (rc == -2) ? 2 : 1;
  return rc;
}
