# Roadmap.md — Дорожная карта (PMBOK: Эпик → Пользовательская история → Задача)

**Назначение:** иерархия управления объёмом работ и статусы. Обновляется только после одобрения
пользователем (авто-коммиты запрещены). Статусы: ✅ готово и проверено · 🟡 в работе / частично ·
⬜ не начато · 🔵 требует решения (blocker на Discovery).

**Текущая фаза:** **Implementation** — базовая линия M0–M7 закоммичена; объём Epics E0–E3 утверждён,
реализация идёт по приоритету (E6 QA → E5 UX). Фаза switched по запросу пользователя (implementation
work approved); Discovery-блокеры E0.T6/E4.T2/E6.T3/E6.T4 остаются 🔵 (E7.T1 решён owner'ом,
см. ADR §4.20: бинарь продукта в корень, `build/` заморожен).
Базовая линия (M0–M7) уже существует в репозитории и зафиксирована в `PROJECT_MEMORY.md`.

---

## E0. Discovery и управление проектом (🟡 активен)

**Цель эпика:** утвердить рамки, стек, приоритеты и критерии готовности до реализации.

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E0.S1 | Как владелец, я хочу фиксировать контекст проекта, чтобы агенты не теряли решения | ✅ | `PROJECT_MEMORY.md`, `Roadmap.md`, `AGENTS.md` созданы |
| E0.T1 | Создать артефакты памяти/дорожной карты/инструкций | ✅ | файлы в корне, структура заполнена |
| E0.T2 | Опрос требований (5–7 вопросов: цели, стек, форматы, UX, QA, риски) | 🟡 | ответы зафиксированы в §Requirements |
| E0.T3 | Утвердить Project Scope + Definition of Done v1 | ⬜ | подписан список “готово/не готово” |
| E0.T4 | Решить: git init или работать без VCS | ✅ | `git init` + remote `origin` = https://github.com/dslf/pi-gyro-s64 (public, branch `main`, push выполнен); трекаются только источники и документы |
| E0.T5 | Решить: политика артефактов (`build/`, `lab/`, `artifacts/`, мусор в корне) | ✅ | вывод тестов в `tests/out/`, зонды в `trash/`, `.gitignore`/`.gitattributes` |
| E0.T6 | Определить приоритет: fidelity рендера vs интерактив vs тесты | 🔵 | ранжирование Epics E4–E7 |
| E0.T7 | Переход на OpenGL 4.6 core (контекст + шейдеры `#version 460 core`) | ✅ | `GL  : 4.6.0 NVIDIA 616.92`; сборка без warning'ов; selftest 26 ok / регрессия 59 ok, fail 0, exit 0 |
| E0.T8 | Регламент: большие правки выполняются частями (лимит owner 8192 tok; «большая» = ≥4096 tok; цель 1–2k с запасом) | ✅ | `AGENTS.md` §0.7, `PROJECT_MEMORY.md` §4.11 — план частей до правки, проверка между шагами |
| E0.T9 | Регламент терминала harness'а: многострочный текст — PowerShell here-string (`@' … '@` литерал, `@" … "@` с переменными); bash-heredoc не используется | ✅ | `AGENTS.md` §0.10, `PROJECT_MEMORY.md` §2 + §4.25; проверено в реальном shell: литерал не раскрывает `$scale`, `@"…"@` раскрывает (0.78), пайп программы в `python -` → `program from stdin`, данные через `python -c` → `'print(...)'`; `cmd <<'PY'` даёт ParserError |

## E1. Загрузка и representation модели (✅ готово, будет расширяться)

