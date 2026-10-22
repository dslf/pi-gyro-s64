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
