# AGENTS.md

Guidance for AI agents and contributors working on OGG (OffGrid Games).

## Never use Python

Do not use Python for build tools, code generation, scripts, or automation in this repository. **Never.**

Use C or C++ projects instead. Shell (`sh`) and Make are acceptable for orchestration only.

## Do not create root folders without permission

Do not add new top-level directories (e.g. `scripts/`, `.Launcher/`, `bin/`) without explicit user approval.

Allowed roots:

- `src/` — application projects
- `libs/` — shared headers and platform code
- `tools/` — C/C++ build utilities (each tool may have its own `CMakeLists.txt`)
- `build/` — CMake output (generated, not committed)

## Project layout

Application code lives under `src/`:

```
src/OffGridGamer.OffGridGames.<Project>/
```

Examples:

- `src/OffGridGamer.OffGridGames.Client/`
- `src/OffGridGamer.OffGridGames.Server/`
- `src/OffGridGamer.OffGridGames.Launcher/`

Build utilities live under `tools/` via `tools/CMakeLists.txt`:

- `tools/icongen/` — converts `icon.svg` to `icon.ico` (nanosvg, runs before launcher build)
- `tools/stop-port/` — stops processes listening on given TCP ports (`ogg.stop-port`)
- `tools/versionbump/` — bumps the last segment of `src/version.txt` (`ogg.versionbump`)

Shared version: `src/version.txt` (read by CMake for client, server, and launcher).

Platform networking: `libs/win/` (Windows) and `libs/nix/` (POSIX).

## Build

Use Make from the repo root (MinGW UCRT toolchain):

```cmd
make build
make server
make launcher
make icon
make version
make clean
```

Do not use `go run`, `python`, or ad-hoc `cmake` invocations unless updating the Makefile.

## Windows

- Default toolchain: `/ucrt64/bin/g++.exe` (see Makefile `CMAKE_CONFIG`).
- `make server` builds the project, stops listeners on 8123/8124/8125 via `ogg.stop-port`, then rebuilds and runs `ogg.server`.

## Windows release build flags (MinGW)

Root `CMakeLists.txt` forces a small, static Release build on Windows/MinGW:

| Flag / setting | Effect |
|---|---|
| `-Os` | Optimize for size over speed |
| `-ffunction-sections -fdata-sections` | Per-function/data sections for linker GC |
| `-fno-rtti` | No RTTI (`dynamic_cast` on polymorphic types, `typeid`) |
| `-fno-exceptions` | No C++ exceptions (`throw`/`try`/`catch` unavailable) |
| `CMAKE_INTERPROCEDURAL_OPTIMIZATION` (LTO) | Cross-TU inlining and dead-code elimination |
| `-static-libgcc -static-libstdc++` | Static C++ runtime in shipped `.exe` files |
| `-Wl,--gc-sections -s` | Drop unused sections; strip symbols |

POSIX builds use size/GC/strip but **not** `-fno-exceptions` or LTO.

### What this means for C++ code

- Do not use `throw`, `try`, `catch`, or libraries that require exceptions.
- Avoid `dynamic_cast` / `typeid` on class hierarchies.
- Standard library facilities that may rely on exceptions internally (`std::function`, some iostream paths) can be fragile under `-fno-exceptions` + LTO even when they compile.
- Win32 UI code runs inside `WndProc` callbacks; an access violation there surfaces as `STATUS_FATAL_USER_CALLBACK_EXCEPTION` / sudden process exit, not a C++ stack trace.

### Client XML UI crash (v1.0.19) — lessons

Two separate issues were fixed in `libs/common/ui/xml_ui.cpp`:

1. **Root cause:** `layout_tree()` measured text via DirectWrite before `IDWriteFactory` was created. `format_for_rule()` called `state.write_factory->CreateTextFormat(...)` on a null pointer during layout (before the first `WM_PAINT`). **Fix:** `ensure_write_factory()` before any DirectWrite use in layout or paint.
2. **Hardening:** `create_inputs()` used a recursive `std::function` lambda inside layout, which runs during `SetWindowPos` / `WM_SIZE` on a child HWND. Replaced with an iterative `std::vector<UiNode*>` walk to avoid type-erasure overhead and deep recursion in a message-handler path.

Other hardening in the same file: `paint_ready` / `layout_in_progress` guards, null-safe D2D brush creation, skip zero-size HWND targets.

### `std::function` — when to use it here

`std::function` is still used at **API boundaries** where it is acceptable:

- `libs/common/http_client.hpp` — optional download progress callback
- `src/OffGridGamer.OffGridGames.Launcher/main.cpp` — `animate_status(..., std::function<bool()>)`
- `libs/common/ui/xml_ui.hpp` — `ButtonHandler` typedef

**Prefer to avoid `std::function` in:**

