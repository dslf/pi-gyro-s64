# PLAN — 3D-рендер OBJ на C + OpenGL 3.3

## 1. Цель и критерии готовности

**Цель:** desktop-вьюер, который открывает `.obj` (и позже `.mtl` + текстуры), вращает/масштабирует мышью, рендерит в окно и может сохранить кадр в файл.

**Definition of Done:**
1. `make` собирает `viewer.exe` одной командой, без установки зависимостей.
2. `viewer.exe cube.obj` показывает модель; ЛКМ — вращение, колесо — zoom, `Esc` — выход.
3. Модель всегда в кадре независимо от размера OBJ (авто-центрирование + авто-масштаб).
4. Нормали корректны: гладкие для `vn`, вычисленные локально если `vn` нет.
5. Двусторонние грани (back faces) не чёрные — двустороннее освещение.
6. `viewer.exe --render cube.obj out.bmp 800 600` пишет BMP без окна, файл проверяется скриптом.
7. Ошибки (нет файла, битый OBJ, компиляция шейдера) — понятное сообщение и код возврата ≠ 0.

## 2. Проверенное окружение (факт, не предположение)

| Компонент | Статус |
|---|---|
| gcc | 14.2.0, `C:\Apps\w64devkit\bin\gcc.exe` |
| GLFW | `include/GLFW/glfw3.h`, `lib/libglfw3.a` (статический), `lib/glfw3.dll` |
| OpenGL | `lib/libopengl32.a`, `include/GL/gl.h` |
| Контекст | 3.3 core profile создан и напечатан: `3.3.0 NVIDIA 616.92` |
| GPU | NVIDIA RTX 4070 Ti (основной), AMD iGPU — резерв |
| Сборка | `make`, `cmake`, `ninja` доступны |
| Нет | clang, cl, vcpkg, glad.c, cglm, stb_image.h |

Сборка-пробник подтверждена:
```
gcc probe.c -o probe.exe -lglfw3 -lopengl32 -lgdi32 -lwinmm
```

## 3. Архитектура

```
src/
  main.c        GLFW-окно, главный цикл, ввод, CLI, resize
  gl.c/.h       мини-загрузчик GL-функций + glCheckError()
  shader.c/.h   read_all, compile, link, лог ошибок
  vmath.c/.h    vec3, mat4, mat3: perspective, look_at, mul, normal matrix
  camera.c/.h   orbit-камера: yaw/pitch/dist/target/fov/ortho, framing по bounding radius
  obj.c/.h      парсер OBJ ->Mesh (позиции+нормали+uv, fan-триангуляция, up-axis)
  mesh.c/.h     VAO/VBO, upload, free
  render.c/.h   конвейер: uniforms, draw, FBO offscreen, glReadPixels
  img.c/.h      RGBA -> BMP
shaders/
  model.vert    GLSL 330
  model.frag    Blinn-Phong, sky ambient, specular, gamma
assets/
  cube.obj, quad_vvn.obj, nov.obj, bad.obj, bad_index.obj
  checkered.png (появится на M8)
tests/
  run_tests.ps1        сборка + selftest + рендеры + коды возврата
  check_images.py      проверка пикселей BMP (PIL/numpy)
Makefile
```

**Минимальные зависимости:** только GLFW (уже в devkit). Загрузчик GL, математика, OBJ, BMP — свои, чтобы не тянуть glad/cglm/stb на старте. `stb_image.h` добавляется только на этапе текстур.

**Поток данных:**
CLI → obj.c (парсинг) → mesh.c (GPU-буферы) → camera.c (матрицы) → render.c (draw) → swapBuffers / glReadPixels → img.c.

## 4. Этапы (каждый проверяем до следующего)

**M0 — Пробник тулчейна** ✅ уже сделано: контекст 3.3 + RENDERER напечатаны.

