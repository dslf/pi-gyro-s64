# Отчёт шага: кадр README из реального окна (2026-10-23)

**Цель:** заменить `docs/viewer-window.png` на актуальный кадр текущего билда: Utah teapot на
дефолтных настройках, три панели HUD, без захвата рабочего стола и без DPI-масштабирования.

## Сделано

- `lab/probes/probe_shot.ps1` — новый зонд (источник трекается, артефакты в `lab/probes/`).
  Кадр создаёт **само приложение** по hotkey `S` (`snapshot()` → `fb_read_rgba` default FBO →
  `img_write_bmp`), поэтому HUD внутри кадра, а фон/DPI не смешиваются. Порядок зонда:
  `settings.ini` отодвигается в бэкап (кадр на встроенных дефолтах) → `viewer.exe assets/utah_teapot.obj
  --size 1280 800` → окно находится по префиксу заголовка → `WM_KEYDOWN/WM_KEYUP` VK `S` (с fallback на
  injected `keybd_event`) → VK `Escape` → разбор BMP → PNG в `docs/` → `settings.ini` возвращается.
  24-битный BMP читается напрямую: строки bottom-up, `bits` на смещении 28, `BI_RGB` на 30 (смещения
  сверены с `src/img.c`, который пишет заголовок).
- `docs/viewer-window.png` — новый кадр 1280×800, 160331 байт (старый 802×632, 86679 байт —
  захват экрана, старше панели виджетов).
- `README.md` — подпись кадра переписана честно: окно 1280×800 на дефолтах, три панели HUD
  (статистика / Settings / подсказки), outline **не** показан, потому что грань под курсором не выбрана.
  Старая подпись обещала «outline силуэта, выбранная грань, две панели» — в новом кадре этого нет.

## Проверено

```
powershell -NoProfile -File lab/probes/probe_shot.ps1
settings.ini aside for defaults: True
ok   screenshot.bmp written by the app's own S hotkey (3072054 bytes)
exit: 0
ok   stderr empty (no GL errors during the shot)
ok   app reported screenshot.bmp at 1280x800
ok   stdout names assets/utah_teapot.obj
ok   BMP header: 1280 x 800 x 24 bpp, pixel offset 54
ok   background corner luminance 132.6, silhouette coverage 47.27% (484073 px), bright ink 113952 px, untouched background 905211 px
ok   stats (top-left) card: ink 2437 px, panel background 50051 px of 57152 px
ok   settings (left column) card: ink 35952 px, panel background 195596 px of 263552 px
ok   hints (bottom-centre) card: ink 7563 px, panel background 90875 px of 109824 px
ok   wrote docs\viewer-window.png (160331 bytes) from the 3072054 byte BMP; root screenshot.bmp removed
ok   PNG dimensions match the frame: 1280x800
ok   settings.ini restored from the backup (862 bytes)
probe_shot: PASS   (exit 0)
```

Числа кадра (относительные пороги в зонде): coverage силуэта 47.27% > 2%, фон не «почти весь кадр»
(905211 px из 1024000 px = 88.4%), чернила во всех трёх карточках (2437 / 35952 / 7563 px), фон углов
132.6 — серый скай-градиент по дефолту `sky light = true`, не чёрный `bg_rgb`.

Регрессия продукта (код `src/` не менялся):

```
make            -> сборка без warning'ов
./viewer.exe --selftest -> selftest: PASS (11 разделов, 81 строка ok)
make test       -> tests: PASS
./viewer.exe --selftest | Select-String '^selftest: ' -> 11 разделов + 1 итог
Select-String src/main.c -Pattern 'GLFW_KEY_' -> 'S' присутствует (snapshot)
Test-Path docs/viewer-window.png -> True
```

`git status --short`: изменён только `docs/viewer-window.png` (tracked, 86679 → 160331 байт) и
новый `lab/probes/probe_shot.ps1`; `settings.ini` вернулся байт-в-байт (862 байта); артефакт
`lab/probes/probe_shot.bmp` и `*.bmp` — ignored.

Визуальное содержание кадра подтверждено owner'ом (модуль зрения harness'а выключен, регламент
`AGENTS.md` §0.9): чайник в центре, Settings слева со скроллом, статистика `persp fov 60 deg /
dist 1.12 / 1250 tris / 169.9 fps`, подсказки внизу, ровный серый фон, мягкий свет сверху-слева.
Числовые метрики зонда (coverage, чернила) подтверждают presence, но «что на экране» описывает owner.

## Риски / следующий шаг

- Кадр снят с реального окна, поэтому FPS в карточке статистики — live (169.9 fps на этой
  машине): это показатель запуска, не замер E6.T5.
- Пиксельная метрика кадра проверяет presence (силуэт + чернила карточек), но не сверяет rect
  панелей внутри кадра — rect сверяет `--selftest` на фикстуре 1280×900.
- `--size 1280 800` даёт framebuffer ровно 1280×800 на этой машине (DPI scale не вмешивается);
  на другой scale'е зонд читает размер из stdout BMP, поэтому пороги остаются относительными.
