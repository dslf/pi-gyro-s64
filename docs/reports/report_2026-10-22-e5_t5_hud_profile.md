# Отчёт: профиль HUD — font scale 1.0, компактная панель (E5.T5)

**Цель:** вернуть `font scale` к 1 и поджать внешний вид HUD (панель стала меньше, палитра
контрастнее), с численной проверкой читаемости.

**Сделано**
- `src/hud.c`: `HUD_FONT_SCALE` 1.5 → **1.0**; панель 480×112 → **380×96**; стиль плотный —
  без рамки (`WindowBorderSize 0`), `WindowRounding 4`, `WindowPadding 8×6`, `ItemSpacing 0×2`;
  палитра: WindowBg (0.08,0.10,0.14, α 0.94), Text (0.95,0.97,1.00).
- `src/hud.h`/`src/hud.c`: размеры и масштаб хранятся в `hud` (`margin`, `panel_w`, `panel_h`),
  добавлены env-переопределения `HUD_FONT_SCALE`, `HUD_W`, `HUD_H` (подбор профиля без пересборки);
  лог инициализации печатает фактический профиль.
- `Roadmap.md` (E5.T5 — новые метрики), `PROJECT_MEMORY.md` (ADR §13, статус M9, §6 риск-строка).

**Проверено** (`make` чистый вывод; `viewer.exe --selftest assets/cube.obj`; `tests/run_tests.ps1`)

```
hud: imgui 1.93.0, font scale 1.00, panel 380x96 px
  ok   HUD ink: 36348 pixels of 560x240 (27.04% of frame)
  ok   HUD centroid (197.5, 55.5) sits top-left
  ok   HUD layout is deterministic (0 pixels moved on redraw)
  ok   hud rect 8..388 x 8..104 (380 x 96 px)
  ok   hud palette: text lum 246.4 on panel bg lum 24.6 (requested 16.39:1)
  ok   hud ink mean lum 165.7 (max 248.8), panel mean lum 35.1
  ok   hud ink coverage 13.00% of panel, contrast 6.43:1 (WCAG)
  ok   ink runs: short(<=12px)=1470 long(>12px)=1, ratio 1470.00
selftest: PASS          exit 0
tests: PASS             exit 0
```

Контроль переопределений (`HUD_FONT_SCALE=1.5 HUD_W=480 HUD_H=112`): контраст 8.05:1,
coverage 15.86%, runs 1927/4 — обе конфигурации зелёные, дефолт — scale 1.
Интерактив: `viewer.exe utah_teapot.obj` жил 4 с, stderr пуст, GL-ошибок нет.

**Коммиты:** `06f2067` (код HUD), `99e46c4` (Roadmap + PROJECT_MEMORY). Worktree чист.

**Риски/следующий шаг**
- При scale 1 текст меньше (контраст 6.43:1 остаётся выше порога WCAG AA 4.5:1); для мелких
  панелей подбор профиля — env-переменными, метрики печатает selftest.
- `id` треугольника в HUD (E5.T1) и BVH для picking на 1M граней — следующий кандидат.
