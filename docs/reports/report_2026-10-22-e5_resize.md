# Отчёт E5.S1 — repaint на resize окна (виток 9, E5)

**Цель:** изменение размера окна рисзуется сразу и контекст кадра (viewport, аспект, HUD)
обновляется в том же кадре; чёрные полосы и ожидание конца drag'а убираются.

## Причина (проверена, не assume'ита)
`docs/INPUT.md` GLFW: на Windows перетаскивание границы входит в **modal loop** внутри
`DefWindowProcW` — пока он активен, `glfwPollEvents` (и коллек `on_framebuffer_size`) не вызываются.
Наш луп не рисовал ничего до конца drag'а, а на первом кадре после drag'а рисовался старый
размер → черные полосы. `on_resize` в `src/main.c` только ставил `dirty` и не трогал `a->width/height`;
`sync_framebuffer_size()` вообще вызывался из `--shot`.

## Сделано
- `src/main.c` `sync_framebuffer_size(app*)` — читает `glfwGetFramebufferSize` и обновляет
  `a->width/height` (возвращает 1, если размер изменился).
- `draw_scene`: `sync_framebuffer_size(app)` + `glfwSwapBuffers(app)` — цель кадра следует за окном,
  когда бы ни был вызван repaint; offscreen/FBO путь не трогает окно.
- `on_framebuffer_size` → вызывает `sync_framebuffer_size` (для non-modal resize: X11, максимизация).
- `on_refresh` (`glfwSetWindowRefreshCallback`) → sync + `draw_scene(NULL)` + swap: единственный
  repaint во время modal drag'а. Счётчик `app.refreshes` показывает, сколько раз это произошло.
- Лог в интерактиве: `resize: live repaint - frame size read from the window every frame, redraw from WM_PAINT`
  и одна строка `resize: WxH (repaints during the drag: N)` на завершившийся burst.
- `--idle-report` печатает `resize repaints N`.

## Проверено (фактический вывод)
```
make → viewer.exe (без warning'ов), exit 0
./viewer.exe --selftest:
  selftest: frame size follows the target immediately (resize repaint)
    ok   size re-read from the window: 7x3 -> 120x1 (changed=1, window says 120x1)
    ok   grown target 256x192 -> 512x384: corner vs background max diff 0 (budget 12),
         black pixels in the newly exposed area 0, silhouette coverage 27.54% (>2%)
  selftest: PASS exit 0
tests/run_tests.ps1 → tests: PASS exit 0
trash/probes/probe_resize.ps1 (окно меняется извне SetWindowPos, луп не поллит):
  exit: 0
  ok   stderr empty (no GL errors during the resize bursts)
  ok   idle: 665 frames, fps 170.0, repaints 3
  ok   repaints come from WM_PAINT, not from the poll loop (>= 2 bursts)
  sizes after each burst: 1264x861 684x441
  ok   frame size follows the window: grown client larger than shrunk one
  ok   repaint counter advanced between bursts
  resize probe: PASS exit 0
```

## Риски / следующий шаг
- `on_refresh` рисует из callback'а — состояние камеры/GL одинаково с лупом, но если в лупе
  появится ввод, требующий exclusive-доступ (например асинхронная загрузка модели), repaint надо
  будет сериализовать.
- На drag'е repaint делает полный `render_scene` (для 1M граней это ~full cost на каждый WM_PAINT).
  Метрика на `assets/teapot_1m.obj` не снята — ассета нет в репозитории (открытая задача).
- `GetClientRect/GetDIBits`-захват окна из PowerShell не работает (нет System.Drawing.Common),
  поэтому проверка «нет чёрных полос» численная через FBO-цель в selftest + счётчик repaint.

## Roadmap
E5.S1 ✅ (строка про мгновенный repaint + метрики), E5.T5 ✅ (метрики обновлены). Коммит `4578870`
(код + документы), затем правка документов (E5.R3 убран как несуществующий id, метрики E5.T5
обновлены), worktree чистый.
