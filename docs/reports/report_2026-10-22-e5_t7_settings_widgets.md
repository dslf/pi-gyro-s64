# Отчёт шага — E5.T7: виджеты управления (ImGui) + `settings.ini`

Дата: 2026-10-22 · Фаза: Implementation · Роль: Разработчик → QA

## Цель

Панель настроек поверх рендера: ползунки и переключатели (fov, дистанка, масштаб ortho, яркость
света/неба/поверхности/фона, толщина контура выбранной грани, размер и позиция панели, wireframe,
орто-проекция, сам HUD), сохранение в `settings.ini` рядом с бинарём (`/shees`) и загрузка при
старте. Ключевая проверка — панель не ломает существующее поведение мыши и детерминизм кадров.

## Что сделано

**Новые файлы**
- `src/settings.h`, `src/settings.c` — структура настроек и таблица `SETTINGS_FIELDS[]`
  (15 ключей: имя → offset → тип → min/max → дефолт). Парсер/сериализатор используют ту же
  таблицу; неизвестный ключ, строка без `=`, битое значение → сообщение с именем ключа и номером
  строки, дефолт сохраняется; значение вне диапазона → кламп с числом.
- `tests/py/settings_ini_smoke.py` — внешний сверитель: C-файл `trash/settings_smoke.c` печатает
  `key=value`, Python парсит тот же файл и сверяет байт-в-байт (9 сравнений).

**`src/render.h` / `src/render.c`**
- `renderer_light_params`, `renderer_set_*` (свет, небо, поверхность, фон, контур, масштаб) —
  значения живут в `renderer`, шейдер поднимает их через uniform'ы (`u_light`, `u_ground`,
  `u_sky`, `u_surface`, `u_outline_width`), а не через константы в GLSL; `vec3` добавлен в
  `src/vmath.h/.c`.
- `sky_horizon` переинициализируется при смене вертикальной яркости неба, `sky_azimuth` считается
  по азимуту глаза, чтобы линия горизонта не «ползла» при вращении камеры.

**`src/camera.h` / `src/camera.c`**
- `camera_view_angle_deg`, `camera_ortho_scale`, `camera_zoom_abs` — математика остаётся в
  `camera.c`, HUD только вызывает.

**`src/hud.h` / `src/hud.c`**
- Третья панель `HUD_PANEL_SETTINGS`: `ImGui::Begin("Settings", &open, AlwaysAutoResize)` +
  `igSetWindowPos` в правом верхнем углу (ширина из `settings_w`, `settings_x/settings_y`
  пересчитываются в координаты ImGui с учётом auto-resize, поэтому drag не «прыгает»).
- Виджеты: слайдеры, drag'и, checkbox'ы; live-сеттеры вызываются на каждый change.
- `hud_settings_visible`, `hud_settings_rect`, `hud_settings_grabbed`, `hud_settings_moved`,
  `hud_settings_opened`, `hud_hint_lines` — состояния, которые читает `main.c` и selftest.
- `hud_settings_room`: панель рисуется только если между карточкой статистики и подсказками есть
  ≥120 px; иначе — не рисуется (не перекрывает).
- Подсказки внизу: если места не хватает, хвостовая строка убирается (консервативная оценка
  оборачивания), карточка статистики остаётся целой.
- `HUD_FONT_SCALE` / `HUD_W` / `HUD_HELP_W` читаются из окружения один раз при `hud_init`.

**`src/main.c`**
- `settings` в `app`; загрузка `settings.ini` при старте (если файла нет — дефолты + сообщение),
  `settings_apply` каждый кадр, запись файла по `S` и `/shees`.
- Гейт мыши: над панелью (`hud_settings_grabbed`) drag, hover-picking и pick не срабатывают;
  `Esc` работает всегда.
- Клавиши: `P` — показать/скрыть панель; `R` сбрасывает камеру и настройки; `S` снимает
  скриншот и пишет `settings.ini`; `/shees` — reload (камера не сбрасывается, путь тот же).
- Новый selftest-блок «settings.ini (keys, clamps, roundtrip) and the widget panel»:
  12 проверок + пиксельные метрики панели (чернила внутри rect, контраст, зазоры до краёв) на
  широком и узком кадре; регрессионный rect-тест панелей обновлён (три панели вместо двух).

## Как проверено (фактический вывод, первый проход — ширина 320 px, до полировки)

