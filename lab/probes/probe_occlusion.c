/* temporary probe: end-to-end GPU check of depth ordering in perspective mode.
   Renders the cube twice - only its near faces, then only its far faces - and
   compares the window depth read back from the FBO with the analytic value. */
#include "src/gl.h"
#include "src/obj.h"
#include "src/mesh.h"
#include "src/camera.h"
#include "src/vmath.h"
#include <GLFW/glfw3.h>
#include <GL/gl.h>      /* GL_LESS and glDepthFunc are GL 1.1 - linkable from opengl32 */
#include <stdio.h>
#include <string.h>

static const int W = 300, H = 300;

static const char* VS =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "uniform mat4 uMVP;\n"
  "void main(){ gl_Position = uMVP * vec4(aPos, 1.0); }\n";

static const char* FS =
  "#version 330 core\n"
  "out vec4 fragColor;\n"
  "void main(){ fragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0); }\n";

static GLuint compile(GLenum t, const char* s) {
  GLuint sh = my_glCreateShader(t);
  my_glShaderSource(sh, 1, &s, NULL);
  my_glCompileShader(sh);
  GLint ok = 0;
  my_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; my_glGetShaderInfoLog(sh, sizeof log, NULL, log); fprintf(stderr, "%s\n", log); return 0; }
  return sh;
}

/* window depth of a point that is `d` in front of the eye, using the same
   convention the projection uses (eye-space z = -d, w = d). */
static float win_depth(const mat4* p, float d) {
  float ndc = (p->m[10] * (-d) + p->m[14]) / (p->m[11] * (-d) + p->m[15]);
  return (ndc + 1.0f) * 0.5f;
}

static float centre_depth(GLuint prog, GLuint uMVP, gpu_mesh* m, const mat4* mvp, GLenum cull) {
  my_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  my_glClearColor(0, 0, 0, 1);
  my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  my_glViewport(0, 0, W, H);
  my_glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  my_glEnable(GL_CULL_FACE);
  my_glCullFace(cull);
  my_glUseProgram(prog);
  my_glUniformMatrix4fv(uMVP, 1, GL_FALSE, &mvp->m[0]);
  my_glBindVertexArray(m->vao);
  my_glDrawArrays(GL_TRIANGLES, 0, m->count);
  my_glDisable(GL_CULL_FACE);
  my_glFinish();

  unsigned char buf[4];
  my_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  my_glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf);
  return buf[0] / 255.0f;
}

int main(void) {
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* w = glfwCreateWindow(W, H, "probe", NULL, NULL);
  if (!w) { fprintf(stderr, "window creation failed\n"); return 1; }
  glfwMakeContextCurrent(w);
  char err[256] = "";
  if (gl_load(glfwGetProcAddress, err, sizeof err) < 0) { fprintf(stderr, "%s\n", err); return 1; }

  obj_mesh o;
  if (obj_parse_file("assets/cube.obj", &o, err, sizeof err) < 0) { fprintf(stderr, "%s\n", err); return 1; }
  float span_z = o.span[2];      /* the axis-aligned cube's face distance is span/2 */
  gpu_mesh m;
  if (mesh_upload(&o, &m, err, sizeof err) < 0) { fprintf(stderr, "%s\n", err); return 1; }
  obj_mesh_free(&o);

  GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
  if (!vs || !fs) { fprintf(stderr, "shader compile failed\n"); return 1; }
  GLuint prog = my_glCreateProgram();
  my_glAttachShader(prog, vs);
  my_glAttachShader(prog, fs);
  my_glLinkProgram(prog);
  GLint linked = 0;
  my_glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  if (!linked) { fprintf(stderr, "link failed\n"); return 1; }
  GLuint uMVP = my_glGetUniformLocation(prog, "uMVP");

  camera c;
  camera_reset(&c, m.fit);
  camera_set_fov(&c, 60.0f);
  c.ortho = 0;                 /* the projection under test */
  c.yaw = 0.0f; c.pitch = 0.0f;   /* eye on +z, looking down -z at the origin */
  mat4 proj = camera_projection(&c, 1.0f);
  mat4 view = camera_view(&c);
  mat4 mvp = m4_mul(&proj, &view);

  /* The cube's flat faces sit at +-span_z/2 along the view axis when the eye is on +z. */
  float near_d = c.dist - span_z * 0.5f, far_d = c.dist + span_z * 0.5f;
  float exp_near = win_depth(&proj, near_d), exp_far = win_depth(&proj, far_d);
  printf("eye %.2f, fit %.2f | analytic window depth: near %.3f, far %.3f\n",
         c.dist, m.fit, exp_near, exp_far);

  float d_near = centre_depth(prog, uMVP, &m, &mvp, GL_BACK);    /* front faces only */
  float d_far  = centre_depth(prog, uMVP, &m, &mvp, GL_FRONT);   /* back faces only */
  printf("rendered centre depth: front-only %.3f, back-only %.3f\n", d_near, d_far);

  int fails = 0;
  if (!(d_near < d_far)) { printf("FAIL nearer faces must win the GL_LESS test\n"); fails++; }
  if (!(d_far - d_near > 0.05f)) { printf("FAIL depth range collapsed (%.4f)\n", d_far - d_near); fails++; }
  if (fabsf(d_near - exp_near) > 0.02f) { printf("FAIL near depth vs analytic %.3f != %.3f\n", d_near, exp_near); fails++; }
  if (fabsf(d_far - exp_far) > 0.02f) { printf("FAIL far depth vs analytic %.3f != %.3f\n", d_far, exp_far); fails++; }

  /* Orthographic, same geometry: ordering must hold and the spread must differ. */
  c.ortho = 1;
  mat4 po = camera_projection(&c, 1.0f);
  mat4 mvp_o = m4_mul(&po, &view);
  float o_near = centre_depth(prog, uMVP, &m, &mvp_o, GL_BACK);
  float o_far  = centre_depth(prog, uMVP, &m, &mvp_o, GL_FRONT);
  printf("ortho centre depth: front-only %.3f, back-only %.3f\n", o_near, o_far);
  if (!(o_near < o_far)) { printf("FAIL ortho ordering inverted\n"); fails++; }

  printf("%s\n", fails ? "PROBE FAILED" : "PROBE PASS");
  mesh_free(&m);
  glfwDestroyWindow(w);
  glfwTerminate();
  return fails ? 1 : 0;
}
