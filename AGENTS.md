# Stunt Car Racer — Agent Instructions

This is a Windows C++ port of the classic game **Stunt Car Racer** (originally by Geoff Crammond).

## Source files (src/)

- **main.cpp** — `ui::app` entry point, drawn menu-bar/frame host, software-surface element, input forwarding and the game's own frame loop
- **game.h** — Shared declarations: screen/render constants, menu IDs, fixed-point 3D math, COORD types, cross-module structs, and null-safe sound helpers
- **game.cpp** — Top-level game logic: frame update/render, menu commands and accelerators (`BuildGameMenu`), palette/resources/sound setup, viewpoints, car construction and world transforms
- **game.backdrop.cpp** — Sky/ground horizon and distant scenery rendering
- **game.drive.cpp** — Car physics and player driving behaviour
- **game.opponent.cpp** — Opponent AI, car-to-car collision detection, race position tracking
- **game.track.cpp** — Track data loading, conversion, and rendering
- **render.software.h / .cpp** — Software 3D renderer writing a `core::surface`
- **game.tests.cpp** — Headless self-tests (chain-drop / settling regression suite across all tracks, menu shortcuts, pause and minimise rules)

The platform layer lives in the shared **libdesktop** repository's `core`,
`platform`, `render` and `ui` libraries. The application is a `ui::app`; its
menu bar is a `ui::control` in the frame host's element tree, and the renderer's
`core::surface` is uploaded as a texture by the content element. Tracks, samples
and road textures are compiled into the executable by
`libdesktop_add_app(EMBED ...)` and fetched with
`platform::embedded_resource_data`.

## Build & test

```pwsh
.\dd.ps1 build                        # both configs; or: .\dd.ps1 build release
.\dd.ps1 test --label stuntcarracer   # build both, then run the headless self-tests
```

The vendored dd driver sets up the MSVC environment itself. Output lands in
`exe/` (`stunt-car-racer-64.exe`, `-64d` for Debug). An unfiltered
`.\dd.ps1 test` also opens the game's window for a second as a smoke check.
The vendored [dd](https://github.com/ZacWalk/dd) files are the upstream
`dd.ps1` and `.dd/` tree; [docs/dd-upstream.json](docs/dd-upstream.json)
records their hashes. Keep project configuration in `dd.psd1`; the one
dependency, libdesktop, is pinned by the `GIT_TAG` in `CMakeLists.txt`, not in
`cmake/dd-dependencies.json`. Use `.\dd.ps1 commands --json` to list the
project command surface. Upstream dd owns the documentation for
[CLI and MCP mode](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes)
and [built-in commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands).

Tests run without creating a window, write a per-frame log to stdout, and exit
with code `0` on success / non-zero on any failure (final `Total failures: N`
line). Add new cases by extending `AppRunTests` in [game.tests.cpp](src/game.tests.cpp).
Always run `-test` after touching `game.drive.cpp`, `game.track.cpp`, or any
shared physics/track-geometry code — the chain-drop suite catches a lot of
"car sinks through / floats above the road" regressions for free.

## Rendering system

A small, single-mode software rasterizer in `render.software.*`. The frame
buffer is a packed `core::surface`; the UI content element uploads it as a
texture each frame.

**Pipeline (per triangle):**
1. World/View/Projection matrices set via `SetWorldMatrix` / `SetViewMatrix` /
   `SetProjectionMatrix`. They are combined lazily into `m_worldViewProj` on
   the next draw.
2. `TransformToClipSpace` produces homogeneous clip-space verts.
3. Flat per-face lighting is applied (`ApplyFaceLighting`): face normal in
   object space → world space → `N·L` with fixed light dir, ambient, and
   diffuse → vertex colours scaled.
4. `ProcessAndRasterizeTriangle` near-plane-clips against `m_nearClipW`
   (derived from the projection matrix in `SetProjectionMatrix`, so it always
   matches the actual frustum), perspective-divides, and rejects triangles
   past the far plane. **No back-face culling**: the track is single-sided,
   so culling caused the road to vanish whenever the eye crossed below the
   surface (banked turns, hill crests, hard landings). The rasterizer handles
   both windings via signed area; back-facing triangles shade as ambient-only
   because `ApplyFaceLighting` clamps `N·L` at 0.
5. `RasterizeTriangle` does a top-left-fill-rule edge-function rasterizer with
   incremental per-pixel stepping. Two inner loops: textured (perspective-
   correct UVs) and Gouraud-shaded RGB. **Reversed-Z depth**: double Z-buffer
   cleared to 0.0, test is `z > depthBuf`, projection maps near→1 and far→0.
   This keeps depth precision usable across the huge near/far ratio (0.5 /
   131072) — the standard near→0 mapping crushes everything past view-z ≈
   1000 into the last few double ULPs and produces far-over-near artifacts.
