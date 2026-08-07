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
