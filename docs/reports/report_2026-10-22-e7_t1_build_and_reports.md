# Отчёт: E7.T1 — бинари в `build/`, отчёты шага опубликованы

**Дата:** 2026-10-22 · **Roadmap:** E7.T1 → ✅ · **Решение owner'а:** «открыть репорты и бинари
складывать в подобающее место».

## Цель

Каталогная политика доведена до конца: бинарь продукта больше не появляется в корне, а
отчёты шага становятся частью репозитория (публикуются вместе с кодом).

## Сделано

- `Makefile`: цель `all` → `$(OUT)/viewer.exe`, `OUT = build`; каталог создаётся `mkdir -p $(OUT)`
  перед компиляцией; `test` зависит от `build/viewer.exe`; `clean` убирает `build/viewer.exe`.
  `imgui` не менялся (`deps/imgui/build/libcimgui.a`, git-ignored).
- `tests/run_tests.ps1`: запуск `.\build\viewer.exe` (был `.\viewer.exe`), комментарий синхронно.
- `docs/reports/` (трекается): индекс `docs/reports/README.md` (имя файла
  `report_YYYY-MM-DD-<task>.md`, формат по `AGENTS.md` §5) + 7 перенесённых отчётов из
  `trash/logs/` с датой из `LastWriteTime`.
- Документы: `README.md` (команды → `./build/viewer.exe`, структура: `build/`, `docs/reports/`),
  `AGENTS.md` (§0.5 отчёт шага публикуется; §3 команды QA; §4 структура + политика; §6 п.5 и п.8,
  блок проверки), `PROJECT_MEMORY.md` (ADR §4.19, §5 пути, §6 bullets), `Roadmap.md`
  (E7.T1 → ✅, пункт 4 «Порядок работы» → отчёты публикуются).

## Проверено (фактический вывод)

```
make          → mkdir -p build + gcc ... -o build/viewer.exe   exit 0, без warning'ов
build/viewer.exe --selftest → selftest: PASS                   exit 0 (10 разделов, 60 проверок)
build/viewer.exe assets/utah_teapot.obj --idle-report 2
              → idle: 324 frames in 2.01 s, fps 168.0, resize repaints 1   exit 0
build/viewer.exe --render assets/cube.obj tests/out/frame.bmp 800 600
              → wrote tests/out/frame.bmp (800x600, 23.7% non-background)  exit 0
make test     → tests: PASS                                    exit 0
корень        → нет *.exe и *.bmp; только источники и документы
копия рабочего дерева в temp (аналог чистого checkout): make imgui → make → selftest PASS
              → --render exit 0 → make test PASS, root viewer.exe отсутствует
git status    → M Makefile, M tests/run_tests.ps1, M README/AGENTS/PROJECT_MEMORY/Roadmap,
                ?? docs/reports/ (8 файлов)
```

## Риски / следующий шаг

- Клон проверяет закоммиченное состояние: пока эти правки не в git, клон собирает бинарь в корень
  (старый Makefile). После коммита клон-проверка должна дать `build/viewer.exe` — повторить.
- `mkdir -p` в recipe рассчитывает на POSIX-оболочку (GNU Make 4.4 для mingw использует `sh`,
  что подтверждено: `rm -f` в `clean` работает). На чистой `cmd.exe`-оболочке нужна ветка
  `mkdir` — проверять при E6.T3 (Linux/macOS).
- Старые отчёты перенесены как есть, их текстовые пути (`./viewer.exe`) — исторические; новые
  отчёты используют `build/viewer.exe`.
