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
#include <sys/stat.h>

#define VERT_SHADER "shaders/model.vert"
#define FRAG_SHADER "shaders/model.frag"

typedef struct {
  GLFWwindow* win;
  camera cam;
  gpu_mesh mesh;
  obj_mesh cpu;      /* kept after upload: cursor picking and the outline read vertices */
  renderer rend;
  hud hudl;           /* text overlay drawn over the model, top-left of the viewport */
  int width, height;
  int wireframe;
  int hud_overlay;      /* HUD is drawn only in interactive frames; offscreen renders stay HUD-free */
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

  /* HUD overlay: camera state plus the picked triangle, top-left in pixel space. */
  if (a->hud_overlay) {
    int sel = a->locked >= 0 ? a->locked : a->hover;
    char line1[160], line2[160];
    snprintf(line1, sizeof line1, "%s  fov %.0f deg  dist %.2f", a->cam.ortho ? "ortho" : "persp", a->cam.fov_deg, a->cam.dist);
    snprintf(line2, sizeof line2, "%d tris  selected %d%s", a->cpu.triangles, sel,
             sel < 0 ? " (none)" : (a->locked >= 0 ? " (pinned)" : " (hover)"));
    hud_draw(&a->hudl, w, h, 6, 4, line1);
    hud_draw(&a->hudl, w, h, 6, 4 + a->hudl.ch, line2);
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
  if (hud_init(&a->hudl, hud_err, sizeof hud_err) < 0) {
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

  while (!glfwWindowShouldClose(a->win)) {
    handle_mouse(a);
    if (a->dirty) {
      draw_scene(a, NULL);
      update_title(a);
      glfwSwapBuffers(a->win);
      a->dirty = 0;
      GLenum e = gl_check_error();
      if (e != GL_NO_ERROR) fprintf(stderr, "GL error 0x%x in frame\n", (unsigned)e);
    }
    glfwWaitEventsTimeout(0.05);
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

/* Centroid of every differing pixel: tells where the HUD landed without guessing. */
static void diff_centroid(const unsigned char* a, const unsigned char* b, int w, int h, int tol,
                          int* count, double* cx, double* cy) {
  long sx = 0, sy = 0, n = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const unsigned char* p = a + ((size_t)y * w + x) * 4;
      const unsigned char* q = b + ((size_t)y * w + x) * 4;
      int dr = p[0] - q[0], dg = p[1] - q[1], db = p[2] - q[2];
      if (dr * dr + dg * dg + db * db > tol * tol) { sx += x; sy += y; n++; }
    }
  *count = (int)n;
  *cx = n ? (double)sx / (double)n : 0.0;
  *cy = n ? (double)sy / (double)n : 0.0;
}

/* Fraction of changed pixels per row inside one HUD glyph cell, rows top-down. */
static void rendered_row_profile(const unsigned char* base, const unsigned char* hud, int w,
                                 int x0, int cw, int y0, int ch, float* prof, int cap) {
  for (int v = 0; v < cap; v++) prof[v] = 0.0f;
  for (int v = 0; v < ch && v < cap; v++) {
    int ink = 0;
    for (int c = 0; c < cw; c++) {
      int x = x0 + c, y = y0 + v;
      const unsigned char* p = base + ((size_t)y * w + x) * 4;
      const unsigned char* q = hud + ((size_t)y * w + x) * 4;
      int dr = p[0] - q[0], dg = p[1] - q[1], db = p[2] - q[2];
      if (dr * dr + dg * dg + db * db > 16 * 16) ink++;
    }
    prof[v] = (float)ink / (float)cw;
  }
}

/* Mean absolute difference between two row profiles; used for orientation checks. */
static float profile_delta(const float* a, const float* b, int n) {
  float sum = 0.0f;
  for (int i = 0; i < n; i++) sum += fabsf(a[i] - b[i]);
  return sum / (float)n;
}

/* The HUD is the overlay, so it is measured against the same scene drawn without it:
   every differing pixel is HUD ink. Position, coverage, stroke structure, orientation
   and determinism are all numeric, so a flipped or slab-like glyph cannot pass. */
static void hud_selftest(app* a, int* fails) {
  printf("selftest: HUD overlay\n");
  char err[256] = "";
  const int w = 320, h = 200;
  framebuffer fb;
  if (fb_create(&fb, w, h, err, sizeof err) < 0) {
    printf("  FAIL hud framebuffer: %s\n", err);
    (*fails)++;
    return;
  }

  static unsigned char px[320 * 200 * 4];
  static unsigned char px2[320 * 200 * 4];
  static unsigned char px3[320 * 200 * 4];
  camera_reset(&a->cam, a->mesh.fit);

  a->hud_overlay = 0;
  draw_scene(a, &fb);
  int rc = fb_read_rgba(&fb, px, err, sizeof err);
  a->hud_overlay = 1;
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

  int n = 0;
  double cx = 0.0, cy = 0.0;
  diff_centroid(px, px2, w, h, 8, &n, &cx, &cy);
  if (n < 40) {
    printf("  FAIL HUD changed only %d pixels - overlay is not visible\n", n);
    (*fails)++;
  } else {
    printf("  ok   HUD ink: %d pixels of %dx%d (%.2f%% of frame)\n", n, w, h, 100.0 * n / (double)(w * h));
    if (cy >= h * 0.5 || cx >= w * 0.5) {
      printf("  FAIL HUD centroid (%.1f, %.1f) is outside the top-left half of %dx%d\n", cx, cy, w, h);
      (*fails)++;
    } else {
      printf("  ok   HUD centroid (%.1f, %.1f) sits top-left\n", cx, cy);
    }
  }

  /* Determinism: the same frame twice must produce identical HUD pixels. */
  int moved = region_diff_count(px2, px3, w, h, 0, 0, w, h, 8);
  if (moved != 0) {
    printf("  FAIL HUD moved %d pixels between identical redraws\n", moved);
    (*fails)++;
  } else {
    printf("  ok   HUD layout is deterministic (0 pixels moved on redraw)\n");
  }

  /* First glyph cell of the first line, drawn at (6, 4). */
  const int cw = a->hudl.cw, ch = a->hudl.ch;
  const char first = a->cam.ortho ? 'o' : 'p';
  float mask[32], got[32];
  const int cap = ch < 32 ? ch : 32;
  hud_glyph_row_profile(&a->hudl, first, mask, cap);
  rendered_row_profile(px, px2, w, 6, cw, 4, ch, got, cap);

  float lo = 1.0f, hi = 0.0f, ink_rows = 0.0f;
  for (int v = 0; v < cap; v++) {
    if (got[v] < lo) lo = got[v];
    if (got[v] > hi) hi = got[v];
    ink_rows += got[v];
  }
  if (hi - lo < 0.15f) {
    printf("  FAIL glyph '%c' cell is flat (row ink %.1f%%..%.1f%%) - geometry is not glyph-shaped\n",
           first, lo * 100.0f, hi * 100.0f);
    if (cw * ch <= 512) {
      unsigned char cell[512];
      hud_glyph_cell_mask(&a->hudl, first, cell, cw * ch);
      printf("  mask cell (ink where lum < 160):\n");
      for (int v = 0; v < ch; v++) {
        printf("    %2d ", v);
        for (int c = 0; c < cw; c++) printf("%c", cell[v * cw + c] < 160 ? '#' : '.');
        printf("\n");
      }
      printf("  rendered HUD pixels in the same cell:\n");
      for (int v = 0; v < ch; v++) {
        printf("    %2d ", v);
        for (int c = 0; c < cw; c++) {
          int x = 6 + c, y = 4 + v;
          const unsigned char* p = px + ((size_t)y * w + x) * 4;
          const unsigned char* q = px2 + ((size_t)y * w + x) * 4;
          int dr = p[0] - q[0], dg = p[1] - q[1], db = p[2] - q[2];
          printf("%c", dr * dr + dg * dg + db * db > 16 * 16 ? '#' : '.');
        }
        printf("\n");
      }
    }
    (*fails)++;
  } else {
    printf("  ok   glyph '%c' has stroke structure (row ink %.1f%%..%.1f%%, mean %.1f%%)\n",
           first, lo * 100.0f, hi * 100.0f, ink_rows / (float)cap * 100.0f);
  }

  /* Orientation: rendered rows must match the CPU mask rows top-down, not mirrored. */
  float mirror[32];
  for (int v = 0; v < cap; v++) mirror[v] = mask[cap - 1 - v];
  float direct = profile_delta(got, mask, cap);
  float flipped = profile_delta(got, mirror, cap);
  printf("  glyph '%c' vs mask: direct %.3f, mirrored %.3f\n", first, direct, flipped);
  if (direct >= flipped) {
    printf("  FAIL glyph rows match the mask better when mirrored (direct %.3f >= mirrored %.3f)\n", direct, flipped);
    (*fails)++;
  } else if (direct > 0.30f) {
    printf("  FAIL glyph rows do not match the mask (direct %.3f)\n", direct);
    (*fails)++;
  } else {
    printf("  ok   glyph orientation matches the mask top-down (direct %.3f < mirrored %.3f)\n", direct, flipped);
  }

  /* Ink must stay ink: most of the cell must still be background, not a solid slab. */
  int cell_px = cw * ch;
  int cell_ink = region_diff_count(px, px2, w, h, 6, 4, 6 + cw, 4 + ch, 8);
  double bg = 1.0 - (double)cell_ink / (double)cell_px;
  if (bg < 0.40) {
    printf("  FAIL glyph cell is %.1f%% ink - a slab, not text\n", (1.0 - bg) * 100.0);
    (*fails)++;
  } else {
    printf("  ok   glyph cell keeps %.1f%% background around the ink\n", bg * 100.0);
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
  a.wireframe = wire;
  a.fov_req = fov_req;
  a.fov_given = fov_given;
  a.ortho_req = ortho_req;
  a.yaw_req = yaw_req; a.yaw_given = yaw_given;
  a.pitch_req = pitch_req; a.pitch_given = pitch_given;
  a.width = w; a.height = h;

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
  hud_free(&a.hudl);
  mesh_free(&a.mesh);
  obj_mesh_free(&a.cpu);
  glfwDestroyWindow(a.win);
  glfwTerminate();

  /* -2 = bad arguments / unreadable model (exit 2), -1 = GL or IO failure (exit 1) */
  if (rc < 0) return (rc == -2) ? 2 : 1;
  return rc;
}