**M1 — Окно и пустой кадр** ✅
- `glfwInit`, core 3.3 hints, окно 800x600, `glfwCreateWindow`, `MakeContextCurrent`.
- `glClear` + `glClearColor`, swap, 60 FPS, callback resize (`glfwGetFramebufferSize` → `glViewport`).
- Проверка: серое окно, resize не ломает картинку.

**M2 — Загрузчик GL + шейдер + треугольник** ✅
- `gl.c`: таблица из ~30 функций, `glfwGetProcAddress`, fallback `wglGetProcAddress`; без него — понятная ошибка.
- `shader.c`: compile с `GL_COMPILE_STATUS` и печатью инфолога.
- VAO/VBO, `model.vert/frag`, один треугольник.
- Проверка: цветной треугольник + печать версии GL.

**M3 — Математика и камера** ✅
- `mat4_perspective(fov 45°, aspect, near 0.01, far 100)`, `look_at`.
- Orbit: yaw/pitch clamp ±89°, dist clamp, scroll zoom, правая кнопка — pan.
- Проверка: треугольник вращается/зумится, нет искажений при resize.

**M4 — Парсер OBJ** ✅
- Токены: `v`, `vt`, `vn`, `f` (формы `v`, `v/vt`, `v/vt/vn`, `v//vn`), `g`/`o`/`usemtl` (игнор), комментарии, пустые строки.
- Полигоны → треугольники (fan), отрицательные индексы (OBJ 1-based) → 0-based.
- Нет `vn` → computeFaceNormal + накопление по вершинам.
- Нет `vt` → uv = 0, шейдер использует нормаль-цвет.
- AABB → центруем, нормализуем масштаб (самый длинный спан = 1), near/far и дистанция камеры от радиуса.
- Up-axis: OBJ по умолчанию Y-up. Если по одной из осей `min ≈ 0`, а `max > 0` (модель стоит на «земле»,
  типичный Z-up экспорт Blender/3ds Max) — эта ось становится +Y; иначе чистый Y-up.
  По длинному спану ось не определяют: у чайника ширина (6.4) больше высоты (3.15), и он лёг бы на бок.
- Проверка: `--selftest` на синтетических OBJ + `utah_teapot.obj` (1250 треугольников).

**M5 — Освещение** ✅
- Blinn-Phong: 1 directional (позиционный в мировых координатах) + hemispheric ambient; specular.
- Нормаль-матрица = transpose(inverse(model3x3)).
- Двусторонность: в фрагменте инвертировать нормаль к направлению на камеру (`gl_FrontFacing`).
- Проверка: куб читается по граням, тёмная сторона не полностью чёрная.

**M6 — Ввод и файлы** ✅
- CLI: `viewer.exe <file.obj>`, `--wire`, `--size W H`, `--ortho`, `--fov <deg>`.
- Хоткеи: `W` wireframe, `R` сброс камеры (сохраняет запрошенные `--fov`/`--ortho`), `S` скриншот bmp,
  `O` переключение perspective/ortho, `[` / `]` угол обзора ±5°, `Ctrl+колесо` — угол вместо zoom, `Esc` выход.
- Заголовок окна показывает проекцию, FOV и дистанцию глаза.
- Открытие файла: аргумент; позже кнопка через `GetOpenFileNameW` (comdlg32).
- Проверка: «не найден файл» → сообщение + exit 2 (битые аргументы — тоже 2, GL/IO-сбой — 1).

**M6b — Проекция и угол обзора** ✅
- `camera_reset(fit)`: дистанция глаза из радиуса модели и FOV — `fit / sin(fov/2) * 1.08`, модель никогда не обрезается.
- `camera_projection()`: перспектива, либо ортография с `half = dist * tan(fov/2)` — при переключении `O`
  размер объекта на экране не меняется.
- `camera_set_fov()`: угол меняется с сохранением видимого размера (`dist *= sin(fov_old/2) / sin(fov_new/2)`),
  диапазон 8–90°; широкий угол = глаз ближе = сильнее перспективные искажения.
- `camera_set_aspect()`: при resize дистанция умножается только на разницу «потребности» аспекта —
  история зума сохраняется, экстремальные 100x400 / 400x100 не обрезают модель.