**Цель:** корректно читать `.obj` любой формы и гарантировать, что модель в кадре.

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E1.S1 | Открыть любой валидный OBJ и увидеть модель | ✅ | cube/teapot/quad рендерятся, coverage в тесте |
| E1.T1 | Парсер: tri/quad/n-gon, отрицательные и relative индексы | ✅ | selftest: 5 файлов, в т.ч. битые → понятная ошибка |
| E1.T2 | Нормали: `vn` гладкие, без `vn` — локальные | ✅ | selftest `quad_vvn` / `nov` |
| E1.T3 | Авто-центрирование + авто-масштаб (bounding radius) | ✅ | coverage 36–45% на разных размерах |
| E1.T4 | Ошибки: нет файла / битый OBJ / shader → сообщение + exit ≠ 0 | ✅ | регрессия: exit 2 на 4 плохих командах |
| E1.T5 | `.mtl` + текстуры (`map_Kd`, `Ka/Kd/Ks`, `Ns`, `d`, относительные пути) | ⬜ | → E4 |
| E1.T6 | Up-axis определяется по bbox и нормализуется в +Y (`obj.c`: X-up/Z-up/Y-up) | 🟡 | реализовано в `obj.c` (ротация detected-оси в +Y), молча не меняется; явный CLI-override не предусмотрен; отдельная численная проверка оси не добавлена — нужен selftest-блок на X-up и Z-up фикстурах |

## E2. Камера, проекция, framing (✅ готово)

**Цель:** модель всегда в кадре; perspective/ortho переключаемы без “прыжка”.

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E2.S1 | Модель целиком в кадре при любом аспекте и fov | ✅ | selftest fov 8/45/90 × aspects 1.00/1.78/0.50 |
| E2.T1 | Orbit-камера (yaw/pitch/dist) + zoom | ✅ | интерактив + selftest |
| E2.T2 | Ortho/perspective переключение без изменения размера на экране | ✅ | ortho vs persp coverage 1.2% diff |
| E2.T3 | near/far “обнимают” модель → корректный depth-order в ortho | ✅ | depth-тесты: near z < far z в обеих проекциях |
| E2.T4 | Foreshortening реально зависит от fov | ✅ | deviation от ortho растёт: 0.41 < 0.73 < 0.85 |

## E3. Выбор граней и мышь (✅ готово — цель текущего витка)

**Цель:** предсказуемая мышь (одинаковая конвенция в обеих проекциях) + выбор грани.

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E3.S1 | Drag вправо → видимая грань уходит вправо (perspective и ortho) | ✅ | `drag_test_selftest`, fov 20/60/80, обе проекции |
| E3.S2 | Drag вниз → видимая грань уходит вниз | ✅ | тот же selftest, worst dy +0.21 |
| E3.S3 | Hover обводит грань под курсором (nearest-first) | ✅ | `mouse_pick_selftest`: depth beats file order; pixel y top = model top; пусто → -1 |
| E3.S4 | Клик закрепляет грань; повторный клик снимает | ✅ | wiring в `handle_mouse`, pin/unpin + printf |
| E3.S5 | Обводка не отрывается при вращении/пане | ✅ | подсветка в координатах модели |
| E3.S6 | Пиксельное доказательство обводки | ✅ | offscreen FBO: 148 px с обводкой, 0 после снятия |
| E3.T1 | BVH/fast picking для крупных mesh (>1M tri) | ⬜ | риск подтверждён замером E6.T5: 39.9 ms на hover-луч при 2.26M tri (17.7 ms на 1M tri) — hover обновляется не чаще ~25 луч/с |

## E4. Материалы (MTL, текстуры) (⬜ следующий после утверждения)

**Цель:** физически правдоподобный вид модели с материалами из OBJ.

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E4.S1 | Модель с `map_Kd` показывает текстуру корректно | ⬜ | скриншот-тест: текстура ≠ uniform color |
| E4.T1 | MTL-парсер (`Ka/Kd/Ks/Ns/d/Ns/map_Kd`, relative paths) | ⬜ | selftest на 2 MTL-файлах |
| E4.T2 | Декодер изображений (stb_image vs свой) | 🔵 | решение на Discovery |
| E4.T3 | UV-интерполяция + sRGB gamma | ⬜ | тест:checker-паттерн не “съезжает” |

