# Отчёт E5.T4/E5.T1 — перенос HUD в продукт

**Цель:** перенести HUD-текст из зонда `trash/probes/probe_hud.c` в продукт: глиф-атлас (GDI) →
CPU-маска → геометрия штрихов → GL-оверлей, с числовыми selftest-проверками.

## Сделано

| Файл | Что |
|---|---|
| `src/hud.h` (41 строк) | API: `hud_init/hud_draw/hud_free`, `hud_glyph_ink`, `hud_glyph_row_profile`, `hud_glyph_cell_mask` |
| `src/hud.c` (271 строк) | атлас GDI (`CreateDIBSection` + `TextOutW`, 95 глифов ASCII 32..126), luminance-маска, `hud_draw` — горизонтальные штрихи (ink-runs) как premultiplied-квады, blend `(ONE, ONE_MINUS_SRC_ALPHA)`, depth test выключен |
| `shaders/hud.vert`, `shaders/hud.frag` | pixel-space вершина (`aPos/uViewport`, y сверху) + фрагмент, цвет = premultiplied ink |
| `src/gl.h/gl.c` | новые слоты загрузчика: `glBlendFunc`, `glDepthMask`, `glUniform2fv` |
| `src/main.c` | инициатива HUD в контексте, оверлей в `draw_scene`, `hud_free`; флаг `hud_overlay`; selftest-блок `HUD overlay` |
| `Makefile` | `src/hud.c/hud.h`, `shaders/hud.vert/frag` в SRC/HDR/SH |
| `trash/probes/probe_atlas.c` | isolated-зонд дампа CPU-маски (GDI-only) |

Коммит `1023dff` (8 файлов, +568 строк).

## Два дефекта, найденных числами

1. **luminance-веса 59/20/11 (сумма 90)** → белый фон атласа давал lum 229, ink `0.10` > порога
   `0.02`, вся ячейка считалась чернилами → глифы рисовались сплошными блоками (5×10 px).
   Исправлено на BT.601 `30/59/11` (сумма 100): белый stays 255, чёрный stays 0.
   Диагностика: `trash/probes/probe_atlas.c` + временный дамп вершин (`qx = 12` при `px = 2`,
   т.е. штрих растягивался на всю ширину ячейки).
2. **HUD попадал в offscreen-кадры `--render`** → 3 падения тестов (`teapot lid not narrower`,
   `silhouette touches the frame edge`, IndexError в сравнении проекций). Решение по выводу зонда:
   оверлей рисуется только в интерактивном кадре (`a->hud_overlay = 1` в window-цикле,
   0 в `--render` и в selftest кроме блока HUD).

## Проверено (фактический вывод)

```
viewer.exe --selftest           exit 0, ok=32, fail=0
  hud: atlas 100x190, cell 10x19, 94 glyphs, min ink 0.021
  ok   HUD ink: 635 pixels of 320x200 (0.99% of frame)
  ok   HUD centroid (145.5, 22.1) sits top-left
  ok   HUD layout is deterministic (0 pixels moved on redraw)
  ok   glyph 'p' has stroke structure (row ink 0.0%..30.0%, mean 9.5%)
  ok   glyph orientation matches the mask top-down (direct 0.126 < mirrored 0.158)
  ok   glyph cell keeps 90.5% background around the ink

tests/run_tests.ps1             tests: PASS
  cube coverage 37.5%, teapot 12.7% / 12.3% / 10.9%, aspect 1.06 / 1.49 / 1.40 / 1.54
  wide fov deviation 0.41 < 0.73 < 0.85, wireframe 328.6% < shaded 5463.6%
make                            чистый вывод без warning'ов
```

До исправления веса маски тот же selftest давал `HUD ink: 5225 px (4.46%)` и `FAIL glyph 'p' cell
is flat` — регрессия не ослаблена, пороги прежние (относительные).

## Риски / следующий шаг

- Атлас и метрики зависят от системного DPI (в продукте ячейка 10×19 при 17 px шрифте);
  размер глифов задаётся при `hud_init`, пер-каدر ресайз атласа не делается.
- `hud_draw` рисует штрихи по CPU-маске на каждый кадр (для двух строк ~26 штрихов, ~156 вершин) —
  горячий путь не логрируется; при длинном тексте есть cap и сообщение о truncation.
- hidden-window не подтверждает оверлей в default framebuffer (ограничения зондов зафиксированы в §2
  `PROJECT_MEMORY.md`); проверка идёт на продукном FBO-пути.
- В HUD второй строки пока только счётчик треугольников; `id`/`normals` выбранного
  треугольника (E5.T1 acceptance) — следующий шаг.

**Статус Roadmap:** E5.T4 → ✅; E5.T1 → 🟡 (текст HUD в продукте есть, id/normals в HUD ещё нет).
