# Отчёт E6.T5 — крупная модель (2 250 948 треугольника), 2026-10-22

## Цель

Проверить, что продукт (парсер, mesh-upload, кадр, picking) работает на модели в миллионы
треугольников, и получить численные метрики времени кадра и picking (Roadmap E6.T5).

## Что проверялось

Фикстура предоставлена владельцем: `assets/utah_teapot_test_2mil_tri.obj`
(188.4 MB; 1 129 252 `f`-строк → 2 250 948 треугольника после триангуляции quad'ов,
1 125 603 `v`, 1 131 021 `vt`, 1 126 627 `vn`, flags = authored normals + uv).
Фикстура **не публикуется** (`assets/*_test_*.obj`, решение E7.T1) — метрики относятся к локальной машине.

Два слоя проверки:

1. **CPU-зонд** `lab/probes/probe_bigobj.c` (без GL): `obj_parse_file` → буферы → peak RSS →
   `pick_triangle` на 60 лучах по средней зоне кадра 800×600 → `obj_mesh_free`.
   Пороги — относительные бюджеты (avg pick ≤ 200 ms, худший ≤ 400 ms, фикстура ≥ 1M tri,
   `vcount == triangles*3`, authored `vn`). Сборка:
   `gcc -O2 -std=c11 -Wall -Wextra -Isrc src/obj.c src/vmath.c src/camera.c lab/probes/probe_bigobj.c -o lab/probes/probe_bigobj.exe -lpsapi`.
   Первый запуск показал `parse: 0 ms` — таймер был написан с `QueryPerformanceCounter` вместо
   `QueryPerformanceFrequency`; чинится в том же шаге, числа ниже — после починки.
2. **Продукт**: `./viewer.exe --render <фикстура> tests/out/big_1280.bmp 1280 900` и
   `./viewer.exe <фикстура> --idle-report 3`; peak RSS снимался опросом `WorkingSet64` процесса.

## Числа

Зонд (`probe_bigobj: PASS`, wall 5.5 s):

```
fixture assets/utah_teapot_test_2mil_tri.obj: 197510758 bytes (188.4 MB)
parse: 3023 ms -> faces 1129252, triangles 2250948, vertices 6752844, flags 7
buffers: v 206.1 MB (6752844 x 8 floats), face_ids 8.6 MB, vert_ids 25.8 MB, total 240.4 MB
rss: before 4.4 MB, after parse 245.4 MB (+241.0 MB), peak 572.7 MB
pick: 60 rays (hits 26, misses 34), total 2394 ms, avg 39.9 ms, worst 44.4 ms
pick density: 17.7 ms per 1M triangles (triangles 2250948)
first hit: triangle 781961, identity face 392508
free: 11 ms, rss after free 5.0 MB (peak 572.7 MB)
probe_bigobj: PASS
```

Продукт:

```
offscreen 1280x900: wall 4.4 s, peak RSS 747.3 MB, exit 0
  model ... 1129252 faces -> 2250948 triangles, 6752844 vertices, normals (from vn) + uv
  camera: perspective, fov 60 deg, dist 1.12
  wrote tests/out/big_1280.bmp (1280x900, 12.0% non-background)      # GL 4.6.0 NVIDIA 616.92
interactive 1280x900 idle 3s: wall 7.3 s, peak RSS 764.2 MB, exit 0
  idle: 74 frames in 3.03 s, fps 23.7, resize repaints 1
```

Регрессия после шага: `make` (без warning'ов), `selftest: PASS`, `tests/run_tests.ps1 → tests: PASS`,
exit 0 — малые фикстуры не изменились; в корне репозитория новых артефактов нет (`tests/out/big_1280.bmp`
и бинарь зонда игнорируются), `git status` показывает только `lab/probes/probe_bigobj.c`.

## Выводы

- **Парсер и память выдерживают 2.26M tri**: 3.0 s на загрузку, peak RSS 747–764 MB
  (файл целиком в RAM 188 MB + CPU-буферы 240 MB + VBO 206 MB). `int vcount` (6.75M вершин)
  остаётся в пределах int32; путь освобождения подтверждён (`free` 11 ms, RSS падает до ~5 MB).
- **Кадр**: 23.7 fps при 1280×900 с MSAA 4 на RTX 4070 Ti, GL-ошибок нет, силуэт 12.0%.
  Ограничение — GPU/вsync, не парсер.
- **Picking — узкое место**: 39.9 ms на hover-луч при 17.7 ms на 1M tri (худший 44.4 ms).
  На 2M+ граней hover обновляется не чаще ~25 луч/с: подсветка грани «плывёт» при быстром
  движении мыши. Это численное подтверждение риска E3.T1 (BVH/облок), который остаётся ⬜.
- Ничего в продукте не менялось: шаг — измерение и фиксация метрик, правок `src/` нет.

## Открытые риски / следующий шаг

- E3.T1 BVH: без него picking на крупных mesh остаётся O(triangles) на каждый hover.
- `--idle-report` не измеряет picking (нет движения мыши); отдельный интерактивный замер hover
  можно добавить, если потребуется сравнение до/после BVH.
- Запуск с `cwd ≠ корень` всё ещё даёт `no model found` (known limitation, §6).
