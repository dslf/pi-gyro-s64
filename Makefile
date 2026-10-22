CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra
LDFLAGS = -lglfw3 -lopengl32 -lgdi32 -lwinmm

SRC = src/main.c src/gl.c src/vmath.c src/camera.c src/obj.c src/mesh.c \
      src/shader.c src/render.c src/img.c src/hud.c
HDR = src/gl.h src/vmath.h src/obj.h src/mesh.h src/camera.h src/render.h src/img.h src/hud.h
SH  = shaders/model.vert shaders/model.frag shaders/hud.vert shaders/hud.frag

all: viewer.exe

viewer.exe: $(SRC) $(HDR) $(SH)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

test: viewer.exe
	powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1

clean:
	rm -f viewer.exe out_*.bmp screenshot.bmp

.PHONY: all test clean
