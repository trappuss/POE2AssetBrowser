# D4AssetBrowser → POE2 code-reuse audit

A file-by-file pass over Diablo4AssetBrowser Native's generic (`[U]`/`[A]`) helpers, deciding what POE2
should reuse verbatim, what it already has, and what is D4-specific. Grounded in the AssetBrowser
template's portability marks (`[U]` copy the mechanism · `[A]` same mechanism, engine data · `[E]` D4's
answer to a question another engine may not ask).

## Taken this pass

| D4 file | Mark | What / why | POE2 landing |
|---|---|---|---|
| `util/TextReportDialog.h` | U | Read-only, monospace, copyable report pane. POE2 had a hand-rolled copy in `showMaterialReport` and used a non-copyable `QMessageBox` for Health check. | `src/util/TextReportDialog.h`; used by `ModelsTab::showMaterialReport` **and** `MainWindow::showHealthCheck` — one definition, can't drift (§18). |
| `app/ExportNotifier.h` | U | App-wide "Show in folder" completion notice (§15). POE2 only emitted status text with no way to reach the files. D4's `glbOptionsLine` (coupled to D4's exporter options) deliberately dropped. | `src/app/ExportNotifier.h`; MainWindow shows a status-bar **📂 Show in folder** button; every Export-menu path (model/parts/image/turntable/anim-loop, single & multi) reports through it. |
| `tabs/HintBar.h` | U | First-run dismissible tip teaching otherwise-invisible viewport controls (§23). | `src/util/HintBar.h`; one tip pinned atop the Models tab (click-to-select · orbit · F1 · Export menu), dismissed forever with ✕. |

## Already had (nothing to take)

| Area | Status |
|---|---|
| `util/PanelPersist.h` | **Identical** to POE2's — already ported. |
| `util/QueryTerm.h` | POE2's is **ahead** of D4's: structured `Query{and,not,meta,id}` + `isHexId` find-by-id (decimal & `0x` hash). D4's is the older `matches()`-only form. |
| `util/ExportLayout.h`, `util/NameTemplate.h`, `index/Facets.h` | Ported earlier. |
| `gl/GifEncoder.*`, `app/ExportCapture.*` (GIF ladder) | Ported verbatim in the GIF-export work. |
| verify-src / doc skeleton / wiki / SettingsDialog skeleton | Already present. |

## Skipped — D4-specific, POE2 doesn't have the problem

| D4 file | Why not |
|---|---|
| `app/ViewportSettings.h` | Solves drift between D4's **three** 3D tabs (Models/Wardrobe/Stable) with divergent key schemes. POE2 has one 3D tab, and its Settings already resets export keys by removal. |
| `util/AnimClipFilter.h` | Per-clip export filters exist for D4's 378-clip appearance sets. POE2's `.ast` clips number in the handful; all/current/none already covers it. |
| `util/AnimExportScope.h` | D4's five-source scope (original/sets/previewed/pulled/base) is its appearance + AnimSet taxonomy. POE2 clips come straight off the `.ast`; there is no pulled/sets concept. |
| `tabs/PanelBox.h` | A stacking-splitter panel design (reorder/hide). POE2 deliberately uses a **tabbed** panel (Parts/Animations/Attachments/Info); PanelBox also depends on D4's `BrowserTab.h`. |
| `util/CameraOrbitRow.h` | Shared yaw/pitch panel for D4's three camera popups; needs a turntable/auto-spin control POE2's viewport doesn't have. |
| `util/ProcQuiet.h` | Suppresses console-window flashes for spawned processes (git/curl). POE2 spawns none. |
| `util/CacheVersioning.h` | The *discipline* (version in the filename, bump on meaning change) POE2 already follows (`NameIndex` kCacheVersion, `bundle_index_v1`); the file itself is D4's cache list. |
| CASC / SNO / AppearanceMeta / cosmetics / d4data / UpdateCheck | Entirely D4 game-data or D4-infra. |

## Done in a follow-up pass

- **Viewport right-click part menu (§11/§13).** Built as an *adapted* menu rather than a verbatim copy
  of D4's D4-coupled `ViewportPartMenu.h` (20 KB, keyed on SNO/collection/outfit). POE2's
  `GLModelWidget` now raises `viewportPartMenuRequested` on right-click with the template's scoping
  (a part outside the selection replaces it, inside keeps it), and gained per-part hide/isolate
  (`setHiddenParts`/`isolateParts`/`clearHiddenParts`, honoured by both draw passes and by picking).
  `ModelsTab` builds the menu — frame · isolate · export selected parts · copy name(s) · show-all ·
  select-all · clear — reusing the same `exportParts()` path as the rest of the tab. Verified headless
  by `tools/part_isolate_test.cpp` (isolating one of two cubes removes it from the render).

## Recommended future (not taken — new UI / higher risk, needs its own pass)

- **Startup prune of stale cache versions.** The template calls for version-stamped caches pruned at
  launch; POE2 versions its caches but doesn't delete superseded files. Low priority (they're small).
- **Update check / "what's new after a patch".** D4's `deps/UpdateCheck` + `SnoIndex` build-history are
  network/GitHub-specific; only worthwhile once POE2 ships releases.

## Verification after the changes

Full in-container build green; `check-links.py` green (17 docs); `verify-src.py` green (82 files, after
fixing a pre-existing missing `RigMath.h` include in `AssetStore.cpp`); and all three headless render
tests — image export, GIF export, animated GIF — still PASS.
