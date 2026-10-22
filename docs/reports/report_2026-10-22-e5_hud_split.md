# Отчёт: HUD разделён на две панели (E5.T5 / E5.T1)

**Дата:** 2026-07-26 · коммиты `f2b7392` (код), `4d1c8ee` (документы) · `viewer.exe` собран в корне

## Цель
Статистика остаётся в окне слева сверху; подсказки горячих клавиш выведены отдельным HUD
внизу по центру. Подсказки не должны обрезаться при уменьшении окна.

## Сделано
### `src/hud.h`
- `hud_panel { HUD_PANEL_STATS, HUD_PANEL_HELP }`, `hud_rect(const hud_layout*, hud_panel, ...)`
  вместо `hud_rect(const hud_layout*, ...)`; `hud_layout` хранит размеры обеих панелей
  (`margin`, `panel_w/h`, `help_w/h`).

### `src/hud.c`
- `hud_frame` рисует две панели:
  - **stats** — слева сверху, `pos_in = (margin, margin)`, размер `panel_w × panel_h` (380×96);
  - **help** — внизу по центру: ширина считается по тексту подсказок (`CalcTextSize` для обеих
    строк), ограничивается `help_max_w` (по умолчанию 760 px, env `HUD_HELP_W`), высота — по
    переносу строк; `pos_in = ((frame_w - help_w)/2, frame_h - help_h - margin)`.
- `HUD_HINT_KEYS` разбит на 2 строки, `HUD_HINT_PICK` — одна строка; перенос делает ImGui.
- `hud_layout_init` поджимает ширину подсказок до `min(measured, frame_w - 2*margin)`;
  `hud_layout_is_ready` требует обоих размеров; лог инициализации печатает `stats WxH px, help WxH px`.

### `src/main.c` (selftest)
- `hud_rect(&a->hudl, HUD_PANEL_STATS/…HELP, …)` для обеих панелей; общий `panel_m` для метрик.
- Новые проверки: stats прижат к левому верхнему углу; help центрирован (≤1% ширины) и лежит на
  нижней margin (±6 px); панели не пересекаются; чернила вне обеих панелей = 0 px; контраст каждой
  панели ≥ 4.5:1; coverage каждой панели < 45% (не «плашка»); короткие/длинные штрихи.
- **Новый проход на узком кадре 420×320**: help остаётся внутри кадра, по центру, на нижней margin,
  зазор до stats есть, чернила есть (≥40 px), контраст ≥4.5, и панель стала **уже и выше**
  (доказательство переноса, а не обрезки).

## Проверено (`make` без warning'ов)
```
viewer.exe --selftest → selftest: PASS, exit 0
  ok   stats rect 8..388 x 8..104 (380 x 96 px)
  ok   help rect 100..860 x 196..312 (760 x 116 px)
  ok   stats pinned top-left at (8, 8)
  ok   help centred: centre x 480.0, off by 0.0 px (0.00% of width)
  ok   help sits on the bottom margin: edge 312, gap 8.0 px
  ok   panels disjoint: 92 px gap between them
  ok   HUD layout is deterministic (0 pixels moved on redraw)
  ok   stats ink 3655 px of 380 x 96, coverage 10.02%, contrast 6.51:1, centroid (197.5, 55.5)
  ok   help ink 5037 px of 760 x 116, coverage 5.71%, contrast 6.47:1, centroid (479.5, 253.5)
  ok   hud ink confined to the two panel rects (0 pixels outside)
  ok   stats runs: short(<=12px)=1101 long(>12px)=0
  ok   help runs: short(<=12px)=1490 long(>12px)=3
  ok   narrow frame 420x320: stats 8..388 x 8..104, help 8..412 x 128..312
  ok   narrow help ink 2895 px of 404 x 184, coverage 3.89%, contrast 6.49:1
  ok   hints wrap: 760 x 116 -> 404 x 184 px (narrower, taller)
tests/run_tests.ps1 → tests: PASS, exit 0
Interактив: viewer.exe utah_teapot.obj --idle-report 2 → idle: 335 frames in 2.00 s, fps 171.1
            stderr пуст, окно живёт; HUD инициализация: "stats 380x96 px, help 760x48 px"
```
Контраст обеих панелей выше порога WCAG AA 4.5:1; чернила только внутри панелей; геометрия
детерминирована (0 px на повторном кадре).

## Риски / следующий шаг
- `assets/teapot_1m.obj` (1M tri) в репозитории отсутствует — стресс-проверку HUD на 1M гранях
  надо делать после генерации ассета (E6.T5/E3.T1).
- Высота панели подсказок считается по widest строке; при очень узком окне (< 420 px) она
  растёт — порог «не Leaving the frame» в selftest это ловит.
- Дальше: `id` треугольника в панели статистики (E5.T1), BVH для picking (E3.T1), сетка/оси (E5.T2).

**Статус Roadmap:** E5.T5 ✅ (две панели, новые метрики), E5.T1 🟡 (подсказки вынесены в свою
панель; `id` — 🔵), E5.S1 ✅.

Жду одобрение / указание: E5.T1 (`id`) или E5.T2 (сетка/оси)?
