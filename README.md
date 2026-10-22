# pi-gyro-s64

**OBJ / GL viewer** — интерактивный просмотрщик Wavefront `.obj` на OpenGL 4.6 core, написанный на
C11 без внешних библиотек кроме GLFW и Dear ImGui. **Проект в стадии активной разработки**: это
рабочая дев-ветка, релизов и tag'ов нет, API и hotkeys могут меняться.

![Viewer: shaded teapot, silhouette outline, HUD panels](docs/viewer.png)

На скриншоте: Utah teapot (1250 граней), шейдинг с двумя light'ами, outline силуэта (edge-detected,
`id`-атрибут вместо stencil), выбранная грань, две панели HUD (статистика + подсказки hotkeys) с
маской под текстом.

## Что делает

- Загрузка `.obj` (triangulation, позиции + нормали, bbox-центрирование и fit по bounding radius).
- Orbit / pan / zoom camera, перспектива и ортография (`P`), auto-fit при загрузке.
- Шейдинг (ambient + diffuse + specular), билинейный filter для outline-сэмплеров.
- **Outline всего силуэта** без readback: edge detection + polygon-offset (`GL_EQUAL`), 2-pass.
- **Picking по face id**: `id`-атрибут + `discard` (главный pass) и `glReadPixels` для pick-pass;
  nearest-first по глубине, `Tab`/`Shift+Tab` — шаг по ближайным граням к камере.
- **Подсветка выбранной грани** без readback: uniform + `discard`.
- **HUD на Dear ImGui** (двухпанельный: статистика и подсказки), текст из атласа, контраст по WCAG.
- **Resizing окна рисуется сразу**: framebuffer size перечитывается в кадре, не в callback'е
  (`glfwPollEvents` на Windows во время drag'a окна не вызывается — repaint идёт из `WM_PAINT`).
- Offscreen-рендер в FBO и запись BMP/PNG без окна (`--shot`).
- **`--selftest`**: численные метрики вместо «на глаз» — coverage силуэта (> 2%), outline-пиксели
  (2–35% кадра, контраст ≥ 600), pick-пиксель в 20 px от центра, отсутствие чёрных пикселей после
  resize, gaps HUD ≤ 2 px, wrap подсказок. 8 разделов, 59 проверок, пороги относительные, чтобы
  пережить другой GPU/AA.

## Стек

| | |
|---|---|
| Язык | C11, `gcc -O2 -std=c11 -Wall -Wextra`, сборка без warning'ов |
| Графика | OpenGL 4.6 core, GLSL `#version 460 core`, GLFW 3.4 (static) |
| UI | Dear ImGui (backend `imgui_impl_glfw` + `imgui_impl_opengl3`) |
| Математика | свои `mat4`/`vec3` (`src/vmath.c`), без внешних math-libs |
| Загрузчик GL | ручной (`src/gl.c`): `glfwGetProcAddress` + свои typedef'ы, без `glad`/`glew` |
| Вывод кадров | свой BMP-writer (`src/img.c`), PNG конвертация пост-процессом |
| Platform | Windows (mingw-w64 / w64devkit, GCC 14.2); статический линк GLFW, mingw-специфичные линк-флаги |

Внешних зависимостей в `src/` нет: `deps/imgui` (vendored, MIT) и статический GLFW.

## Сборка и запуск

Нужен mingw-w64 (в этом окружении — w64devkit, GCC 14.2) со статическим GLFW 3.4
(`libglfw3.a` в lib-каталоге тулчейна) — GLFW линкуется через `-lglfw3`, в репозитории его нет.

```sh
make imgui      # на чистом клоне сначала: собирает vendored Dear ImGui (libcimgui.a)
make            # viewer.exe (GCC + mingw-w64; линк GLFW/imgui статически)
make test       # selftest + tests/run_tests.ps1
```

```sh
./viewer.exe                     # Utah teapot
./viewer.exe assets/cube.obj     # свой файл
./viewer.exe assets/cube.obj --shot out.bmp 800 600   # offscreen BMP без окна
./viewer.exe --selftest          # 8 разделов, 59 численных проверок
```

### Управление

| Клавиша | Действие |
|---|---|
| ЛКМ drag | orbit (yaw/pitch, pitch clamp ±85°) |
| ПКМ drag | pan |
| Колесо | zoom (clamp по размеру модели) |
| `L` | подсветка всех граней (wireframe overlay) |
| `W` | wireframe + shaded (кадр 1000×750, метрика coverage) |
| `LWin+W` | wireframe-only offscreen 640×480, ratio к shaded ≥ 1.5× |
| `T` | пересборка mesh с другой winding-конвенцией |
| `P` | перспектива ↔ ортография |
| `R` | reset камеры (auto-fit по bounding radius) |
| `Tab` / `Shift+Tab` | шаг по граням, ближайшая к камере первой |
| `H` | HUD on/off |
| `Esc` | выход |

## Проверка

Публичная проверка — `make test`: сборка, `viewer.exe --selftest` (8 разделов, 59 проверок, exit 0),
`tests/run_tests.ps1` (BMP-метрики: coverage, outline, picking, resize, wireframe, offscreen).
Проверки числовые; пороги относительные.

## Структура

```
src/         продукт (C11): obj, camera, vmath, mesh, shader, render, gl, img, hud, main
shaders/     GLSL 460 core
assets/      тестовые модели (cube, teapot)
tests/       PS1 + Python проверка пикселей
docs/        скриншот для README
lab/probes/  изолированные зонды ABI/GL/HUD — см. lab/probes/README.md
deps/imgui/  vendored Dear ImGui (MIT) + libcimgui.a
Roadmap.md   фазы E0…E7 и статусы
PROJECT_MEMORY.md  решения и факты окружения (ADR)
AGENTS.md    регламент разработки
```

## Как разрабатывается

Проект написан **ИИ в локальном окружении**, без ручной правки кода:

- **Harness:** [pi.dev](https://pi.dev) coding agent (TUI, сессии в `~/.pi/agent/sessions`).
- **Модель:** локальный Qwen-чекпоинт (`PI_MODEL=qwen3.8-flash-next-gyro-gyro-s`,
  provider `strata`, OpenAI-compatible endpoint на локальной машине), reasoning medium.
- **Контекст:** 64k токенов; лимит ответа owner'а 8192 токена, поэтому большие правки идут по
  частям (1–2k токена на генерацию, проверка между шагами).
- **Процесс:** Discovery (аналитик/PM) → Implementation (dev) → Verification (QA), роли описаны в
  `AGENTS.md`; решения фиксируются в `PROJECT_MEMORY.md` (ADR) и `Roadmap.md`; новый функционал
  сначала проверяется isolated-прототипом в `lab/`, потом переносится в `src/`; каждый шаг
  сопровождается численной проверкой, без плейсхолдеров и «зеленения» тестов.

## Ограничения и open tasks

- Производительность на ~1M граней не замерена (задача E5.T4: счётчик кадров/сек на большой модели).
- Windows-only сборка: линк-флаги и `GLFW_EXPOSE_NATIVE_WIN32` специфичны для mingw.
- Рендер из `WM_PAINT` (repaint во время drag'а окна) требует сериализации с асинхронной загрузкой модели.
- `deps/imgui/build` содержит бинарные артефакты сборки (vendored); их можно пересобрать `make imgui`.

## Лицензия

MIT (см. `LICENSE`). Vendored'ные зависимости: Dear ImGui (MIT), GLFW (zlib).