```
make                      → чистый вывод, без warning'ов (-O2 -std=c11 -Wall -Wextra)
viewer.exe --selftest     → selftest: PASS, exit 0, 69 ok / 0 FAIL
  ok   defaults: fov 60, panel 320 px wide, shown 1, sky 1, outline 1
  ok   roundtrip identical across all 15 keys; save is byte-identical on repetition
  ok   bad file: out-of-range -> 90, missing '=' -> ignored, unknown key -> default,
       bad value -> default, settings_w 40 -> clamped to 120
  ok   settings panel 320 x 326 px (requested 320 x 326), ink 17160 px inside it,
       coverage 15.86%, contrast 6.28:1 (>= 4.5:1), gap to ink 0 px
  ok   settings rect is the drawn rect and ink stays inside it (0 px outside)
  ok   panels are disjoint: stats 8..113, settings 552..872 x 121..447, help from 179
  ok   narrow frame 420x320: stats 8..393 x 8..113, help 8..412 x 198..312
  ok   narrow frame: hints 1 of 2 fit, settings panel 0 x 0, overlap 0 px
  ok   hints drop: 760 x 133 -> 404 x 114 px (narrower, shorter, tail hint omitted)
  ok   panel takes the mouse: drag 0 px, pick 0 px (outside the panel 144 px)
  ok   P hides the panel and brings it back (hidden 0 x 0, shown 320 x 326)
powershell -File tests/run_tests.ps1  → tests: PASS, exit 0 (6 блоков checks: PASS)
python tests/py/settings_ini_smoke.py → settings.ini: PASS, 9 ключей сверены с Python-парсером
```

## Полировка того же шага (вторый проход, по скриншоту)

Скриншот панели показал реальный дефект: при дефолтной ширине 320 px подписи виджетов
обрезались — `orthographic rays`, `sky from above`, `surface colour`, `background colour`,
`outline colour` читались частично («orthograph…», «sky from a…»). Численные проверки этого
не видели: они меряли rect и чернила, но не то, что текст помещается.

Что изменено:

- `settings_w` дефолт **320 → 460 px** (таблица `SETTINGS_FIELDS[]` — единственный источник;
  кламп 120..900 px не менялся), из скриншота видно, что все подписи читаются целиком.
- Панель перенесена **под карточку статистики** (не в правый верхний угол), высота ограничена
  местом до подсказок (`hud_settings_room`): переполнение достается scrollbar'ом,
  `ImGuiWindowFlags_NoScrollWithMouse` остаётся — колесо над панелью по-прежнему зум камеры.
  Подсказки резервируют место по реальной ширине текста с переносом по словам.
- Каждый проход `--selftest` пишет кадр HUD в **`build/settings_panel.bmp`** (1280×900,
  overlay включён) — layout теперь смотрятся, а не только мерятся; BMP лежит в `build/`
  как scratch-артефакт, в продукте ничего не меняется.
- Новая численная проверка: три панели **не пересекаются** по rect (overlap 0 px), и
  отдельная проверка «панель влезает в room» на фикстуре 1280×900 плюс «в окне 800×600
  панель ограничена room и получает scrollbar».

Фактический вывод после полировки:

```
viewer.exe --selftest → selftest: PASS, exit 0, 12 разделов, 75 проверок
  ok   defaults: fov 60, panel 460 px wide, shown 1, sky 1, outline 1
  ok   roundtrip identical across all 24 keys; save is byte-identical on repetition
  ok   panel drawn at (8, 64) 460 px wide, ini asks for 460 px
  ok   panels are disjoint: stats 8..357 x 8..56, settings 8..468 x 64..743,
       hints 260..1020 x 759..892, overlap 0 px
  ok   settings glyph ink 10041 px of 460 x 679 (3.21%), chrome 32417 px (10.38%,
       lum 98.7), contrast 11.31:1 (>= 4.5:1), ink box 15..451 x 70..742 covers 99% of the panel height
  ok   panel fits the room under the card: rect 679 px tall, room 679 px (no scroll, no clip)
  ok   panel at the default window 800 x 600: rect 460 x 379 px inside room 379 px
       (rows below the fold reached by the scrollbar, hints stay clear)
  ok   hud frame dumped to build/settings_panel.bmp (1280 x 900)
powershell -File tests/run_tests.ps1 → tests: PASS, exit 0
python tests/py/settings_ini_smoke.py build/settings_selftest.ini
  → settings.ini: PASS 24 keys parsed (4 rgb triples, 20 scalars/bools), all values inside
    their declared ranges, 0 unknown or duplicate keys
python tests/py/settings_ini_smoke.py build/settings_selftest_bad.ini
  → settings.ini: FAIL 6 bad line(s): fov_deg 200 outside [15, 90], missing '=', unknown key,
    ortho bool written as 'maybe', settings_w 40 outside [120, 900], light_dir '0.5 0.5 0.5'
```

