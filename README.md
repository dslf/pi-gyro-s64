# pi-gyro-s64

**OBJ / GL viewer** — интерактивный просмотрщик Wavefront `.obj` на OpenGL 4.6 core, написанный на
C11 без внешних библиотек кроме GLFW и Dear ImGui. **Проект в стадии активной разработки**: это
рабочая дев-ветка, релизов и tag'ов нет, API и hotkeys могут меняться.

![Viewer: shaded Utah teapot on a sky-lit background, three HUD panels (stats, settings widgets,
keyboard hints)](docs/viewer-window.png)

На скриншоте: реальное окно 1280×800 на дефолтных настройках (`fov 60 deg`, перспектива, shading,
`sky light` и `outline` включены, фон панелей 0.94, текст 13.26 px — ключ `hud_font_px`). Utah teapot (1250 граней),
шейдинг с light'ом, солнцем и небом, маска под текстом в трёх панелях HUD: статистика (слева сверху —
FPS, координаты и скорость камеры), панель виджетов Settings (слева — от «Render mode» до
«Settings width», целиком без скролл-бара при высоте 800 px; ☰ сворачивает её в одну строку
Collapsing header),
подсказки hotkeys (внизу по центру). Контур силуэта включён, грань под курсором не выбрана —
подсветка грани не показана.

## Что делает

- Загрузка `.obj` (triangulation, позиции + нормали, bbox-центрирование и fit по bounding radius).
- Orbit / pan / zoom camera, перспектива и ортография (`O`), auto-fit при загрузке.
- Шейдинг (ambient + diffuse + specular), билинейный filter для outline-сэмплеров.
- **Outline всего силуэта** без readback: edge detection + polygon-offset (`GL_EQUAL`), 2-pass.
- **Picking по face id**: `id`-атрибут + `discard` (главный pass) и `glReadPixels` для pick-pass;
  nearest-first по глубине. `Tab`/`Shift+Tab` — шаг по граням в порядке файла (циклический).
- **Идентификация грани в HUD**: ordinal авторской грани, 1-based номера вершин из `f`-строки,
  нормаль грани и нормали вершин (авторские `vn` либо computed, с оценкой отклонения).
- **Подсветка выбранной грани** без readback: uniform + `discard`.
- **HUD на Dear ImGui** (три панели: статистика, подсказки горячих клавиш, панель настроек),
  текст из атласа, контраст по WCAG. Панель настроек берёт мышь над собой: drag и picking
  там не срабатывают.
- **`settings.ini` рядом с бинарём** — 24 ключей (fov, дистанка, орто-масштаб, яркости
  света/неба/поверхности/фона, горизонт неба, azimuth солнца, контур и его видимость,
  размер панели и её видимость, wireframe, орто, видимость карточек, размер шрифта HUD в пикселях
  (дефолт 13.26 px, диапазон 6…32 px; атлас печатается при базовых 17 px, ключ — прямой px,
  legacy-ключ `hud_font_scale` читается как multiplier над 17 px и больше не пишется),
  плотность фона панелей, ширина подсказок и карточки). Один источник правды: таблица
  `SETTINGS_FIELDS[]` — ключ, диапазон, дефолт и виджеты из одной строки. Файл перезаписывается
  после отпускания виджета, после изменения с клавиатуры и при закрытии окна; файл, написанный
  старой версией (с legacy-ключом `hud_font_scale`), переписывается в текущие ключи при первой
  загрузке, поэтому legacy-ключ живёт на диске ровно до одного запуска; читается только
  в интерактиве (`--render`/`--selftest` остаются на дефолтах), перечитать его «на горячую»
  нельзя — только перезапуск.
- **Панель виджетов (`P`)** стоит под карточкой статистики, ширина по умолчанию 460 px —
  при 320 px подписи виджетов обрезались («orthographic rays», «sky from above»), поэтому
  дефолт поднят до ширины, где весь текст читается. Если места до подсказок меньше,
  чем занимают виджеты, панель упирается в это место и получает scrollbar: колесо над ней
  прокручивает список виджетов, а не зумит камеру; на совсем узком кадре панель не рисуется.
  Панель двигается за заголовок (штатный move ImGui), позицию и ширину она возвращает в
  `settings.ini`; активный drag (виджет, scrollbar, заголовок) держит камеру вне игры, пока
  кнопка нажата, — в том числе когда курсор вышел за пределы окна. Каждый проход `--selftest`
  пишет кадр HUD в `build/settings_panel.bmp`, чтобы layout смотрелся, а не только мерился.
  Сворачивание в вкладку и скрытие панели — отдельная проверка в `--selftest`: свёрнутая панель
  по-прежнему принимает mouse-over и кадр не прерывается (контракт ImGui: `End()` вызывается даже
  когда `Begin()` вернул false).