## E5. UX и полировка (⬜)

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E5.S1 | Понятный статус окна (fov, dist, проекция, FPS) + мгновенный repaint на resize | ✅ | окно + HUD: проекция, fov, dist, FPS, треугольники, выбор, wireframe (`src/main.c` → `hud_frame`); интерактивный цикл рисует каждый кадр (HUD живой без ввода), `fps` = кадры за окно 0.5 с, диаг-флаг `--idle-report <sec>`; resize: размер читается из окна каждый кадр (`sync_framebuffer_size`) и перерисовывается в том же кадре, плюс repaint в refresh-коллеке на WM_PAINT (модальный цикл drag'а не доходит до `glfwPollEvents`) — чёрных полос нет; selftest-блок «frame size follows the target immediately» (размер из окна, новая площадь не чёрная) + зонд `lab/probes/probe_resize.ps1` |
| E5.T5 | Dear ImGui (cimgui, C ABI) как основа HUD: FPS, подсказки горячих клавиш, читаемый контраст | ✅ | две read-only панели + третья панель виджетов (E5.T7), каждая облегает текст по обеим осям (`AlwaysAutoResize`, ширина не задаётся для read-only): статистика слева сверху, подсказки внизу по центру с лимитом ширины (`HUD_HELP_W`, перенос через `PushTextWrapPos`); selftest: rect панелей (stats 8..113 × 8..75 на фикстуре без выбора, help 8..412 × 198..312 на узком кадре), чернила вплотную к краям (худший пустой зазор 0 px на read-only панелях), контраст 6.57:1 / 6.32:1, coverage 11.93% / 11.86%, чернила вне панелей 0 px, без выбора карточка сжимается 520×67 → 349×48, перенос подсказок на узком кадре 760×116 → 404×114, детерминизм 0 px |
| E5.T6 | Сборка vendored `deps/imgui` (`libcimgui.a`) отдельным скриптом; продукт остаётся C11 | ✅ | `deps/imgui/build.ps1` (инкрементально, проверка exported C-символов), Makefile `imgui` target линкуется; продукт — C11 + `-static-libstdc++` |
| E5.T1 | HUD: подсказки, выбранный треугольник (id/normals) | ✅ | подсказки в отдельной панели внизу по центру (`HUD_PANEL_HELP`), выбор в панели статистики; `obj` хранит ordinal грани (`face_ids`) и 1-based номера вершин из `f`-строк (`vert_ids`) → `obj_triangle_identity`; нормаль грани через `obj_face_normal`, нормали вершин через `obj_vertex_normals` с различением авторских `vn` и computed (`OBJ_NORMALS_AUTHORED`), отклонение — оценка `obj_normal_deviation_deg`. Числа selftest: карточка растёт с выбором 45×21 → 96×57 (2.19×), строки карточки сверены с парсером (sel 8 из 12 → face 8/12, вершины 3,7,6, нормаль (-1.00,0.00,0.00) len 1.00, dev 90.0° computed; фикстура `vn` → dev ≤ 1e-3°), Tab/Shift+Tab циклический шаг по граням файла (`-1→0`, `11→0`, `0→11`, шаг 0 → грань 1), `R` снимает pin; подсказки и README hotkeys = реальный кэймап |
| E5.T4 | Глиф-атлас: `GDI TextOutW` → CPU маска глифов как источник HUD-текста | ✅ | перенесено в продукт (`src/hud.c`, `shaders/hud.vert/frag`); selftest: coverage 0.99%, centroid top-left, штрихи глифа 0..30% (mean 9.5%), ориентация top-down (direct 0.126 < mirrored 0.158), детерминизм 0 px. **Заменено ImGui (E5.T5): GDI-путь и `shaders/hud.*` удалены из продукта** |
| E5.T7 | Виджеты управления (ImGui): ползунки fov/света/поверхности/фона/HUD, переключатели режимов + `settings.ini` рядом с бинарём | ✅ | таблица `SETTINGS_FIELDS[]` — один источник правды для ключа, диапазона, дефолта и виджета (24 ключа); численная сверка: roundtrip всех ключей (save → load → идентичные значения, 0 расхождений), повторный save байт-в-байт, битый/неизвестный ключ → дефолт + сообщение с именем ключа и номером строки, значение вне диапазона → кламп с числом (`out-of-range -> 90`, `settings_w 40 -> 120`), внешний сверитель `tests/py/settings_ini_smoke.py` (9 ключей против Python-парсера); панель настроек берёт мышь (drag 0 px, pick 0 px над панелью; вне панели drag 144 px), `P` скрывает/возвращает, `Esc` работает; пиксельная метрика панели (фикстура 1280×900): rect 460×679 px = width из `settings_w` и room под карточкой, чернила 10041 px внутри (coverage 3.21%, контраст 11.31:1, ink box покрывает 99% высоты панели), чернила вне панелей 0 px, три панели не пересекаются (stats 8..357 × 8..56, settings 8..468 × 64..743, hints 260..1020 × 759..892); в окне 800×600 панель остаётся rect 460×379 px при room 379 px — строки ниже складки достаются scrollbar'ом, подсказки не задеваются; на узком кадре 420×320 панель не рисуется, хвост подсказок убирается (overlap 0 px); кадр HUD пишется в `build/settings_panel.bmp` (подписи виджетов читаются целиком — дефолт ширины поднят с 320 px до 460 px, при 320 px текст обрезался); `--render` и `--selftest` остаются на дефолтах — детерминизм существующих метрик не ломается. **Post-release фикс 2026-10-22:** сворачивание панели треугольником роняло процесс (`Assertion failed: (0) && "Missing End()"`, `imgui.cpp:11325`) — `igBegin` возвращает false для свёрнутого окна, а `igEnd` обязателен; во всех трёх панелях `hud_draw` теперь всегда вызывает `igEnd`, selftest-шаг fold/unfold (`tab 460 x 23 px` под курсором → `rect 460 x 679 px`) это закрепил. **Второй фикс 2026-10-22 (мышь панели, три жалоб owner'а):** колесо над панелью прокручивает список виджетов (`scroll_y 0.0 -> 85.0 px` при rect 170×679 px, room 679 px — снят флаг `NoScrollWithMouse`), панель двигается за заголовок (`rect y 64 -> 104` за drag 40 px, координаты возвращаются в `settings.ini`: `panel moved by the title bar to (8, 104)`), активный drag держит камеру вне игры и после выхода курсора за окно (`held busy 1, camera gate 0, after release busy 0` при cursor 1279×899 против rect 8..178 × 96..770). События мыши для headless-проверки инжектируются в очередь ImGui (`hud_warp_mouse`: `AddMousePosEvent`/`AddMouseButtonEvent`/`AddMouseWheelEvent`), итого 11 разделов / 81 строка `ok`. Перемерено на дефолте текста 13.26 px (E5.T8): панель `460 x 674 px`, чернила 6985 px (coverage 2.25%, контраст 14.13:1), свёрнутая вкладка `460 x 19 px`, drag за заголовок `rect y 56 -> 96`, колесо над панелью меряется в кадре 1280×520 (`scroll_y 0.0 -> 65.0 px`, room 361 px — при 1280×900 список влезает целиком), окно 800×600 — `rect 460 x 441 px` внутри места 441 px |
| E5.T8 | Дефолт размера текста HUD: `hud_font_scale` как пиксельный размер и читаемый дефолт | ✅ | ключ `hud_font_scale` — множитель над `HUD_FONT_BASE = 17 px` (`GetFontSize() = FontSizeBase × FontScaleMain`), диапазон `[0.6, 2.5]` = 9.6…42.5 px; owner сверил читаемый размер на реальном окне: 0.73…0.80 = 12.41…13.60 px. Встроенный дефолт поднят до середины диапазона 0.78 = 13.26 px; дефолт совпадает в двух местах (`src/settings.c` ключ, `src/hud.c` профиль HUD до `settings.ini`) и сверяется численно в selftest (`hud glyph size: base 17 px x scale 0.78 = 13.26 px, HUD profile 0.78 == settings default`) и зондом `lab/probes/probe_font.ps1` (scale 1.00 → rect 460×679 px, ink 10041 px, coverage 3.21%, contrast 11.31:1; scale 0.78 и 0.80 → rect 460×674 px, ink 6985 px, coverage 2.25%, contrast 14.13:1, чернила не выходят за rect; `--selftest` и `make test` зелёные при обоих дефолтах) |
| E5.T9 | Размер текста HUD в настройках как **прямой px**: `hud_font_px` вместо множителя | ✅ | ключ `hud font size` в пикселях (дефолт 13.26 px, диапазон 6…32 px, `fmt "%.2f px"`); внутри ImGui `FontScaleMain = px / HUD_FONT_BASE (17 px)`; legacy `hud_font_scale` читается как алиас (`px = scale × 17`), `settings_save` пишет только `hud_font_px`; env `HUD_FONT_PX` (legacy `HUD_FONT_SCALE` поддерживается); `hud_set_font_px()` вместо `hud_set_font_scale()`; legacy-файл нормализуется при первой загрузке (`settings.legacy_keys`) — headless-проверка `--idle-report 0.2` (окно не создаётся): run 1 печатает `legacy key 'hud_font_scale' = 0.76 read as 'hud_font_px' = 12.92` + `rewritten in current keys - 1 legacy key(s) converted`, run 2 — stderr пустой, `24 keys applied`, `hud: font 12.92 px`; selftest: `1 legacy key counted` и `0 legacy keys left, hud font 12.41 px`; метрики HUD не изменились (`hud glyph size: 13.26 px == settings default 13.26 px`). Зелёное: `make` без warning'ов, `selftest: PASS` (11 разделов, 84 строка `ok`, 0 FAIL), `make test` → `tests: PASS`, `tests/py/settings_ini_smoke.py` PASS (24 ключа), `tests/py/hud_panel_probe.py build/settings_panel.bmp` exit 0 |
| E5.T2 | Сетка/оси, несколько моделей, tone mapping | ⬜ | скриншот-тест |
| E5.T3 | DPI/resize корректны | ✅ | on_resize + aspect re-fit |