- Near/far прилегают к модели (`dist - fit*4`, `dist + fit*6`) → больше точности глубины на всех зумах.
- Проверка: `--selftest` секция projection + `tests/check_images.py projection`.

**M7 — Offscreen-рендер (автоматическая проверка)** ✅
- FBO (RGBA8, depth24), `glReadPixels`, запись BMP в `img.c`.
- Режим: `viewer.exe --render in.obj out.bmp 800 600`.
- Скрипт проверки: `tests/run_tests.ps1` + `tests/check_images.py` (PIL/numpy).
- Проверка: `make test` — сборка (в т.ч. `-Werror -fsyntax-only`), selftest, рендеры, коды возврата, пиксели.

**M8 — Текстуры и MTL**
- `stb_image.h` + `stb_image_write.h` (vendored, один header).
- `.mtl`: `map_Kd`, `Ka/Kd/Ks`, `Ns`, `d`. Разрешение относительных путей относительно OBJ.
- Если текстуры нет — цвет материала; если нет MTL — дефолтный серый.
- Проверка: OBJ с checker-текстурой показывает текстуру; `--render` пишет её.

**M9 — Полировка (опционально)**
- FPS в заголовке окна, сетка/оси, множественные объекты (`--models a.obj b.obj`), tone mapping, слайдер света мышью.

## 5. Риски и решения

| Риск | Решение |
|---|---|
| GL-функции 3.3 не загрузились через `wglGetProcAddress` | двойной путь: `glfwGetProcAddress` → `wglGetProcAddress`; при отсутствии — явная ошибка со списком недостающих имён |
| OBJ без `vn`/`vt`, с `v//vn`, с полигонами >3 вершин | парсер покрывает все формы; нормали считаются; fan-триангуляция |
| Модель гигантская/микроскопическая, камера «в нутре» | авто-масштаб в юнит-куб, near/far от радиуса модели |
| Двойной GPU (NVIDIA + AMD iGPU) | по умолчанию выбирается дискретный; если нужен iGPU — запуск через флаг Windows graphics settings, не кодом |
| Двойной FB-size vs window-size (DPI) | использовать `glfwGetFramebufferSize`, не `glfwGetWindowSize` |
| BIG-endian/мис-align в BMP | BMP пишется Little-Endian, фиксированный stride 4 |
| Нет `.mtl`/текстуры | graceful fallback: цвет материала + нормаль-цвет |
| Висящий `stb_image.h` на старте | добавляется только на M8, до этого ноль внешних зависимостей |
| Магические байты BMP записаны little-endian `put16(0x424d)` | BMP — это байты `'B','M'` подряд; в `img.c` пишется строка `"BM"` |
| `glClear` не кодирует gamma (`GL_FRAMEBUFFER_SRGB` выключен), а шейдер кодирует | цвет фона хранится в `renderer.bg[]` уже gamma-encoded |
| Имена GL-функций из загрузчика | указатели названы `my_gl*`; вызовы везде через `my_gl*`, никаких макросов-алиасов |
| `glReadPixels` из default FBO | `glReadBuffer(GL_BACK)` (для FBO — `GL_COLOR_ATTACHMENT0`), иначе чёрный кадр |
| Заголовок `src/math.h` конфликтует с `<math.h>` | переименован в `vmath.h` / `vmath.c` |
| Камерная дистанция от «длинного спана» обрезает модель при повороте | framing по bounding radius: `r / sin(fov/2)` + margin |
| Широкий FOV обрезает модель или object «улетает» при смене угла | дистанция пересчитывается так, что видимый размер постоянен: `dist *= sin(fov_old/2)/sin(fov_new/2)` |
| Переключение на ortho меняет размер объекта | ortho-frustum строится из того же `dist` и `fov`: `half = dist * tan(fov/2)` |
| Экстремальный аспект окна (100x400) обрезает модель | `camera_set_aspect()` умножает дистанцию на ratio аспектной «потребности», а не сбрасывает зум |
| Параметр PowerShell-функции назван `$args` (автоматическая переменная) → вызов `viewer.exe` без аргументов, открылось интерактивное окно и тест завис | `Run-Viewer` использует `Start-Process -ArgumentList <строка>` с перенаправлением stdout/stderr; stderr не роняет скрипт |
| Не загруженные слоты (например `glTexImage2D`) → nullptr → краш | загрузчик печатает список недостающих имён; `fb_create` использует `glTexImage2D`/`glTexParameteri` |

