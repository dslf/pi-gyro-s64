#include "gl.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* --- storage for the loaded entry points ---------------------------------- */
fn_enum  my_glEnable;
fn_enum  my_glDisable;
fn_void  my_glFlush;
fn_void  my_glFinish;
fn_u     my_glDeleteProgram;
fn_u     my_glDeleteShader;
fn_u     my_glUseProgram;
fn_u     my_glLinkProgram;
fn_u     my_glCompileShader;
fn_u     my_glEnableVertexAttribArray;
fn_u     my_glDisableVertexAttribArray;

fn_ui    my_glClear;
fn_f4    my_glClearColor;
fn_i4    my_glViewport;
fn_enum  my_glCullFace;
fn_f2    my_glLineWidth;
fn_eii   my_glDrawArrays;
fn_euep  my_glDrawElements;
fn_eu    my_glBindBuffer;
fn_u     my_glBindVertexArray;
fn_eu    my_glBindTexture;
fn_eu    my_glBindRenderbuffer;
fn_eu    my_glBindFramebuffer;
fn_enum  my_glReadBuffer;
fn_espc  my_glBufferData;
fn_vap   my_glVertexAttribPointer;
fn_su    my_glGenBuffers;
fn_su    my_glGenVertexArrays;
fn_su    my_glGenTextures;
fn_su    my_glGenFramebuffers;
fn_su    my_glGenRenderbuffers;
fn_su2   my_glDeleteBuffers;
fn_su2   my_glDeleteVertexArrays;
fn_su2   my_glDeleteTextures;
fn_su2   my_glDeleteFramebuffers;
fn_su2   my_glDeleteRenderbuffers;
fn_eess  my_glRenderbufferStorage;
fn_eeeu  my_glFramebufferRenderbuffer;
fn_eeeui my_glFramebufferTexture2D;
fn_eec   my_glTexParameteri;
fn_tex2d my_glTexImage2D;
fn_read  my_glReadPixels;
fn_u1    my_glCreateShader;
fn_u0    my_glCreateProgram;
fn_uu    my_glAttachShader;
fn_shader_src my_glShaderSource;
fn_uei   my_glGetShaderiv;
fn_uei   my_glGetProgramiv;
fn_ussc  my_glGetShaderInfoLog;
fn_ussc  my_glGetProgramInfoLog;
fn_iuc   my_glGetUniformLocation;
fn_iuf   my_glUniformMatrix4fv;
fn_iuf3  my_glUniformMatrix3fv;
fn_iusf  my_glUniform3fv;
fn_if    my_glUniform1f;
fn_enum0 my_glGetError;
fn_e2enum my_glCheckFramebufferStatus;
fn_str   my_glGetString;
fn_isen  my_glIsEnabled;
fn_ee    my_glBlendFunc;
fn_bool  my_glDepthMask;
fn_iusf  my_glUniform2fv;

static int g_loaded = 0;

#define GL_SLOT(fname) { "gl" #fname, (void**)&my_gl##fname }

struct slot { const char* name; void** target; };