## E6. QA и автоматизация (🟡 база есть, расширяется)

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E6.S1 | `make test` зелёный без ручного осмотра картинок | ✅ | `tests: PASS`, exit 0 |
| E6.T1 | Selftest: parser/projection/mouse/pick/outline/frame-orientation | ✅ | selftest-проверки (`ok`, 0 `FAIL`) + регрессионные, exit 0; HUD-блок измеряет rect/coverage/контраст/runs и детерминизм ImGui-оверлея |
| E6.T7 | Контракт ориентации кадра (`row 0` = верх) зафиксирован численно, а не «на глаз» | ✅ | CPU-зонд `lab/probes/probe_img.c`: top-down → PASS, GL-порядок → FAIL; selftest `ok  BMP orientation`: верхний ряд = красный, нижний = синий |
| E6.T6 | Вывод регрессии в `tests/out/` (корень чист, артефакты не в VCS) | ✅ | `tests/out/*.bmp` после прогона, корень без BMP |
| E6.T2 | Регрессия пикселей (coverage, silhouette, shading) | ✅ | 5 моделей + ortho/fov матрицы |
| E6.T3 | Кроссплатформенная проверка (Linux/macOS) | 🔵 | зависит от scope |
| E6.T4 | CI-рука (или локальный gate) + фиксация артефактов | 🔵 | git + remote (public `main`) есть; gate = `tests/run_tests.ps1`, CI-рука не настроена |
| E6.T5 | Производительность: время кадра, время picking на крупной модели | ✅ | замер на `assets/utah_teapot_test_2mil_tri.obj` (2 250 948 tri, локальная фикстура, не публикуется): парсинг 3.02 s, peak RSS 747 MB offscreen / 764 MB интерактив, кадр 23.7 fps при 1280×900 MSAA 4 (exit 0, coverage 12.0%), picking 39.9 ms на hover-луч (17.7 ms на 1M tri, худший 44.4 ms), `obj_mesh_free` 11 ms; зонд `lab/probes/probe_bigobj.c` (PASS), отчёт `docs/reports/report_2026-10-22-e6_t5_big_model.md`; метрики зафиксированы в PROJECT_MEMORY §2/§6 |

