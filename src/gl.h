#ifndef VIEWER_GL_H
#define VIEWER_GL_H

/*
 * Minimal OpenGL entry-point loader.
 *
 * GLFW creates the core-profile context, but every function newer than GL 1.1
 * has to be fetched at runtime: on Windows wglGetProcAddress returns NULL for
 * real GL 1.2+ entry points such as glGetString, so the classic "declare the
 * prototype and link against opengl32.lib" trick does not work. We resolve
 * each entry point through glfwGetProcAddress (wglGetProcAddress as fallback).
 *
 * The loaded functions are *variables* whose type is the real prototype, so a
 * call site reads exactly like standard GL (my_glClear(GL_COLOR_BUFFER_BIT))
 * and the compiler still type-checks the argument list -- unlike a macro alias
 * that would silently accept mismatched arguments.
 */

#include <stddef.h>

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef float         GLfloat;
typedef char          GLchar;
typedef ptrdiff_t     GLintptr;
typedef ptrdiff_t     GLsizeiptr;
typedef int           GLsizei;

/* --- constants (values verified against <GL/gl.h> and <GL/glext.h>) ------- */
#define GL_FALSE                0
#define GL_TRUE                 1
#define GL_NO_ERROR             0
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0547
#define GL_DEPTH_BUFFER_BIT     0x00000100
#define GL_COLOR_BUFFER_BIT     0x00000400
#define GL_TRIANGLES            0x0004
#define GL_LINES                0x0001
#define GL_ARRAY_BUFFER         0x8892
#define GL_STATIC_DRAW          0x88E4
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_TEXTURE_2D           0x0DE1
#define GL_VERTEX_SHADER        0x8B31
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_VERSION              0x1F02
#define GL_RENDERER             0x1F01
#define GL_DEPTH_COMPONENT      0x1902
#define GL_FRAMEBUFFER_SRGB     0x8DB9
#define GL_RGBA                 0x1908
#define GL_RGB                  0x1907
#define GL_RENDERBUFFER         0x8D41
#define GL_DEPTH_COMPONENT24    0x81A6
#define GL_RGBA8                0x8058
#define GL_FRAMEBUFFER          0x8D40
#define GL_READ_FRAMEBUFFER     0x8CA8
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_DEPTH_ATTACHMENT     0x8D00
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_LINEAR               0x2601
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_BLEND                0x0BE2
#define GL_ZERO                 0
#define GL_ONE                  1
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_CULL_FACE            0x0B44
#define GL_DEPTH_TEST           0x0B71
#define GL_BACK                 0x0405
#define GL_FRONT                0x0404
#define GL_CCW                  0x0901
#define GL_CW                   0x0900
#define GL_UNPACK_ROW_LENGTH    0x00020202
#define GL_UNSIGNED_BYTE        0x1401
#define GL_FLOAT                0x1406
#define GL_BGRA                 0x80E1