- **Resizing окна рисуется сразу**: framebuffer size перечитывается в кадре, не в callback'е
  (`glfwPollEvents` на Windows во время drag'a окна не вызывается — repaint идёт из `WM_PAINT`).
- Offscreen-рендер в FBO и запись BMP без окна (`--render`, плюс `--yaw/--pitch/--fov/--ortho` для
  воспроизводимых кадров).
- **`--selftest`**: численные метрики вместо «на глаз» — coverage силуэта (> 2%), outline-пиксели
  (2–35% кадра, контраст ≥ 600), pick-пиксель в 20 px от центра, identity грани (ordinal +
  номера вершин + нормали), Tab-шаг, отсутствие чёрных пикселей после
  resize, gaps HUD ≤ 2 px, wrap подсказок, roundtrip `settings.ini` и поведение панели виджетов
  (гейт мыши, rect, контраст, room+scrollbar, сворачивание Collapsing header, размер глифа HUD в px
  и миграция legacy-ключа `hud_font_scale`). 11 разделов, 84 проверка, пороги относительные,
  чтобы пережить другой GPU/AA.

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
make            # viewer.exe в корень репозитория (GCC + mingw-w64; линк GLFW/imgui статически)
make test       # selftest + tests/run_tests.ps1
```

Запускать нужно **из корня репозитория**: `assets/` и `shaders/` резолвятся относительно текущего
каталога (не относительно бинаря), поэтому `./viewer.exe` из `build/` не найдёт модель.

```sh
./viewer.exe                     # Utah teapot
./viewer.exe assets/cube.obj     # свой файл
./viewer.exe --render assets/cube.obj tests/out/frame.bmp 800 600   # offscreen BMP без окна
./viewer.exe --selftest          # численные проверки: 11 разделов, 82 строка ok, итог selftest: PASS
```

### Управление

| Клавиша | Действие |
|---|---|
| ЛКМ drag | orbit (yaw/pitch, pitch clamp ±85°). Над панелью настроек мышь принадлежит виджетам; активный drag держит камеру вне игры, пока кнопка нажата, даже если курсор вышел за окно |
| ЛКМ drag за заголовок панели | двигает панель настроек; позиция возвращается в `settings.ini` |
| ПКМ drag | pan (и скрывает панель настроек). Над панелью — ширина панели |
| Колесо | zoom (clamp по размеру модели). Над панелью — прокрутка списка виджетов; `Ctrl` + колесо там же меняет значение виджета, а не fov |
| `Ctrl` + колесо (вне панели) | viewing angle (fov), не дистанция |
| `[` / `]` | fov −5° / +5° (8–90°) |
| `=` / `-` | zoom +1 / −1 |
| `W` | wireframe overlay on/off |
| `O` | перспектива ↔ ортография |
| `R` | reset камеры (auto-fit по bounding radius) + снятие pin + дефолты настроек |
| `P` | панель настроек: показать / скрыть |
| `S` | screenshot BMP текущего кадра в `screenshot.bmp` |
| `H` | скрыть / показать информационные панели (карточка статистики и подсказки) |
| `Tab` над панелью настроек | свернуть панель в узкую вкладку (треугольник в заголовке делает то же); `Tab` над вкладкой — развернуть |
| hover | outline граней под курсором (nearest-first по глубине) |
| ЛКМ click | pin грани; повторный click на той же — снять pin |
| `Tab` / `Shift+Tab` | шаг по граням в порядке файла (циклический) |
| `Esc` / `Q` | выход |

## Проверка

Публичная проверка — `make test`: сборка, `./viewer.exe --selftest` (11 разделов, 82 проверок,
exit 0), `tests/run_tests.ps1` (BMP-метрики: coverage, outline, picking, resize, wireframe,
offscreen). Проверки числовые; пороги относительные.
Состав проверок сверяется из вывода, а не «из головы»:

```powershell
./viewer.exe --selftest | Select-String '^selftest: '   # 11 заголовков разделов + итоговая строка
./viewer.exe --selftest | Select-String '^\s+ok ' | Measure-Object   # 84 проверка, FAIL нет
```

`python tests/py/settings_ini_smoke.py` сверяет
`settings.ini` с независимым Python-парсером: состав ключей (24), формат `key = value`,
`true/false` для булевых, три числа для RGB и все значения в своих диапазонах; битый файл
даёт `FAIL` с номером строки и причиной.

## Структура

```
src/         продукт (C11): obj, camera, vmath, mesh, shader, render, gl, img, hud, settings, main
shaders/     GLSL 460 core
assets/      тестовые модели (cube, teapot)
tests/       PS1 + Python проверка пикселей
docs/        скриншот для README + отчёты шага (docs/reports/)
build/       замороженный scratch (git-ignored): бинаря продукта там нет, только временные файлы
             сборки (`*.o`) если понадобятся
lab/probes/  изолированные зонды ABI/GL/HUD — см. lab/probes/README.md
deps/imgui/  vendored Dear ImGui (MIT) + libcimgui.a
Roadmap.md   фазы E0…E7 и статусы
PROJECT_MEMORY.md  решения и факты окружения (ADR)
AGENTS.md    регламент разработки
```

## Как разрабатывается

Проект написан **ИИ в локальном окружении**, без ручной правки кода:

- **Harness:** [pi.dev](https://pi.dev) coding agent (TUI, сессии в `~/.pi/agent/sessions`).
- **Модель:** локальный Qwen-чекпоинт (provider `strata`, OpenAI-compatible endpoint на
  локальной машине).
- **Процесс:** новый функционал сначала проверяется isolated-прототипом в `lab/`, потом переносится
  в `src/`; каждый шаг сопровождается численной проверкой (пиксели, coverage, delta), без
  плейсхолдеров и без «зеленения» тестов; решения и факты окружения фиксируются в
  `PROJECT_MEMORY.md`, статусы работ — в `Roadmap.md`, регламент разработки — в `AGENTS.md`.

## Ограничения и open tasks

- Производительность на ~1M граней не замерена (задача E5.T4: счётчик кадров/сек на большой модели).
- Windows-only сборка: линк-флаги и `GLFW_EXPOSE_NATIVE_WIN32` специфичны для mingw.
- Рендер из `WM_PAINT` (repaint во время drag'а окна) требует сериализации с асинхронной загрузкой модели.
- `deps/imgui/build` содержит бинарные артефакты сборки (vendored); их можно пересобрать `make imgui`.

## Лицензия

MIT (см. `LICENSE`). Vendored'ные зависимости: Dear ImGui (MIT), GLFW (zlib).