## E7. Доставка и переносимость (⬜)

| ID | История / Задача | Статус | Acceptance |
|---|---|---|---|
| E7.T1 | Каталогная политика: `build/`, `lab/`, `artifacts/` (мусор из корня уходит) | ✅ | решение owner'а 2026-10-22 (ADR §4.20): бинарь продукта — `viewer.exe` в корень (`make`, git-ignored), запуск и регрессия из корня репозитория (`./viewer.exe`, `./viewer.exe --selftest`, `make test`); пути `assets/…obj` и `shaders/…glsl` резолваются относительно cwd, поэтому запуск только из корня. `build/` заморожен: git-ignored, бинаря там нет, допустимы только временные файлы сборки (`*.o`) если понадобятся; `mkdir -p build` из recipe убран. Зонды в `lab/probes` (источники трекаются, артефакты игнорирует `lab/.gitignore`); вывод тестов — `tests/out/`; кадры публикации — `docs/`; отчёты шага — `docs/reports/` (трекаются); модели — `assets/` (`!assets/*.obj`); scratch — `trash/` (не публикуется). `artifacts/` как каталог продукта не заведен |
| E7.T2 | Portable build (Makefile/CMake, без абсолютных путей) | 🟡 | Makefile без абсолютных путей; сборка из чистого clone проверена вручную: `git clone` во временный каталог → `make imgui` → `make` → `--selftest` PASS → `--render` exit 0 → `make test` PASS. Трекаемого скрипта клон-проверки нет. Linux/macOS не проверялись — E6.T3 🔵 |
| E7.T3 | README — публичное описание репозитория (не инструкции); регламент агента в `AGENTS.md` §6 | ✅ | README: что это, скриншот, возможности текущего билда, стек, сборка/запуск, реальный кэймап hotkeys, численная проверка, структура, MIT. Проверено: команды из README зелёные (`make imgui`, `make`, `--selftest`, `make test`), все клавиши таблицы есть в `src/main.c` (14 уникальных `GLFW_KEY_*`, включая Ctrl-модификаторы и `D`/`L` из E7.T6), `--render assets/cube.obj tests/out/frame.bmp 800 600` exit 0, пути скриншота и каталогов существуют. Состав секций, запрет регламента/токенов в README и обязательная синхронизация с кодом зафиксированы в `AGENTS.md` §6. **Кадр обновлён 2026-10-23:** `docs/viewer-window.png` (100150 байт) — реальное окно 1280×800 на дефолтах, текст 13.26 px (E5.T8/E5.T9, ключ `hud_font_px`), подсказки с `Esc/Q - quit` (кадр создаёт приложение по `S`, зонд `lab/probes/probe_shot.ps1`, PASS: coverage силуэта 45.58%, чернила трёх карточек 1573 / 32985 / 4413 px, BMP 3072054 байт, PNG 1280×800 и 100150 байт; клавиши зонда отправляются с реальным scan-code через `MapVirtualKeyW`, окно закрывается по `Q`, exit 0); подпись README переписана честно — outline и выбранная грань в новом кадре не показаны. **Hotkey-фикс 2026-10-23:** выход по `Q` в продукте не существовал (в `on_key` был только `GLFW_KEY_ESCAPE`) — `Q` добавлен как второй ключ выхода, строка `controls:` печатает `Esc/Q quit`, README-таблица — `Esc` / `Q` |

---

## Порядок работы

1. Discovery (E0) → утверждение Scope/DoD → реализация по приоритетам (E4 → E5 → E6 → E7).
2. Сложные фичи/новые форматы: сначала прототип в `lab/<feature>`, затем перенос в `src/`.
3. Каждый шаг: чек-лист на русском → реализация → проверка → отчёт (цель/сделано/как проверено) →
   **ожидание одобрения** → коммит + отметка статуса здесь и в `PROJECT_MEMORY.md`.
4. Отчёт каждого шага публикуется в `docs/reports/report_YYYY-MM-DD-<task>.md` (трекается,
   ADR `PROJECT_MEMORY.md` §4.19, формат — `AGENTS.md` §5 и `docs/reports/README.md`).
   Рабочий scratch/harness-лог остаётся в `trash/logs/` (git-ignored) и не публикуется.
