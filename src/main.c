/* pi-gyro viewer: OBJ -> OpenGL 4.6 core, orbit camera, screenshot export, HUD overlay. */

#include "gl.h"
#include "vmath.h"
#include "obj.h"
#include "mesh.h"
#include "camera.h"
#include "render.h"
#include "img.h"
#include "hud.h"

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

static void on_resize(GLFWwindow* win, int w, int h) {
  app* a = of(win);
  a->width = w; a->height = h;
  a->dirty = 1;
}

static void on_scroll(GLFWwindow* win, double sx, double sy) {
  app* a = of(win);
  (void)sx;
  /* Ctrl+wheel changes the viewing angle instead of the distance: that is what
     makes perspective strong (wide fov, close eye) or flat (narrow fov, far eye). */
  if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
    camera_fov_step(&a->cam, (float)sy * 2.0f);
  else
    camera_zoom(&a->cam, (float)sy);
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

static void on_key(GLFWwindow* win, int key, int scancode, int action, int mods) {
  app* a = of(win);
  (void)scancode; (void)mods;
  if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

  if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, 1);
  else if (key == GLFW_KEY_W) { a->wireframe = !a->wireframe; a->dirty = 1; }
  else if (key == GLFW_KEY_O) { camera_toggle_proj(&a->cam); a->dirty = 1; }
  else if (key == GLFW_KEY_LEFT_BRACKET) { camera_fov_step(&a->cam, -5.0f); a->dirty = 1; }
  else if (key == GLFW_KEY_RIGHT_BRACKET) { camera_fov_step(&a->cam, 5.0f); a->dirty = 1; }
  else if (key == GLFW_KEY_R) {
    camera_reset(&a->cam, a->mesh.fit);
    apply_camera_req(&a->cam, a);
    a->dirty = 1;
  }
  else if (key == GLFW_KEY_S) snapshot(a, "screenshot.bmp");
  else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) { camera_zoom(&a->cam, 1.0f); a->dirty = 1; }
  else if (key == GLFW_KEY_MINUS) { camera_zoom(&a->cam, -1.0f); a->dirty = 1; }
}