## 6. Тесты

1. **Unit (без окна):** `viewer.exe --selftest` — парсер на синтетических OBJ:
   - `cube.obj` (12 треугольников, есть `vn` и `vt`)
   - `quad_vvn.obj` (`v//vn`, 2 треугольника)
   - `nov.obj` (без нормалей → нормали вычислены, `|n| = 1`)
   - `bad.obj` (мусор → ошибка, не краш), `bad_index.obj` (битые индексы → «no faces found»)
   - `utah_teapot.obj` (1250 треугольников из 538 quad + 174 triangle, `vn` + `vt`)
   Печатаются triangles/verts/radius/span —.span виден сразу, поэтому ориентация модели проверяется числом.
2. **Компиляция шейдеров:** `--selftest` компилирует `model.vert/frag` на реальном контексте и печатает лог.
3. **Offscreen:** `--render` создаёт BMP; `tests/check_images.py` проверяет размер, режим RGB,
   долю пикселей, отличных от фона, что силуэт не обрезан краем кадра, aspect силуэта,
   «узкая крышка vs пузо» (ориентация чайника), наличие светлых и тёмных пикселей,
   что wireframe даёт заметно меньше пикселей, чем shading.
   Дополнительно: `check_images.py framing <bmp>` — экстремальный аспект (ничего не обрезано),
   `check_images.py projection <ortho> <persp> <wide>` — ortho не масштабирует глубину
   (base/lid 2.59 > perspective 1.64), перспектива сильнее широкого FOV (1.64 > 1.12),
   широкий FOV покрывает меньше пикселей при том же framing (11.1% < 13.8%).
4. **Коды возврата:** несуществующий файл, размер < 16x16, отсутствующий путь вывода, `--fov 120` → exit 2;
   `--ortho --fov 15` и `--selftest --ortho` → exit 0.
5. **Ручной:** resize окна, wireframe (`W`), reset камеры (`R`), скриншот (`S`), `O` и `[`/`]`.

Запуск: `make test` или `powershell -File tests/run_tests.ps1` (exit 0 = всё зелено, ~17 c).

Вывод `--selftest` (реальный):
```
selftest: OBJ parser
  ok   assets/cube.obj:        12 tris, 36 verts, radius 0.866, span 1.00 x 1.00 x 1.00
  ok   assets/quad_vvn.obj:     2 tris,  6 verts, radius 0.559, span 1.00 x 0.50 x 0.00
  ok   assets/nov.obj:          2 tris,  6 verts, radius 0.707, span 1.00 x 1.00 x 0.00
  ok   assets/bad.obj:          no vertices found
  ok   assets/bad_index.obj:    no faces found (1 malformed)
  ok   utah_teapot.obj:      1250 tris, 3750 verts, radius 0.518, span 1.00 x 0.62 x 0.49
selftest: shader + offscreen pixels
  ok   rendered 39% of 128x128
  ok   coverage: persp45 39.6% | fov15 44.2% | fov80 33.1% | ortho 38.0%
  ok   wide fov differs from narrow by 11.2% (> 1%)
  ok   ortho differs from perspective by 1.6% (> 1%)
selftest: projection
  ok   fov 8 deg fits aspect 1.00/1.78/0.50 (eye 30.96 from centre)
  ok   fov 45 deg fits aspect 1.00/1.78/0.50 (eye 5.64 from centre)
  ok   fov 90 deg fits aspect 1.00/1.78/0.50 (eye 3.05 from centre)
  ok   perspective foreshortens: near x 0.399 > far x 0.300 (fov 45 deg)
  ok   orthographic keeps size with depth (dx 0.00000)
selftest: PASS
```

