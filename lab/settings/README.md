# lab/settings — прототип формата настроек

**Что проверяет:** `probe_settings.c` — CPU-зонд (без GL/GLFW) для формата `settings.ini`:
одна таблица дескрипторов (`FIELDS[]`) задаёт ключ, подпись виджета, группу, тип, диапазон и
дефолт, из неё же печатается файл и сверяется парсер.

**Команда**

```
gcc -O2 -std=c11 -Wall -Wextra lab/settings/probe_settings.c -o trash/probe_settings.exe
./trash/probe_settings.exe
```

**Что подтверждено (числа, 2026-10-23)**

```
probe: 20 fields, 0 duplicate keys
defaults: 0 out-of-range values
save: 20 key lines for 20 fields
roundtrip: 20 applied, 0 drifted of 20
determinism: 627 bytes, identical = yes
clamps: 4 applied, 4 clamped, fov 90.0 spec 1.00 dist 0.30 amb 2.00
malformed: 0 applied, 3 skipped, fov 60.0
missing file: rc 1, missing 1, fov 60.0
crlf: 4 applied, 0 skipped, fov 70.0 ortho 1 light_y 0.900
dirty: 0 after load, 1 after a widget change
probe_settings: PASS   (exit 0)
```

**Формат:** `key = value`; пустые строки и `#`-комментарии игнорируются; RGB — три числа
через запятую; bool — `true/false/yes/no/1/0`; выход за диапазон → кламп с печатью числа;
неизвестный ключ / битое значение / строка без `=` → предупреждение с именем ключа и номером
строки, дефолт остаётся.

**Две правки самого зонда (зафиксированы, не «зеленование»):** заголовок-комментарий содержит
`key = value`, поэтому подсчёт ключей должен пропускать строки с `#` (было 21 вместо 20); в
CRLF-фикстуре 4 ключа (`fov_deg`, `ortho`, `wireframe`, `light_dir`), ожидание было 3.

**Перенос:** логика переехала в `src/settings.c` / `src/settings.h` (E5.T7); зонд остаётся
документацией эксперимента и собирается отдельно.