static struct slot table[] = {
  GL_SLOT(Enable), GL_SLOT(Disable), GL_SLOT(IsEnabled), GL_SLOT(Flush), GL_SLOT(Finish),
  GL_SLOT(DeleteProgram), GL_SLOT(DeleteShader), GL_SLOT(UseProgram),
  GL_SLOT(LinkProgram), GL_SLOT(CompileShader),
  GL_SLOT(EnableVertexAttribArray), GL_SLOT(DisableVertexAttribArray),
  GL_SLOT(Clear), GL_SLOT(ClearColor), GL_SLOT(Viewport), GL_SLOT(CullFace),
  GL_SLOT(LineWidth), GL_SLOT(DrawArrays), GL_SLOT(DrawElements),
  GL_SLOT(BindBuffer), GL_SLOT(BindVertexArray), GL_SLOT(BindTexture),
  GL_SLOT(BindRenderbuffer), GL_SLOT(BindFramebuffer), GL_SLOT(ReadBuffer),
  GL_SLOT(BufferData), GL_SLOT(VertexAttribPointer),
  GL_SLOT(GenBuffers), GL_SLOT(GenVertexArrays), GL_SLOT(GenTextures),
  GL_SLOT(TexParameteri), GL_SLOT(TexImage2D),
  GL_SLOT(GenFramebuffers), GL_SLOT(GenRenderbuffers),
  GL_SLOT(DeleteBuffers), GL_SLOT(DeleteVertexArrays), GL_SLOT(DeleteTextures),
  GL_SLOT(DeleteFramebuffers), GL_SLOT(DeleteRenderbuffers),
  GL_SLOT(RenderbufferStorage), GL_SLOT(FramebufferRenderbuffer),
  GL_SLOT(FramebufferTexture2D), GL_SLOT(ReadPixels),
  GL_SLOT(CreateShader), GL_SLOT(CreateProgram), GL_SLOT(AttachShader),
  GL_SLOT(ShaderSource), GL_SLOT(GetShaderiv), GL_SLOT(GetProgramiv),
  GL_SLOT(GetShaderInfoLog), GL_SLOT(GetProgramInfoLog),
  GL_SLOT(GetUniformLocation), GL_SLOT(UniformMatrix4fv), GL_SLOT(UniformMatrix3fv),
  GL_SLOT(Uniform3fv), GL_SLOT(Uniform2fv),
  GL_SLOT(BlendFunc), GL_SLOT(DepthMask),
  GL_SLOT(Uniform1f), GL_SLOT(GetError), GL_SLOT(CheckFramebufferStatus),
  GL_SLOT(GetString),
};

static void* resolve(gl_getproc getproc, const char* name) {
  void* p = getproc ? (void*)getproc(name) : NULL;
  if (p) return p;
#ifdef _WIN32
  /* wglGetProcAddress gives the driver's extension functions (GL >= 1.2). */
  p = (void*)wglGetProcAddress(name);
  if (p && p != (void*)1 && p != (void*)2 && p != (void*)3 && p != (void*)4) return p;
  /* Core 1.1 functions live in opengl32.dll itself. */
  HMODULE m = GetModuleHandleA("opengl32.dll");
  if (!m) m = LoadLibraryA("opengl32.dll");
  if (m) return (void*)GetProcAddress(m, name);
#endif
  return NULL;
}

int gl_load(gl_getproc getproc, char* err, size_t errcap) {
  const size_t n = sizeof table / sizeof table[0];
  for (size_t i = 0; i < n; i++) {
    void* p = resolve(getproc, table[i].name);
    if (!p) {
      snprintf(err, errcap, "OpenGL entry point '%s' is not available", table[i].name);
      return -1;
    }
    *table[i].target = p;
  }
  g_loaded = 1;
  return 0;
}

int gl_ok(void) { return g_loaded; }

const char* gl_version_string(char* err, size_t errcap) {
  if (!my_glGetString) { snprintf(err, errcap, "glGetString not loaded"); return NULL; }
  const GLubyte* s = my_glGetString(GL_VERSION);
  if (!s) { snprintf(err, errcap, "glGetString(GL_VERSION) returned NULL -- is a context current?"); return NULL; }
  return (const char*)s;
}

const char* gl_renderer_string(char* err, size_t errcap) {
  if (!my_glGetString) { snprintf(err, errcap, "glGetString not loaded"); return NULL; }
  const GLubyte* s = my_glGetString(GL_RENDERER);
  if (!s) { snprintf(err, errcap, "glGetString(GL_RENDERER) returned NULL"); return NULL; }
  return (const char*)s;
}

GLenum gl_check_error(void) {
  if (!my_glGetError) return (GLenum)-1;
  GLenum e = GL_NO_ERROR, first = GL_NO_ERROR;
  while ((e = my_glGetError()) != GL_NO_ERROR) { if (first == GL_NO_ERROR) first = e; }
  return first;
}
