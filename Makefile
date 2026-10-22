CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Ideps/imgui
IMGUI   = deps/imgui/build/libcimgui.a
LDFLAGS = -lglfw3 -lopengl32 -lgdi32 -lwinmm -lstdc++ -static-libgcc -static-libstdc++

SRC = src/main.c src/gl.c src/vmath.c src/camera.c src/obj.c src/mesh.c \
      src/shader.c src/render.c src/img.c src/hud.c src/settings.c
HDR = src/gl.h src/vmath.h src/obj.h src/mesh.h src/camera.h src/render.h src/img.h src/hud.h src/settings.h
SH  = shaders/model.vert shaders/model.frag

# Owner decision (PROJECT_MEMORY.md §4.20): the product binary lives at the repo root, so
# relative paths (assets/, shaders/) resolve from the repo root as before. `build/` is frozen:
# not used for the binary, reserved for temporary build files (objects) if ever needed.
BIN     = viewer.exe
OBJDIR  = build

all: $(BIN)

imgui: $(IMGUI)

# Vendored C++ library is built by its own script; the product stays C11.
$(IMGUI): deps/imgui/build.ps1 deps/imgui/cimgui.cpp deps/imgui/cimgui.h
	powershell -NoProfile -ExecutionPolicy Bypass -File deps/imgui/build.ps1

# The binary is produced at the repo root (git-ignored); the frozen build/ directory holds only
# temporary files such as *.o and is never created by the default build.
$(BIN): $(SRC) $(HDR) $(SH) $(IMGUI)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(IMGUI) $(LDFLAGS)

test: $(BIN)
	powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1

clean:
	rm -f $(BIN) out_*.bmp screenshot.bmp
	rm -f $(OBJDIR)/*.o
	rm -rf deps/imgui/build

.PHONY: all imgui test clean