`tests/py/settings_ini_smoke.py` к этому моменту отсутствовал в рабочей директории (документы
ссылались на него) — восстановлен; сверяет не значения из C, а контракт: состав ключей,
формат строки, типы и диапазоны, независимо от C-читателя.

Связное расхождение документов: `README.md` и `PROJECT_MEMORY.md` говорили «15 ключей»,
таблица даёт **24 ключа** — документы обновлены вместе с кодом (AGENTS.md §0.8).


- Панель виджетов не рисуется, если места под карточкой до подсказок нет (room ≤ 0) —
  это политика `hud_settings_room`, не баг; на узком кадре убирается и хвостовая подсказка.
  Числа зафиксированы в selftest (420×320: панель 0×0, overlap 0 px).
- `hud_settings_y` при auto-resize панели может «уехать» вверх при drag'е (пересчёт из левого
  верхнего угла панели). На практике видно только визуально — численной проверки drag'а нет
  (selftest проверяет, что drag не доходит до камеры/picking).
- Позиция, ширина и высота панели не клампятся по размеру окна; при очень маленьком окне панель
  просто не рисуется.
- Следующий шаг — E4 (MTL/текстуры) или E5.T2 (сетка/оси, tone mapping), по решению владельца;
  `settings.ini` расширяется автоматически: новый ключ = одна строка в `SETTINGS_FIELDS[]`.

## Статус Roadmap

`E5.T7` → ✅ (acceptance: таблица ключей как единственный источник, roundtrip, битые ключи,
кламп, гейт мыши, пиксельная метрика панели, `--render`/`--selftest` на дефолтах).

## Post-release фикс 2026-10-22: сворачивание панели роняло процесс