/* --- prototype typedefs --------------------------------------------------- */
typedef void            (*fn_void)(void);
typedef void            (*fn_ui)(unsigned int);
typedef void            (*fn_enum)(GLenum);
typedef void            (*fn_u)(GLuint);
typedef void            (*fn_eu)(GLenum, GLuint);
typedef void            (*fn_eess)(GLenum, GLenum, GLsizei, GLsizei);
typedef void            (*fn_eeeu)(GLenum, GLenum, GLenum, GLuint);
typedef void            (*fn_eeeui)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void            (*fn_eec)(GLenum, GLenum, GLint);
typedef void            (*fn_eii)(GLenum, GLint, GLsizei);
typedef void            (*fn_euep)(GLenum, GLsizei, GLenum, const void*);
typedef void            (*fn_espc)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void            (*fn_vap)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void            (*fn_su)(GLsizei, GLuint*);
typedef void            (*fn_su2)(GLsizei, const GLuint*);
typedef void            (*fn_i4)(GLint, GLint, GLint, GLint);
typedef void            (*fn_f4)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void            (*fn_f2)(GLfloat, GLfloat);
typedef void            (*fn_uu)(GLuint, GLuint);
typedef void            (*fn_shader_src)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void            (*fn_uei)(GLuint, GLenum, GLint*);
typedef void            (*fn_ussc)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void            (*fn_tex2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void            (*fn_read)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void            (*fn_iuf)(GLint, GLsizei, GLboolean, const float*);
typedef void            (*fn_iuf3)(GLint, GLsizei, GLboolean, const float*);
typedef void            (*fn_iusf)(GLint, GLsizei, const float*);
typedef void            (*fn_if)(GLint, GLfloat);
typedef GLint           (*fn_iuc)(GLuint, const GLchar*);
typedef GLuint          (*fn_u1)(GLenum);
typedef GLuint          (*fn_u0)(void);
typedef GLenum          (*fn_enum0)(void);
typedef GLenum          (*fn_e2enum)(GLenum);
typedef const GLubyte*  (*fn_str)(GLenum);
typedef GLboolean       (*fn_isen)(GLenum);
typedef void            (*fn_ee)(GLenum, GLenum);
typedef void            (*fn_bool)(GLboolean);

/* --- loaded entry points (NULL until gl_load succeeds) -------------------- */
extern fn_enum   my_glEnable;
extern fn_enum   my_glDisable;
extern fn_void   my_glFlush;
extern fn_void   my_glFinish;
extern fn_u      my_glDeleteProgram;
extern fn_u      my_glDeleteShader;
extern fn_u      my_glUseProgram;
extern fn_u      my_glLinkProgram;
extern fn_u      my_glCompileShader;
extern fn_u      my_glEnableVertexAttribArray;
extern fn_u      my_glDisableVertexAttribArray;

extern fn_ui     my_glClear;
extern fn_f4     my_glClearColor;
extern fn_i4     my_glViewport;
extern fn_enum   my_glCullFace;
extern fn_f2     my_glLineWidth;
extern fn_eii    my_glDrawArrays;
extern fn_euep   my_glDrawElements;
extern fn_eu     my_glBindBuffer;
extern fn_u      my_glBindVertexArray;
extern fn_eu     my_glBindTexture;
extern fn_eu     my_glBindRenderbuffer;
extern fn_eu     my_glBindFramebuffer;
extern fn_enum   my_glReadBuffer;
extern fn_espc   my_glBufferData;
extern fn_vap    my_glVertexAttribPointer;
extern fn_su     my_glGenBuffers;
extern fn_su     my_glGenVertexArrays;
extern fn_su     my_glGenTextures;
extern fn_su     my_glGenFramebuffers;
extern fn_su     my_glGenRenderbuffers;
extern fn_su2    my_glDeleteBuffers;
extern fn_su2    my_glDeleteVertexArrays;
extern fn_su2    my_glDeleteTextures;
extern fn_su2    my_glDeleteFramebuffers;
extern fn_su2    my_glDeleteRenderbuffers;
extern fn_eess   my_glRenderbufferStorage;
extern fn_eeeu   my_glFramebufferRenderbuffer;
extern fn_eeeui  my_glFramebufferTexture2D;
extern fn_eec    my_glTexParameteri;
extern fn_tex2d  my_glTexImage2D;
extern fn_read   my_glReadPixels;
extern fn_u1     my_glCreateShader;
extern fn_u0     my_glCreateProgram;
extern fn_uu     my_glAttachShader;
extern fn_shader_src my_glShaderSource;
extern fn_uei    my_glGetShaderiv;
extern fn_uei    my_glGetProgramiv;
extern fn_ussc   my_glGetShaderInfoLog;
extern fn_ussc   my_glGetProgramInfoLog;
extern fn_iuc    my_glGetUniformLocation;
extern fn_iuf    my_glUniformMatrix4fv;
extern fn_iuf3   my_glUniformMatrix3fv;
extern fn_iusf   my_glUniform3fv;
extern fn_if     my_glUniform1f;
extern fn_enum0  my_glGetError;
extern fn_e2enum my_glCheckFramebufferStatus;
extern fn_str    my_glGetString;
extern fn_isen   my_glIsEnabled;
extern fn_ee     my_glBlendFunc;
extern fn_bool   my_glDepthMask;
extern fn_iusf   my_glUniform2fv;

/* --- loader --------------------------------------------------------------- */
typedef void (*(*gl_getproc)(const char*))(void);

/* Resolve the entry points above. Requires a current context. 0 = ok. */
int  gl_load(gl_getproc getproc, char* err, size_t errcap);
/* Non-zero after a successful gl_load. */
int  gl_ok(void);
/* NULL on failure. */
const char* gl_version_string(char* err, size_t errcap);
const char* gl_renderer_string(char* err, size_t errcap);
/* Drains the error queue and returns the first error, or GL_NO_ERROR. */
GLenum gl_check_error(void);

#endif
