# Отчёт: E5.T5/E5.T6 — HUD на Dear ImGui (перенос из lab/imgui в продукт)

**Цель.** Заменить GDI-атлас HUD на Dear ImGui (vendored cimgui, C ABI), добавить FPS и
подсказки горячих клавиш, повысить читаемость, сохранить продукт C11 и численные проверки.

**Сделано.**
- `deps/imgui/` — vendored cimgui + imgui 1.93.0 (MIT): `cimgui.cpp/.h`, `cimgui_impl.cpp/.h`,
  `imgui/*.cpp` (включая `imgui_demo.cpp`), `imgui/backends/imgui_impl_glfw.cpp`,
  `imgui_impl_opengl3.cpp`, `imconfig.h`, `LICENSE`.
- `deps/imgui/build.ps1` — сборка `deps/imgui/build/libcimgui.a` (g++ C++ для C++ файлов,
  gcc для `cimgui.cpp` с `-DIMGUI_IMPL_API=extern "C"`, линковка бэкендов с `-Dimplementations=`,
  инкрементальная recompilation, проверка exported C-символов).
- `deps/imgui/impl_api.h` — `IMGUI_IMPL_API extern "C"` для gcc.
- `Makefile` — target `imgui` + линковка архива, `-lstdc++ -static-libgcc -static-libstdc++`,
  `-Ideps/imgui`; удалены `shaders/hud.vert/frag`.
- `src/hud.h/.c` — C API поверх cimgui: `hud_init` (context + GLFW/OpenGL3 бэкенды, явный
  дефолтный шрифт, масштаб 1.5, палитра: тёмный фон 0.10/0.12/0.15 @ 0.72, текст 0.95/0.96/0.98),
  `hud_frame` (панель 480×112 в левом верхнем углу, `NoInputs/NoMove/NoTitleBar`, строки:
  проекция+fov+dist, FPS, треугольники, выбор hover/pinned, wireframe+hotkeys), `hud_render`,
  `hud_rect`, `hud_colors`. Явный `io.DisplaySize` для offscreen-FBO.
- `src/main.c` — `hud_frame` вызывается из `draw_scene` (только интерактивный кадр через
  `app.hud_overlay`); FPS считается сглаживанием по интервалам перерисовки в loop; selftest-блок
  HUD переписан на пиксельные метрики ImGui (FBO 560×240, warm-up кадры для атласа, rect панели,
  чернила внутри rect, контраст WCAG, короткие/длинные штрихи, детерминизм, палитра).
- `.gitignore` + `lab/.gitignore` — артефакты сборки и vendored клон cimgui в lab не трекаются.
- `Roadmap.md`, `PROJECT_MEMORY.md` — статусы и ADR.

**Коммиты.** `02aa288` (код + vendored deps + lab probe), `7849d4c` (Roadmap + Memory).

**Проверено.**
```
imgui: OK  objects=9 compiled=0 archive=2044694 bytes  C symbols present=15
gcc ... -o viewer.exe deps/imgui/build/libcimgui.a ...   (без warning'ов, exit 0)
```
`viewer.exe --selftest assets/cube.obj` → `selftest: PASS`, exit 0; блок HUD:
```
hud: imgui 1.93.0, font scale 1.50, panel 480x112 px
  ok   HUD ink: 53457 pixels of 560x240 (39.77% of frame)
  ok   HUD centroid (247.5, 63.5) sits top-left
  ok   HUD layout is deterministic (0 pixels moved on redraw)
  ok   hud rect 8..488 x 8..120 (480 x 112 px)
  ok   hud palette: text lum 242.2 on panel bg lum 29.6 (requested 14.99:1)
  ok   hud ink mean lum 172.6 (max 246.9), panel mean lum 42.2
  ok   hud ink coverage 17.19% of panel, contrast 6.36:1 (WCAG)
  ok   ink runs: short(<=12px)=2039 long(>12px)=6, ratio 339.83
```
`tests/run_tests.ps1` → `tests: PASS`, exit 0 (все регрессионные проверки без изменений порогов).
Интерактивный запуск (`viewer.exe utah_teapot.obj`, 4 с) — окно живёт, stderr пуст,
печатаются `GL: 4.6.0 NVIDIA`, `hud: imgui 1.93.0 ...`, `controls: ...`.

**Риски / следующий шаг.**
- Сборка продукта теперь требует `g++` + `ar` (ImGui-архив); чистый clone собирает его сам.
- Атлас появляется со 2-го кадра → в selftest есть warm-up; в интерактиве это незаметно.
- FPS считается по интервалам перерисовки (без vsync-ограничения не показывает limit).
- `id` треугольника в HUD — E5.T1 остаётся 🟡.