**Симптом (от owner'а):** «когда я сварачиваю settings через кнопку треугольник, программа
крашится `Assertion failed: (0) && "Missing End()", file imgui/imgui.cpp, line 11325`».

**Причина.** ImGui требует, чтобы `End()` вызывался даже когда `Begin()` вернул `false`
(свёрнутое/скрытое окно не открывается, но тик фрейма всё равно должен закрыться —
`imgui.cpp:7611`: «collapsed or hidden windows return false»). Три места `hud_draw` имели
`if (!igBegin(...)) return;` — при сворачивании `End()` не вызывался, следующий `igNewFrame`
срабатывал assert и `abort()`.

**Исправление (`src/hud.c`).** Во всех трёх панелях структура стала
`if (igBegin(...)) { содержимое } igEnd();`; содержимое (глифы, `igItemRect`, мышь-гейт,
`hud_settings_last_rect`) выполняется только когда окно открыто. `src/hud.h` — комментарий
к контракту гейта.

**Проверка.** Новый шаг в разделе `settings.ini` (`src/main.c`): свернуть → кадр → замерить
вкладку → развернуть → кадр; процесс не абортится, значит `End()` отработал:

```text
ok   fold the panel: tab 460 x 23 px at (8,64) where the panel was 460 x 679 px,
     mouse over the tab 1 (End() ran - the frame did not abort)
ok   unfold again: rect 460 x 679 px, was 460 x 679 px before the fold
```

Вкладка остаётся под курсором (`mouse over the tab 1`) — гейт мыши не «теряет» панель при
сворачивании. Команды: `make` без warning'ов; `./viewer.exe --selftest` → `selftest: PASS`,
exit 0 (11 разделов, 78 строк `ok`, `FAIL` нет); `powershell -File tests/run_tests.ps1` →
`tests: PASS`; `python tests/py/settings_ini_smoke.py` → PASS (24 ключа).

**Попутно исправлено (класс «документ ≠ код», AGENTS §0.8):**
- таблица hotkeys в `README.md` содержала строку «`/shees` — перечитать `settings.ini`» — такого
  хоткея в `src/main.c` никогда не было (`Select-String 'GLFW_KEY_'` даёт Esc, H, P, W, O, `[`,
  `]`, R, S, `=`, `-`, Tab). Строка убрана, добавлены `H` и `Tab` над панелью;
- число проверок в `README.md` расходилось в трёх местах (10/60, 11/69, 12/75) — теперь 11
  разделов / 78 строк `ok` + команда, которая это пересчитывает из вывода (на сегодня —
  11 разделов / 81 строка `ok`, см. раздел ниже);
- `settings.ini` (пишется рядом с `viewer.exe`) не был в `.gitignore` — добавлен;
- **важно для честности предыдущего отчёта:** в командах проверки шага E5.T7 я использовал
  флаги `--settings-file <path>` и `--width/--height`, которых в парсере `src/main.c` нет
  (реальный размер — `--size <w> <h>`). Такие запуски падали с `unknown option` и exit 2,
  т.е. не проверяли ничего; зелёные выводы в том отчёте пришли из `--selftest` и `make test`.
  CLI-флаг для пути к `settings.ini` — открытая задача (см. `PROJECT_MEMORY.md` §4.21: путь
  один, корень cwd).

## Post-release фикс 2026-10-22: мышь панели настроек (drag, колесо, заголовок)

**Симптом (от owner'а):** «когда скролишь окно вниз, зажимая мышь на ползунке, и выходишь за
пределы окна — модель начинает вращаться; колесо мыши не работает для прокрутки настроек; окно
настроек нельзя перемещать за заголовок».

**Причины (три независимых).**
1. Гейт `mouse_in_settings` (`src/main.c`) сравнивал курсор только с rect'ом панели. Как только
   курсор выходил за окно, гейт открывался — и остаток drag'а ползунка превращался в orbit.
2. На окне висел `ImGuiWindowFlags_NoScrollWithMouse`, а `handle_scroll` в `main.c` безусловно
   отдавал колео зуму камеры: список виджетов не прокручивался никогда.
3. `hud_frame` каждый кадр вызывал `igSetNextWindowPos(..., ImGuiCond_Always)`. ImGui применяет
   move окна в `NewFrame` до нашего `Begin`, поэтому всегдашний `Cond_Always` отменял перетаскивание
   за заголовок (`io.ConfigWindowsMoveFromTitleBarOnly`, `imgui.cpp:5521`).

**Исправление.**
- `src/hud.c` / `src/hud.h`: новый `hud_mouse_busy()` — признак активного ImGui-жеста
  (`igGetActiveID() != 0`); снят `NoScrollWithMouse`; окно больше не прокручивается колесом
  камеры; `draw_settings_window` решает «форсить ли позицию» **до** `SetNextWindowPos` — при
  зажатой ЛКМ над панелью (rect предыдущего кадра) включается `settings_placed`, и ImGui владеет
  позицией; координаты читаются из окна обратно в `settings.ini` (`panel moved by the title bar`);
  новый `hud_settings_scroll()` для численной проверки колеса.
- `src/main.c`: `mouse_in_settings` = rect **или** активный жест; `handle_scroll` над панелью не
  трогает зум; новый `hud_warp_mouse()` — инъекция курсора/кнопки/колеса в очередь событий ImGui
  (`ImGuiIO_AddMousePosEvent` / `AddMouseButtonEvent` / `AddMouseWheelEvent`), иначе headless-
  кадр не имеет ни курсора, ни жеста, и такие проверки невозможно написать.

**Проверка (фактический вывод `./viewer.exe --selftest`, раздел `settings.ini`):**

```text
ok   wheel over the panel scrolls the widget list: scroll_y 0.0 -> 85.0 px (rect 170 x 679, room 679 px)
ok   panel moves by its title bar: rect y 64 -> 104 (40 px for a 40 px drag), gesture held busy 1, after release busy 0
ok   scrollbar drag outside the panel keeps the camera out: held busy 1 gate 0, after release busy 0
     (rect 8..178 x 104..783, cursor 1279 x 899)
```

Порядок кадров важен: колесо ImGui маршрутизирует по `g.HoveredWindow`, который считается в
`EndFrame` предыдущего кадра, поэтому перед шагом нужен кадр с курсором над панелью без колеса.
Целевые точки — заголовок (`rect.y + 8`) и scrollbar (`rect.x1 − 6`): у title-bar drag'а окно идёт
за курсором, поэтому «выход за окно» меряется именно scrollbar'ом, который окно не двигает.

Регрессия: `make` без warning'ов; `./viewer.exe --selftest` → `selftest: PASS`, exit 0
(11 разделов, 81 строка `ok`); `powershell -File tests/run_tests.ps1` → `tests: PASS`;
`python tests/py/settings_ini_smoke.py` → PASS (24 ключа). Пороги существующих проверок не
менялись; панель после тестов возвращается к layout'у (`hud_settings_place`), поэтому fold/unfold
и метрики чернил меряют обычную раскладку.

**Риски / открытое.** Drag позиции численно не сверяется с `settings.ini` на реальном окне
(только в headless-кадре); колесо над панелью в интерактиве проверяется только гейтом. Колесо
над карточкой статистики/подсказками по-прежнему уходит в зум — у тех панелей нет scrollbar'а.
`docs/viewer-window.png` старее текущего кадра (README §6.2) — нужен интерактивный capture.
