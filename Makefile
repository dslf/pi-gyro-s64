CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Ideps/imgui
IMGUI   = deps/imgui/build/libcimgui.a
LDFLAGS = -lglfw3 -lopengl32 -lgdi32 -lwinmm -lstdc++ -static-libgcc -static-libstdc++

SRC = src/main.c src/gl.c src/vmath.c src/camera.c src/obj.c src/mesh.c \
      src/shader.c src/render.c src/img.c src/hud.c
HDR = src/gl.h src/vmath.h src/obj.h src/mesh.h src/camera.h src/render.h src/img.h src/hud.h
SH  = shaders/model.vert shaders/model.frag

all: viewer.exe

imgui: $(IMGUI)

# Vendored C++ library is built by its own script; the product stays C11.
$(IMGUI): deps/imgui/build.ps1 deps/imgui/cimgui.cpp deps/imgui/cimgui.h
	powershell -NoProfile -ExecutionPolicy Bypass -File deps/imgui/build.ps1

viewer.exe: $(SRC) $(HDR) $(SH) $(IMGUI)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(IMGUI) $(LDFLAGS)

test: viewer.exe
	powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1

clean:
	rm -f viewer.exe out_*.bmp screenshot.bmp
	rm -rf deps/imgui/build

.PHONY: all imgui test clean