- `WndProc` handlers and anything they call synchronously (layout, paint, hit-test)
- Recursive tree walks (layout, input creation, paint traversal)
- Hot paths that may re-enter via `SendMessage` / `SetWindowPos`

**Prefer instead:**

- Iterative walks with `std::vector` stack/queue
- Plain function pointers or templated callbacks with a fixed signature
- Member function + `userData` pointer for C-style callbacks

### Recommendation: keep flags or relax?

**Keep the current Windows flags** (recommended). OGG ships small static executables; `-Os`, `-fno-exceptions`, `-fno-rtti`, LTO, and `--gc-sections` are intentional for release size and deploy simplicity.

Do **not** remove flags to “fix” UI bugs — fix initialization order, null checks, and callback-safe algorithms instead (as with the DirectWrite factory and iterative `create_inputs`).

**Alternative (only if debugging becomes painful):** relax `-fno-exceptions` for a local `Debug` CMake configuration or a single target via `target_compile_options`. That slightly increases binary size and static-runtime bloat but improves libstdc++ ergonomics for `std::function` and some STL helpers. Do not relax flags globally without an explicit user decision.

**On `std::function` vs avoiding it:** keep strict flags; avoid `std::function` in UI hot/reentrant paths; keep it for optional, infrequent callbacks (HTTP progress, launcher animation predicates, button handlers). The readability cost of iterative walks in layout code is small compared to hard-to-diagnose callback crashes.

### Client shell frame (`libs/common/ui/client_shell.hpp`)

Reusable borderless client window layout used by `ShellWindow` when `view().minimal_chrome` is set:

| Piece | Role |
|---|---|
| `LayoutSpec` | Constants: 320px login panel, 40px drag band, chrome height, −4px button Y offset, 32px button width |
| `measure_panels(w, h)` | Returns `PanelLayout` rects for login, hero, and top-right chrome overlay |
| `allows_window_drag(pt, width, options)` | Hit-test for borderless drag; default 40px band, optional `full_window` |
| `ChromeOverlay` | Layered HWND (`UpdateLayeredWindow`); glyphs always visible; hover fills only while hovered |

**Transparent backgrounds:** XML UI `Button` and `ChromeOverlay` skip background fill when no `bg_color` is set (or `bg_color="transparent"`). Hover background is drawn only when `hover_bg_color` is set (not `transparent`). Omitting `bg_color` is not a default color — it means no fill.

**Reuse pattern:** call `measure_panels()` to position child HWNDs (XML login panel, hero bitmap, version label). Create one `ChromeOverlay` with `ensure(parent, user_data, on_close, on_minimize)` and `layout(shell_width)`. Wire close/minimize via plain function pointers — not `std::function`.

`ShellWindow::ensure_client_chrome_overlay()` owns hero + chrome + login layout; new client screens should use the same `client_shell` helpers rather than duplicating chrome math in `shell_window.cpp`. Set `view().client_drag_full_window = true` to allow dragging from the entire window instead of the 40px title band.

### App settings (`appsettings.json`)

| Path | Role |
|---|---|
| `%LOCALAPPDATA%/OffGridGames/appsettings.json` | **Canonical** settings (AI keys, appearance, typography, `source_dev_dir`) |
| `config/appsettings.example.json` | Committed template only — copy to AppData or let first `load()` seed from repo `config/appsettings.json` |
| `build/generated/client_theme.hpp` (etc.) | Compile-time theme from AppData via `ogg.sync-settings` |

**You do not need `config/primary_color.txt` or `config/secondary_color.txt`.** Admin Save writes AppData only; `export_for_build()` regenerates `build/generated/*`.

| Make target | Settings sync |
|---|---|
| `make sync_settings` | Runs `ogg.sync-settings` (AppData → `build/generated/`) |
| `make build` / `make client` / `make admin` / `make launcher` | All run sync before building UI targets |

### Login hero images and Admin (`make admin`)

| Path | Role |
|---|---|
| `%LOCALAPPDATA%/OffGridGames/login_images/` | Persistent gallery of generated `.jpg` hero images |
| `%LOCALAPPDATA%/OffGridGames/selected_login_image.txt` | Filename chosen for the next client embed |
| `src/OffGridGamer.OffGridGames.Client/hero_art.jpg` | Build-time copy target (`sync_hero`) embedded via `hero_art.rc` |

| Make target | Effect |
|---|---|
| `make hero` | Download/crop a new image into AppData `login_images/`, select it, copy to `hero_art.jpg` |
| `make client` | Runs `sync_hero` (AppData selection → `hero_art.jpg`) then builds client |
| `make admin` | Builds and launches `ogg.admin` — pick login images from the AppData gallery |

Admin UI lives in `src/OffGridGamer.OffGridGames.Admin/` and uses `XmlUiHost` plus `Gallery`/`Image` tags for the login image grid.