Профили силуэта (ширина на доле высоты от низа) — подтверждают проекции:
```
fov 45 : 0.05:51 0.15:98 0.25:178 0.4:199 0.55:194 0.7:174 0.85:139 0.95:92
fov 80 : 0.05:62 0.15:95 0.25:167 0.4:184 0.55:185 0.7:164 0.85:121 0.95:75   (ближний край шире)
ortho  : 0.05:23 0.15:85 0.25:156 0.4:186 0.55:177 0.7:149 0.85:128 0.95:85   (без масштабирования глубиной)
```

## 7. Порядок реализации и команды

```make
CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra
LDFLAGS = -lglfw3 -lopengl32 -lgdi32 -lwinmm

viewer.exe: $(SRC) $(HDR) $(SH)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

test: viewer.exe
	powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1
```

Одиночная команда сборки, если нужен только бинарь:
```
gcc -O2 -std=c11 -Wall -Wextra src/*.c -o viewer.exe -lglfw3 -lopengl32 -lgdi32 -lwinmm
```

**Порядок:** M1 → M2 → M3 → M4 → M5 → M6 → M7 → M8 → M9. Каждый этап = коммит + скриншот/вывод проверки; не переходить к следующему, пока текущий не виден/проверен.

## 8. Статус и что осталось

**Готово (M0–M7):** парсер OBJ (все формы граней, отрицательные индексы, `p`-точки игнорируются,
авто up-axis, авто-масштаб), загрузчик GL (~50 слотов, `glfwGetProcAddress` + `wglGetProcAddress` +
`opengl32.dll`), математика (`vmath`, есть `m4_ortho` и `m4_map_point`), orbit-камера с FOV и ortho,
framing по bounding radius с учётом аспекта, Blinn-Phong с небесным ambient и gamma, двустороннее
освещение, wireframe, скриншот, offscreen FBO + BMP, CLI (`--wire`, `--size`, `--ortho`, `--fov`)
и коды возврата, `make test` — зелёный (exit 0).

M7 — выбор граней и мышь: hover обводит грань под курсором (nearest-first, учитывает глубину в
perspective и ortho), клик закрепляет её (повторный клик по той же грани снимает), перетаскание
камеры не отвязывает подсветку — она в координатах модели. Конвенция мыши проверена, а не угадана:
`drag_test_selftest` сравнивает проекции граней до/после drag и требует, чтобы грань, смотрящая на
камеру, ушла вправо при drag вправо и вниз при drag вниз (perspective + ortho, fov 20/60/80);
`mouse_pick_selftest` доказывает, что nearer wins по порядку файла, что pixel y сверху = верх модели,
что пустая область даёт -1; `outline_selftest` рисует подсветку в offscreen-FBO и считает пиксели
(148 px с обводкой, 0 после снятия).

**Осталось:**
- **M8 текстуры/MTL**: `stb_image.h`, `map_Kd`, `Ka/Kd/Ks`, `Ns`, `d`, относительные пути от OBJ.
- **M9 полировка**: FPS в заголовке, сетка/оси, несколько моделей, tone mapping.
- **Открытые вопросы** ниже.

## 9. Открытые вопросы (нужен ответ, чтобы зафиксировать дефолты)

1. Окно + интерактив — подтверждён? (сейчас план = интерактивный вьюер + offscreen-режим)
2. Текстуры/MTL — нужны с M8, или раньше/никогда?
3. Формат: только `.obj`, или добавить `.glb`/`.gltf` (потребует свой glTF-парсер или `cgltf`)?
4. Освещение: Blinn-Phong достаточно? (PBR — отдельный этап, больше шейдерного кода)
5. Математика: свои `mat4` (~100 строк) или vendored `cglm` (меньше кода, один header)?
