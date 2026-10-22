# Отчёт — откат бинаря в корень, `build/` заморожен как scratch (2026-10-22)

## Цель
Вернуть сборки «как раньше»: `make` кладёт `viewer.exe` в корень проекта, запуск и регрессия —
`./viewer.exe` из корня; каталог `build/` больше не используется для бинаря продукта и заморожен
(только временные файлы сборки вроде `*.o`). Причина owner'а: перенос бинаря в `build/` сломал
запуск — `assets/…obj` и `shaders/…glsl` резолвятся относительно cwd, и привычная команда
`./viewer.exe` перестала существовать.

## Сделано
Документация зафиксирована **до** кода (AGENTS.md §0.8):
- `PROJECT_MEMORY.md`: новый ADR §4.20 (бинарь в корень, `build/` = frozen scratch) + amendment к
  §4.19 (перенос отменён); §5 статус (`./viewer.exe --selftest`, `--idle-report` замер); §6 bullet
  про бинарь + отдельная запись расхождения ADR 19 → 20 (включая defect usage «put an .obj next to
  the exe»: поведение не реализовано, пути относительно cwd).
- `Roadmap.md`: E7.T1 переформулирован (✅, бинарь в корень, `build/` заморожен, `mkdir -p` убран);
  строка блокеров Discovery больше не содержит E7.T1.
- `README.md`: `make` → `viewer.exe` в корень; команды запуска `./viewer.exe …`; явная строка
  «запускать из корня репозитория»; структура: `build/` = frozen scratch.
- `AGENTS.md` §3/§4/§6: команды QA и README-правила → `./viewer.exe --selftest`, каталогная
  политика: бинарь в корень (git-ignored), `build/` frozen, в корне остаются только ignored
  артефакты.
Код/скрипты:
- `Makefile`: `BIN = viewer.exe`, цель `all: $(BIN)`, recipe без `mkdir -p` (каталог не создаётся);
  `OBJDIR = build` используется только в `clean` (`rm -f $(OBJDIR)/*.o`); `test: $(BIN)`;
  неиспользуемая переменная `OUT` удалена.
- `tests/run_tests.ps1`: `Start-Process -FilePath ".\viewer.exe"` + комментарий про запуск из корня.
Файл `build/viewer.exe` (артефакт предыдущего шага, untracked) удалён.

## Проверено (фактический вывод)
```
make
gcc -O2 -std=c11 -Wall -Wextra -Ideps/imgui src/main.c src/gl.c ... -o viewer.exe deps/imgui/build/libcimgui.a ...
root binary: 2379824 bytes        # viewer.exe в корень
items in build/: 0                # build/ не создаётся и пуст
selftest: PASS                    # exit 0
tests: PASS                       # make test, exit 0
```
Команды из README зелёные с новым путём (cwd = корень):
```
./viewer.exe --render assets/cube.obj tests/out/frame.bmp 800 600  exit 0
  GL 4.6.0 NVIDIA 616.92; wrote tests/out/frame.bmp (800x600, 23.7% non-background)
./viewer.exe assets/utah_teapot.obj --idle-report 3                exit 0
  idle: 497 frames in 3.00 s, fps 170.0, resize repaints 1
```
`.gitignore` покрывает бинарь и каталог: `git check-ignore -v` → `.gitignore:3:*.exe → viewer.exe`,
`.gitignore:22:build/ → build`. В корне из артефактов только `viewer.exe` (ignored), `build/` пуст.

## Дополнение этого же шага: стресс-фикстуры не публикуются
Owner'ское решение: локальный `assets/utah_teapot_test_2mil_tri.obj` (197510758 байт, 188.4 MB) не
нужен в удалённом репозитории. Добавлено правило `.gitignore` (строка 34, после `!assets/*.obj`):
`assets/*_test_*.obj`. Зафиксировано в `AGENTS.md` §4 и `PROJECT_MEMORY.md` §6. Проверено:
```
197510758 bytes = 188.4 MB
git check-ignore -v → .gitignore:34:assets/*_test_*.obj  assets/utah_teapot_test_2mil_tri.obj
git status --short  → файл не показан как ??
git add -A --dry-run → только 6 изменённых файлов + отчёт, ассета нет
```
Публикуемые модели остаются: `cube.obj`, `utah_teapot.obj`, selftest-фикстуры.

## Риски / следующий шаг
- Запуск с `cwd ≠ корень` по-прежнему даёт `no model found …`, exit 2 — это известное поведение
  (резолвер от бинаря не реализован, решение owner'а). usage-текст обещает «.obj next to the exe» —
  defect зафиксирован в §6, чинится отдельным шагом (резолвер путей или правка usage).
- `clean` ссылается на `build/*.o` — каталог может отсутствовать; `rm -f` это терпит.
- Открытые: E4.T2 (декодер текстур), E6.T3/T4, E1.T6, HUD on/off, release pin, menu-панель (🔵).

## Статус Roadmap
E7.T1 → ✅ (переформулирован: бинарь в корень, `build/` заморожен).
