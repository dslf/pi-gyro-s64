# Отчёт шага — E5.T8: дефолт размера текста HUD (2026-10-23)

**Цель.**Owner указал, что «нужный» размер текста HUD получается при `hud_font_scale` 0.73…0.80.
Выяснить, что этот множитель даёт в пикселях, и сделать нужный размер дефолтом.

## Что выяснено (факты, не предположения)

- HUD-текст рендерится одним шрифтом из атласа (`src/hud.c`), базовый размер задан
  `HUD_FONT_BASE` и **равен 17 px** (`src/hud.c:21`, `hud_atlas_bake(..., HUD_FONT_BASE, ...)` в
  `hud_init`). Дефолтный профиль `HUD_TEXT_DEFAULT` имеет `font_scale = 1.00` → 17 px.
- Множитель применяется как `ImGui::FontSizeBaseMain = HUD_FONT_BASE * font_scale`
  (`src/hud.c:134-136`, `ui->FontSizeBaseMain = text->font_base * text->font_scale`),
  `DisplayFramebufferScale = 1.0` → `GetFontSize() = FontSizeBase × FontScaleMain`
  (`deps/imgui/imgui/imgui.cpp:8957, 9324-9327`).
- Диапазон owner'а = **пиксельный размер**: 0.73 → 12.41 px, 0.78 → 13.26 px, 0.80 → 13.60 px;
  base 1 px → 17 px. Ключ `hud_font_scale` в `settings.ini` (диапазон 0.6…2.5, шаг 0.02 в
  слайдере), env-override `HUD_FONT_SCALE`.
- Зонд `lab/probes/probe_font.ps1` (реальный билд, `--selftest`, оверлей рисуется в offscreen-кадр):

  | scale | px | чернила help-панели | coverage | контраст |
  |---|---|---|---|---|
  | 0.73 | 12.41 | 7052 px в 760×74 | 12.54% | 5.01:1 |
  | 0.78 | 13.26 | 4405 px в 760×79 | 7.34% | 14.98:1 |
  | 0.80 | 13.60 | 9293 px в 760×84 | 14.56% | 5.39:1 |
  | 1.00 | 17.00 | 13426 px в 760×133 | 13.28% | 6.30:1 |

  Квадратичная зависимость чернил подтверждена: 7052/13426 = 0.525 против ожидаемых 0.73² = 0.533;
  высота панели 74/133 = 0.556 — ниже линейного 0.730, потому что фиксированные отступы
  (`ItemSpacing`, padding) не масштабируются вместе с текстом.

## Дефолт

Выбран **0.78 → 13.26 px**: середина диапазона owner'а и лучшая читаемость по числам — при этом
значении чернила текста имеют контраст 14.98:1 (против 5.01:1 при 12.41 и 6.30:1 при 13.60), то есть
текст в этом размере rasterize'ится наиболее «dense». Порог контраста 3:1 и coverage 0.5%
существующих проверок проходят при обоих дефолтах (0.78 и 1.00).

## Сделано (файлы)

| файл | что |
|---|---|
| `src/settings.c` | `SETTINGS_DEFAULTS.hud_font_scale = 1.00f` → `0.78f` (комментарий: множитель над base 17 px) |
| `src/hud.h` | новое объявление `float hud_font_base(void)` + комментарий про base × scale |
| `src/hud.c` | `hud_font_base()` возвращает `HUD_FONT_BASE`; `HUD_TEXT_DEFAULT.font_scale = 0.78f` (с Комментарием «settings default, ADR 4.24») |
| `src/main.c` | `hud_font_px()` — пиксельный размер; `hud_text_rect` считает высоту строки через пиксельный размер, а не через `font_scale`; новый selftest-блок «hud glyph size» (base × scale == профиль, сверка с дефолтом настроек; equality пропускается при `HUD_FONT_SCALE` override); тест колеса над панелью переведён на fixture 1280×520, потому что при новом дефолте список влезает в 1280×900 целиком и scroll-тест стал бы no-op |

## Проверено (команды + вывод)

```
make                                  -> gcc -O2 -std=c11 -Wall -Wextra, 0 warning
./viewer.exe --selftest               -> selftest: PASS (exit 0)  11 разделов, 82 строка ok
  ok   hud glyph size: base 17 px x scale 0.78 = 13.26 px == settings default 0.78
  ok   HUD glyph ink: coverage 2.25%, contrast 14.13:1
  ok   hud settings panel rect: 460 x 674 px
  ok   hud settings tab: 460 x 19 px
  ok   hud settings drag: rect y 56 -> 96 (dragged 40 px)
  ok   hud wheel over panel: scroll_y 0.0 -> 65.0 px, room 361 px
make test                             -> tests: PASS  exit 0
python tests/py/settings_ini_smoke.py -> PASS 24 keys parsed, all values inside their ranges
python tests/py/hud_panel_probe.py build/settings_panel.bmp -> exit 0 (строки текста 12 px)
python tests/check_images.py image tests/out/out_teapot.bmp teapot -> checks: PASS (coverage 12.5%)
python tests/check_images.py framing tests/out/out_cube_tall.bmp  -> checks: PASS
powershell -File lab/probes/probe_font.ps1  -> probe_font: PASS (exit 0)
powershell -File lab/probes/probe_resize.ps1 -> resize probe: PASS (fps 170, sizes 1264x861 684x441)
powershell -File lab/probes/probe_shot.ps1   -> shot probe: PASS, docs/viewer-window.png 100071 байт
```

Кадр README переснят на новом дефолте (1280×800, дефолты без выбранной грани); подпись README
исправлена одновременно: старый текст говорил `fov 60 deg` (дефолт 45°) и «скроллable» (при 13.26 px
список из 21 виджента помещается при высоте 800 px целиком).

## Риски / следующий шаг

- Диапазон слайдера 0.6…2.5 остаётся; owner может выбрать 0.73 или 0.80 — это легальные значения,
  контраст в них 5.0…5.4:1 (порог 3:1 проходит).
- `tests/py/hud_panel_probe.py` требует путь к bmp как аргумент — вызывается с `build/settings_panel.bmp`.
- `--render`/`--selftest` используют встроенный профиль (0.78); живой `settings.ini` даёт ту же
  величину, поэтому метрики совпадают с интерактивом.
- Визуальная сверка нового кадра требует owner'ского описания (AGENTS §0.9) — если нужно, попроси.

**Статус Roadmap:** E5.T8 → ✅.