static void handle_mouse(app* a) {
  double lx = 0, ly = 0;
  glfwGetCursorPos(a->win, &lx, &ly);
  int left = glfwGetMouseButton(a->win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  int right = glfwGetMouseButton(a->win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

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
    int sel = a->locked >= 0 ? a->locked : a->hover;
    hud_info info;
    info.projection = a->cam.ortho ? "ortho" : "persp";
    info.fov_deg = a->cam.fov_deg;
    info.cam_dist = a->cam.dist;
    info.triangles = a->cpu.triangles;
    info.selected = sel;
    info.pinned = a->locked >= 0;
    info.wireframe = a->wireframe;
    info.fps = a->fps;
    info.hovered = a->hover;
    info.pin_nx = info.pin_ny = info.pin_nz = 0.0f;
    info.pin_cx = info.pin_cy = info.pin_cz = 0.0f;
    if (sel >= 0 && sel < a->cpu.triangles && a->cpu.v) {
      const float* v = a->cpu.v + (size_t)sel * 24;   /* 3 vertices, stride 8 */
      info.pin_cx = (v[0] + v[8] + v[16]) / 3.0f;
      info.pin_cy = (v[1] + v[9] + v[17]) / 3.0f;
      info.pin_cz = (v[2] + v[10] + v[18]) / 3.0f;
      if (a->cpu.flags & OBJ_HAS_NORMALS) {
        info.pin_nx = v[3]; info.pin_ny = v[4]; info.pin_nz = v[5];
      } else {
        float e1[3] = { v[8] - v[0], v[9] - v[1], v[10] - v[2] };
        float e2[3] = { v[16] - v[0], v[17] - v[1], v[18] - v[2] };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                       e1[0] * e2[1] - e1[1] * e2[0] };
        float len = (float)sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-6f) { info.pin_nx = n[0] / len; info.pin_ny = n[1] / len; info.pin_nz = n[2] / len; }
      }
    }
    hud_frame(&a->hudl, w, h, &info);
    hud_render(&a->hudl);
  }

  if (fb) my_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int load_model(app* a, const char* path) {
  obj_mesh_free(&a->cpu);
  memset(&a->cpu, 0, sizeof a->cpu);
  char err[256] = "";
  if (obj_parse_file(path, &a->cpu, err, sizeof err) < 0) {
    fprintf(stderr, "cannot load '%s': %s\n", path, err);
    return -1;
  }
  printf("model %s: %d triangles, %d vertices, %s%s\n", path, a->cpu.triangles, a->cpu.vcount,
         (a->cpu.flags & OBJ_HAS_NORMALS) ? "normals" : "computed normals",
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
  glfwSetWindowUserPointer(a->win, a);
  glfwSetFramebufferSizeCallback(a->win, on_resize);
  glfwSetScrollCallback(a->win, on_scroll);
  glfwSetKeyCallback(a->win, on_key);

  char err[256] = "";
  if (renderer_init(&a->rend, VERT_SHADER, FRAG_SHADER, err, sizeof err) < 0) {
    fprintf(stderr, "shader error: %s\n", err);
    return -1;
  }
  if (load_model(a, model_path) < 0) return -2;

  printf("controls: LMB rotate | RMB pan | wheel zoom | Ctrl+wheel fov | [ ] fov | O ortho/perspective\n");
  printf("          W wire | R reset | S screenshot | hover outlines a triangle, click pins it | Esc quit\n");
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

    draw_scene(a, NULL);
    if (a->dirty) { update_title(a); a->dirty = 0; }
    glfwSwapBuffers(a->win);
    GLenum e = gl_check_error();
    if (e != GL_NO_ERROR) fprintf(stderr, "GL error 0x%x in frame\n", (unsigned)e);

    if (a->idle_report > 0.0 && now - t0 >= a->idle_report) {
      printf("idle: %llu frames in %.2f s, fps %.1f\n", a->frames, now - t0, a->fps);
      break;
    }
  }
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
    printf("  ok  upright: file row 0 = bottom rgb %d,%d,%d, row %d = top rgb %d,%d,%d\n",
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
} panel_m;

static void measure_panel(const unsigned char* base, const unsigned char* over, int w, int h,
                          const int* rect, const float bg[4], double bg_lum, panel_m* m) {
  memset(m, 0, sizeof *m);
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
                                const int* r1, const int* r2, int tol) {
  long n = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      if (x >= r1[0] && x < r1[2] && y >= r1[1] && y < r1[3]) continue;
      if (x >= r2[0] && x < r2[2] && y >= r2[1] && y < r2[3]) continue;
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
  camera_reset(&a->cam, a->mesh.fit);

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

  long outside = outside_panels_diff(px, px2, w, h, stats_rect, help_rect, 8);
  if (outside != 0) {
    printf("  FAIL hud ink leaked outside both panel rects: %ld pixels\n", outside);
    (*fails)++;
  } else {
    printf("  ok   hud ink confined to the two panel rects (0 pixels outside)\n");
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
        long nout = outside_panels_diff(px, px2, nw, nh, nstats, nhelp, 8);
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
        if (nout != 0) {
          printf("  FAIL hud ink leaked outside both panel rects on a narrow frame: %ld px\n", nout);
          (*fails)++;
        }
        /* Wrapping proof: the narrow panel is taller than the wide one and narrower. */
        if ((nhx1 - nhx0) < (hx1 - hx0) && (nhy1 - nhy0) > (hy1 - hy0)) {
          printf("  ok   hints wrap: %d x %d -> %d x %d px (narrower, taller)\n",
                 hx1 - hx0, hy1 - hy0, nhx1 - nhx0, nhy1 - nhy0);
        } else {
          printf("  FAIL narrow help %d x %d did not wrap from %d x %d\n",
                 nhx1 - nhx0, nhy1 - nhy0, hx1 - hx0, hy1 - hy0);
          (*fails)++;
        }
      }
      fb_free(&narrow);
    }
  }

  fb_free(&fb);
}

static int run_selftest(app* a) {
  int fails = 0;
  printf("selftest: OBJ parser\n");

  expect_mesh("assets/cube.obj", 12, OBJ_HAS_NORMALS | OBJ_HAS_TEXCOORDS, &fails);
  expect_mesh("assets/quad_vvn.obj", 2, OBJ_HAS_NORMALS, &fails);
  expect_mesh("assets/nov.obj", 2, OBJ_HAS_NORMALS, &fails);   /* normals computed from faces */
  expect_parse_failure("assets/bad.obj", &fails);
  expect_parse_failure("assets/bad_index.obj", &fails);
  expect_mesh("utah_teapot.obj", 1250, OBJ_HAS_NORMALS | OBJ_HAS_TEXCOORDS, &fails);

  printf("selftest: BMP orientation\n");
  expect_bmp_orientation(&fails);

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
             orthographic projection must produce visibly different silhouettes. */
          double cov[4] = { 0, 0, 0, 0 };
          const float fovs[4] = { 45.0f, 15.0f, 80.0f, 45.0f };
          int pixels_ok = 1;
          for (int i = 0; i < 4; i++) {
            camera_reset(&a->cam, a->mesh.fit);
            camera_set_fov(&a->cam, fovs[i]);
            a->cam.ortho = (i == 3);
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

  printf("selftest: %s\n", fails == 0 ? "PASS" : "FAILED");
  return fails == 0 ? 0 : 1;
}

static const char* pick_default_model(void) {
  static const char* candidates[] = { "utah_teapot.obj", "assets/utah_teapot.obj", "assets/cube.obj" };
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
