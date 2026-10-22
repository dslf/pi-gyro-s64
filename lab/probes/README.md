# lab/probes — изолированные зонды

Зонды проверяют отдельный факт (ABI загрузчика GL, порядок глубины, конвенция проекции,
контраст текста HUD, размер глифа) без изменения продукта. Это **не** продуктовый код и не
част сборочного вывода `make`: продукт — это `src/` + `shaders/` + `tests/`.

Артефакты зондов (`*.exe`, `*.bmp`, `*.png`, `*.log`, `*.err`, `*.txt`) игнорируются
`lab/.gitignore`; в репозитории живут только источники зондов и этот файл.

## Как собирать и запускать

Запускать **из корня репозитория** — зонды читают `assets/*.obj` относительным путём.
Флаги совпадают с продуктом, кроме include-путей (`-I../../src` внутри `lab/probes`,
`-Isrc` из корня). Общий шаблон:

```
gcc -O2 -std=c11 -Wall -Wextra -I../../src -I../../ <зонд.c> <нужные src/*.c> -o <зонд>.exe -lglfw3 -lopengl32 -lgdi32 -lwinmm
```

Из корня то же самое с `-Isrc -I.` и `src/...` вместо `../../src/...`.

### ABI GL / GLFW (зависимости: `src/gl.c`)

| зонд | что проверяет |
|---|---|
| `probe_gl46.c` | контекст OpenGL 4.6 core создаётся, GLSL `#version 460 core` компилируется |
| `probe_gl_abi.c` | сигнатуры/вызовы функций из нашего загрузчика GL |
| `probe_attrib.c` | per-vertex атрибуты через отдельные VBO и offset'ы (исторический зонд: на текущем загрузчике часть проверок в `FAIL`) |
| `probe_uniform_abi2.c` | ABI uniform'ов (матрицы, vec3) |
| `probe_glfw.c` | версия GLFW (header/runtime), создание невидимого окна 3.3 core |

### Растр на GPU: глубина, окклюзия, подсветка

| зонд | зависимые модули | что проверяет |
|---|---|---|
| `probe_depth.c` | `src/camera.c src/vmath.c` | запись/чтение depth-текстуры, pick-path |
| `probe_occlusion.c` | `src/gl.c src/camera.c src/obj.c src/mesh.c src/vmath.c` | глубина граней: nearer wins (GL_LESS), `GL_EQUAL` + polygon-offset не перекрывает outline |
| `probe_hl.c` | `src/gl.c src/camera.c src/obj.c src/mesh.c src/render.c src/shader.c src/vmath.c` | подсветка выбранной грани без readback пикселей (uniform + `id`-атрибут + `discard`) |

### Камера, проекция, picking, CPU-растр

| зонд | зависимые модули | что проверяет |
|---|---|---|
| `probe_cam.c` | `src/camera.c src/vmath.c` | конвенция осей проекции (+x/+y в NDC) |
| `probe_pick.c` | `src/gl.c src/camera.c src/mesh.c src/obj.c src/vmath.c` | пиксель → NDC с `y` вниз |
| `probe_frame.c` | `src/obj.c src/camera.c src/vmath.c` | CPU-растр как эталон против BMP `viewer.exe` (foreshortening, порядок глубины, вертикаль) |
| `probe_bbox.c` | `src/obj.c` | bbox/spans модели (`assets/utah_teapot.obj`) |
| `probe_bigobj.c` | `src/obj.c src/camera.c src/vmath.c` + `-lpsapi` | стресс на крупной модели: время парсинга, размеры CPU-буферов, peak RSS, стоимость `pick_triangle` на hover-лучах, время `obj_mesh_free`. Фикстура — локальная `assets/utah_teapot_test_2mil_tri.obj` (не в репозитории): `lab/probes/probe_bigobj.exe assets/utah_teapot_test_2mil_tri.obj` |

### Текст HUD: ink, контраст, глиф

| зонд | зависимые модули | что проверяет |
|---|---|---|
| `probe_hud.c` | `src/gl.c src/img.c` | ink-текст в кадре: строки, позиции, пиксели |
| `probe_hud_contrast.c` | — (GDI) | контраст WCAG текста на ярком фоне, вариант с панелью |
| `probe_atlas.c` | — (GDI) | размер глифа шрифта (Consolas) в пикселях |

### BMP

| зонд | зависимые модули | что проверяет |
|---|---|---|
| `probe_img.c` | `src/img.c` | конвенция BMP (row 0 = верх, RGB) |

### Скрипты анализа кадров (Python / PowerShell)

Требуют Python с `numpy` + `PIL` (или `System.Drawing` для `vision_probe.ps1`).

| скрипт | как запускается | что делает |
|---|---|---|
| `probe_proj.py` | `python lab/probes/probe_proj.py <bmp>` | проекции граней до/после drag — направление orbit |
| `probe_drag.py` | `python lab/probes/probe_drag.py <bmp>` | сдвиг силуэта при drag |
| `probe_bands.py` | `python lab/probes/probe_bands.py <bmp>` | ширина верхней/нижней полосы силуэта |
| `hud_rows.py` | `python lab/probes/hud_rows.py` | ink-строки кадра, gaps панелей, wrap окна подсказок |
| `dump_probe.py` | `python lab/probes/dump_probe.py` | stdout зонда `probe_hud.exe` |
| `bmp2png.ps1` | `powershell -File lab/probes/bmp2png.ps1 <src.bmp> <dst.png>` | BMP → PNG |
| `vision_probe.ps1` | `powershell -File lab/probes/vision_probe.ps1 -ImagePath <png>` | base64-загрузка кадра для vision-проверки текста |
| `probe_resize.ps1` | `powershell -File lab/probes/probe_resize.ps1` | перетаскивание окна реально рисует repaint'ы до конца drag'а |

## Оговорка

Зонды — свидетельство на момент их написания: часть проверяет решения, уже зашитые в продукт, и
может показать `FAIL` после последующих правок. Текущий регламент проверки — `make` +
`viewer.exe --selftest` + `tests/run_tests.ps1` (см. `tests/run_tests.ps1`).
