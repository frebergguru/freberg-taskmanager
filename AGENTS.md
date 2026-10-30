# Working on Freberg.guru - Task manager

A task manager for Linux in C++20 and Qt 6 Widgets, in English and Norwegian. See [README.md](README.md) for what it does, [TODO.md](TODO.md) for what's left and [CHANGELOG.md](CHANGELOG.md) for what changed.

## Rules that aren't optional

**Names**
- Never name the old operating system or its vendor behind the classic look, or the existing task manager the feature list came from. That covers code, file names, UI text, docs and commit messages. Say "classic" or "the reference app". Check before each commit.

**Honest readings**
- Every reading is a `Reading<T>` with a state: Measured, Estimated, Unsupported, AccessDenied or NotYetSampled. Never show a zero you didn't measure, and never invent a value. A chip's "no limit" placeholder (such as 65261.85 °C) is not a reading.
- Missing samples are NaN and stay gaps in graphs, exports and recordings.
- Estimates say so in the UI.

**Safe actions**
- A process is a PID plus its start time. Actions go through a pidfd and verify the start time first.
- Anything destructive asks first. Tests touch only throwaway targets they create themselves, never real services or packages.

**Low resource use**
- Views state what they need in `wants()`, and what isn't on screen isn't sampled. With the window hidden and nothing enabled, no sampler runs, and a test checks that.
- Costly extras are off until asked for, or read only while their column shows (`SubProcessGpu`, `SubProcessNetwork`).
- Never start external tools on a timer. Keep files open and re-read them; don't allocate per line.
- After changing a hot path, run `tools/measure-idle.sh build-release/freberg-taskmanager` and report the real numbers.

**Design**
- The classic design is the default: bevels, square corners, navy title bar, stacked tabs, drawn at today's sharpness (smooth text in points, antialiased curves, vector icons).
- Themes come as a family with a light and a dark member. Every member must pass `tests/test_scheme.cpp` (WCAG AA).
- Modern has no glow or neon effects at all. Only the Neon Glow theme glows, through `glowStroke()`, which draws nothing in other themes.
- Classic drawing lives in `app/style/style.cpp` and the flat designs in `app/style/style_modern.cpp`. Hand-drawn widgets use `raisedPanel()`, `sunkenPanel()` and `strokeSeries()`, not raw bevels.

**Accessibility**
- Keyboard for everything, a role and name for every control (a test walks every page), nothing told by colour or sound alone, text from 100% to 200% that never clips, no forced timing.
- Keep `Docs/accessibility-en.md` and `Docs/accessibility-nb.md` truthful and in step. Don't claim a test that wasn't run.

**Text**
- All UI text goes through `tr()`, and new strings need Norwegian in `i18n/freberg-taskmanager_nb.ts` before committing. English plurals are in `i18n/freberg-taskmanager_en.ts`.
- Refresh with `/usr/lib/qt6/bin/lupdate -no-obsolete app -ts i18n/freberg-taskmanager_nb.ts -target-language nb_NO`.

**Privacy**
- Nothing is sent anywhere. The app makes no network requests, and any future one needs a generic User-Agent and no personal data.

**Git**
- Commit locally, and don't push unless asked.
- Commits use this repo's local identity with the GitLab private address. Don't change it, and never use a personal address.

**Releases**
- Everything shipped must be stripped. `tools/release.sh` stops if `tools/check-stripped.sh` finds anything else.
- The app is under GPL-3.0-or-later. `tools/release.sh` releases only committed code, puts the matching source next to the packages, and stops if `tools/third-party-notices.sh` can't name the license of a bundled library. Publish the source package with the others.

## Shared machine

This machine is shared with other sessions. Before a build, test run or anything else heavy, check who is busy and ask them, run one heavy job at a time at `-j2`, and say when you're done. Keep scratch files in `scratchpad/` or `build/`, never in `/tmp`.

Never open a visible window while testing unless the user has asked for it. Use `QT_QPA_PLATFORM=offscreen`, or `tools/headless-wayland.sh` for the real Wayland code.

## Layout

```
core/       No Qt: readings, snapshots, parsers, history, traces, disk scan, benchmarks
platform/   backend.h is what the UI may ask of an OS. linux/ implements it, one sampler per subsystem
app/        Qt Widgets: hub (sampler thread), shell (window, tabs, title bar, menu bar),
            views (one per page), style, popout (skins), widgets
i18n/       Translations
tests/      test_parse, test_samplers, test_trace, test_scheme, test_ui
tools/      measure-idle, release, check-stripped, headless-wayland, grant-sensor-access
packaging/  Desktop file, icon, PKGBUILD
Docs/       Skin format, recording format, permissions, accessibility statements
```

The UI never reads the system directly. It asks `ISystemBackend`, so another operating system can be added without touching the views. Paths go through a configurable root, so tests can use a fake `/proc` and `/sys`. Fixture tests belong in `tests/test_samplers.cpp`, and values copied from real hardware make the best fixtures.

`app/shell/view.h` has `kProcessesView`, the position of Processes in the page list. Update it if the order in `app/views/views.cpp` changes.

## Commands

```sh
cmake -S . -B build && cmake --build build -j2
ctest --test-dir build --output-on-failure

# Pictures of every page, with no window
QT_QPA_PLATFORM=offscreen build/freberg-taskmanager --screenshots scratchpad/shots --schemes classic-light,modern-dark --text-sizes 100,200
QT_QPA_PLATFORM=offscreen build/freberg-taskmanager --skin-screenshots scratchpad/skins

# The real Wayland plugin, on a private compositor with no visible window
(cd build/tests && ../../tools/headless-wayland.sh ./test_ui)

# Packages, into dist/
tools/release.sh 2
```

`--view N`, `--start-test` and `--export-icon FILE` also help with testing. Look at the pictures: several real bugs only showed up there.

On the private compositor, menu tests fail because it has no pointer or keyboard, so Wayland refuses popups. Everything else should pass.
