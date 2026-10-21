#ifndef VIEWER_SHADER_H
#define VIEWER_SHADER_H

#include "gl.h"

/* Read a whole file into a malloc'd NUL-terminated string. */
int shader_read_file(const char* path, char** out, size_t* len, char* err, size_t errcap);

/* Compile both sources and link a program. Fills err with the shader info log on failure. */
int shader_build(const char* vert_path, const char* frag_path, GLuint* prog, char* err, size_t errcap);

#endif
