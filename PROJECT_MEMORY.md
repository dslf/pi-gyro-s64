# PROJECT_MEMORY.md — Память проекта

**Назначение:** глобальный технический контекст. Один источник правды для архитектуры, стека,
принятых решений и особенностей окружения. Обновляется после каждого утверждённого этапа
(после одобрения пользователем; авто-коммиты запрещены).

---

## 1. Что это за проект

Десктопный 3D-вьюер форматов `.obj` (позже `.mtl` + текстуры): загрузка модели, вращение/пан/zoom
мышью, рендер в окно OpenGL 3.3 core, сохранение кадра в файл, headless-режим для тестов.

**Код:** `src/` (C11), `shaders/` (GLSL), `tests/` (PS1 + Python), `Makefile` (сборка одной командой).
Корень: `P:/vibeshe2/Opencoder/pi/pi-gyro-s64`.

## 2. Окружение (проверенные факты, не предположения)

| Компонент | Факт |
|---|---|
| gcc | 14.2.0 (`C:\Apps\w64devkit\bin\gcc.exe`), сборка `-O2 -std=c11 -Wall -Wextra` — чисто |
| GLFW | статический `lib/libglfw3.a` + заголовки; контекст 3.3 core реально создаётся |
| OpenGL | `lib/libopengl32.a`; версия выводится: `3.3.0 NVIDIA 616.92` |
| GPU | NVIDIA RTX 4070 Ti (основная), AMD iGPU резерв |
| Python | есть, используется для проверки BMP (`tests/check_images.py`) |
| Инструменты | `make`, `cmake`, `ninja`; нет clang/cl/vcpkg/glad/cglm/stb |
| Git | **репозиторий создан** (`git init`, identity `pi-agent`/`agent@local`); remote отсутствует; `.gitignore` + `.gitattributes` (`* text=auto eol=lf`) |
| pi | окружение pi.dev; session-recall через `@sting8k/pi-vcc` (package.json — только tooling, не приложение) |

## 3. Архитектура (текущая база)

- `obj.c` — парсер OBJ: все формы граней (tri/quad/poly), отрицательные и relative индексы,
  `vt`/`vn`, отсутствие `vn` → нормали вычисляются локально, `p`-точки игнорируются,
  авто-определение up-axis, авто-масштаб/центрирование по bounding radius.
- `vmath.c/h` — свои `mat4/vec3/mat3`: `m4_perspective`, `m4_ortho`, `m4_map_point`, `m3_normal`.
- `gl.c/h` — ручной загрузчик GL (~50 слотов): `glfwGetProcAddress` → `wglGetProcAddress` → `opengl32.dll`.
- `camera.c/h` — orbit-камера: yaw/pitch/dist/fov/ortho; framing по bounding sphere с учётом аспекта;
  near/far “обнимают” модель (`dist ± fit`) — иначе perspective и ortho расходятся по глубине.
- `mesh.c/h` — загрузка VBO/VAO на GPU + хранение CPU-версий (`cpu`) для picking/подсветки.
- `render.c/h` — Blinn-Phong, двустороннее освещение, gamma, wireframe, offscreen FBO,
  **подсветка выбранной грани** (отдельный VBO с barycentric-attribute, без геометрии в 2×).
- `img.c/h` — BMP 24bpp из FBO.
- `main.c` — CLI (`--render`, `--selftest`, `--wire`, `--ortho`, `--fov`, `--yaw`, `--pitch`, `--size`),
  интерактив (LMB orbit, RMB pan, wheel zoom, Ctrl+wheel fov, O/W/R/S/Esc), `pick_triangle()`,
  самодостаточные selftest-ы (28 проверок, все зелёные).

## 4. Принятые решения (ADR-краткая форма)

1. **C + OpenGL 3.3 core, без внешних библиотек кроме GLFW** — сборка одной командой, reproducibility.
2. **Своя математика** вместо cglm — ~100 строк, ноль зависимостей, полный контроль над ortho/persp.
3. **Освещение Blinn-Phong** (не PBR) — достаточно для вьюера; PBR отдельным этапом.
4. **Подсветка selected triangle вне main VBO** (barycentric + `uHighlight`) — main-буфер не меняется
   при выборе; снятие выбора = один uniform, не пере-загрузка геометрии.
5. **Глубина picking = monotone NDC z** (`m4_map_point`) — одинаково работает в perspective и ortho;
   nearest wins независимо от порядка треугольников в файле.
6. **Конвенции мыши проверяются, а не угадываются**: сравнение проекций граней до/после drag.
7. **Тесты = selftest (C) + регрессия (PS1/Python)**: и геометрия, и пиксели, и коды возврата.
8. **VCS-раскладка**: трекаются только продукт и документы — `src/`, `shaders/`, `assets/`,
   `tests/*.ps1|*.py`, `Makefile`, `PLAN.md`, `PROJECT_MEMORY.md`, `Roadmap.md`, `AGENTS.md`,
   `.obj`-ассеты. Бинарники, кадры, зонды, `node_modules/` — исключены.
9. **Вывод тестов — в `tests/out/`** (git-ignored): регрессия больше не пишет BMP в корень;
   каталог очищается перед каждым прогоном, файлы остаются для визуальной проверки.

## 5. Текущий статус

- M0–M6b: парсер, GL-загрузчик, камера/framing, шейдер, wireframe, скриншот, offscreen BMP, CLI — готово.
- M7: выбор граней (hover outline, click pin/unpin) + конвенции мыши + picking + outline-пиксельный
  тест — готово, `viewer.exe --selftest` → PASS (28 проверок), `make test` → `tests: PASS`.
- Зафиксировано: drag-конвенция корректна в обеих проекциях (perspective и ortho) и fov 20/60/80.
- Артефакты: кадры регрессии — `tests/out/` (git-ignored), исследовательские зонды и их логи —
  `trash/probes/`, `trash/logs/` (git-ignored). В продукном каталоге артефактов нет.

## 6. Открытые решения и риски

- **Формат**: только `.obj` или также `.glb/.gltf`? (glTF потребует свой парсер или vendored `cgltf`).
- **MTL/текстуры**: нужен `stb_image.h` (vendor, один header) или свой minimal PNG/JPEG декодер?
- **Git**: локальный репозиторий без remote; история начинается с одного коммита (базовый снимок
  discovery-фазы). Пуш/PR возможны только после добавления remote и одобрения пользователя.
- **Артефакты вне VCS**: политика зафиксирована — вывод тестов в `tests/out/`, зонды в `trash/`;
  новые пробы не должны появляться в корне или в трекаемых каталогах.
- **Машинные зависимости**: Windows-only (opengl32/wgl). Кроссплатформенность не проверялась.
- **Производительность**: picking = CPU-обход всех треугольников на каждый hover (3750 tri ≈ 0.3 ms,
  но для 1M tri нужен BVH/облок). Пока не критично, риск зафиксирован.
- **Пиксельные тесты чувствительны к GPU** (антиалиасинг, gamma) — пороги задаются как относительные.

## 7. Соглашения процесса (см. AGENTS.md)

Прототипы → `lab/<feature>/`; чек-лист на русском перед шагом; саммари на русском;
никаких плейсхолдеров; понятные ошибки + exit code ≠ 0; тесты на каждый шаг;
отчёт “цель / сделано / как проверено”; коммит **только** после явного одобрения.
