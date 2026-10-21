#include "shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int shader_read_file(const char* path, char** out, size_t* len, char* err, size_t errcap) {
  FILE* f = fopen(path, "rb");
  if (!f) { snprintf(err, errcap, "cannot open '%s'", path); return -1; }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) { fclose(f); snprintf(err, errcap, "'%s' is empty", path); return -1; }

  char* buf = malloc((size_t)size + 1);
  if (!buf) { fclose(f); snprintf(err, errcap, "out of memory"); return -1; }

  size_t read = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[read] = '\0';

  *out = buf;
  if (len) *len = read;
  return 0;
}

static int compile(GLenum type, const char* path, const char* src, GLuint* out, char* err, size_t errcap) {
  GLuint s = my_glCreateShader(type);
  if (!s) { snprintf(err, errcap, "glCreateShader returned 0 for '%s'", path); return -1; }

  my_glShaderSource(s, 1, &src, NULL);
  my_glCompileShader(s);

  GLint ok = 0;
  my_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (ok) { *out = s; return 0; }

  GLint len = 0;
  my_glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
  if (len > 1) {
    char* log = malloc((size_t)len);
    if (log) {
      GLsizei n = 0;
      my_glGetShaderInfoLog(s, len, &n, log);
      snprintf(err, errcap, "%s:\n%s", path, log);
      free(log);
    } else {
      snprintf(err, errcap, "compile of '%s' failed (no memory for the log)", path);
    }
  } else {
    snprintf(err, errcap, "compile of '%s' failed (empty info log)", path);
  }
  my_glDeleteShader(s);
  return -1;
}

int shader_build(const char* vert_path, const char* frag_path, GLuint* prog, char* err, size_t errcap) {
  char* vsrc = NULL;
  char* fsrc = NULL;
  GLuint vs = 0, fs = 0;
  int rc = -1;

  if (shader_read_file(vert_path, &vsrc, NULL, err, errcap) < 0) return -1;
  if (shader_read_file(frag_path, &fsrc, NULL, err, errcap) < 0) { free(vsrc); return -1; }

  if (compile(GL_VERTEX_SHADER, vert_path, vsrc, &vs, err, errcap) < 0) goto fail;
  if (compile(GL_FRAGMENT_SHADER, frag_path, fsrc, &fs, err, errcap) < 0) goto fail;

  GLuint p = my_glCreateProgram();
  if (!p) { snprintf(err, errcap, "glCreateProgram returned 0"); goto fail; }

  my_glAttachShader(p, vs);
  my_glAttachShader(p, fs);
  my_glLinkProgram(p);

  GLint ok = 0;
  my_glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (ok) { *prog = p; rc = 0; goto fail; }

  GLint len = 0;
  my_glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
  if (len > 1) {
    char* log = malloc((size_t)len);
    if (log) {
      GLsizei n = 0;
      my_glGetProgramInfoLog(p, len, &n, log);
      snprintf(err, errcap, "link failed:\n%s", log);
      free(log);
    }
  } else {
    snprintf(err, errcap, "link failed (empty info log)");
  }
  my_glDeleteProgram(p);
  *prog = 0;

fail:
  if (vs) my_glDeleteShader(vs);
  if (fs) my_glDeleteShader(fs);
  free(vsrc);
  free(fsrc);
  return rc;
}
