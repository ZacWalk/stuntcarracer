# Stunt Car Racer — Agent Instructions

This is a Windows C++ port of the classic game **Stunt Car Racer** (originally by Geoff Crammond).

## Source files (src/)

- **platform.h** — Platform-independent types, constants, and API declarations
- **platform.win.cpp** — Windows platform layer (Win32, DirectSound, WinMain)
- **game.h** — Shared declarations: screen/render constants, menu IDs, fixed-point 3D math, COORD types, cross-module structs
- **game.cpp** — Top-level game logic: app entry points, frame loop, palette/resources/sound setup, viewpoints, sin/cos table, car model construction & rendering, world transforms
- **game.backdrop.cpp** — Sky/ground horizon and distant scenery rendering
- **game.drive.cpp** — Car physics and player driving behaviour
- **game.opponent.cpp** — Opponent AI, car-to-car collision detection, race position tracking
- **game.track.cpp** — Track data loading, conversion, and rendering
- **render.software.h / .cpp** — Software 3D renderer, platform-independent
- **game.rc / resource.h** — Win32 resources and auto-generated resource IDs
- **game.manifest** — Win32 application manifest
- **game.tests.cpp** — Headless self-tests (chain-drop / settling regression suite across all tracks)

## Build & test

Build via the `Build Debug Win32` task (or Release) — outputs land in `exe/`.

Run the headless self-tests with the `-test` (or `/test`) command-line flag:

```pwsh
.\exe\StuntCarRacerWin32Debug.exe -test
```

Tests run without creating a window, write a per-frame log to stdout, and exit
with code `0` on success / non-zero on any failure (final `Total failures: N`
line). Add new cases by extending `AppRunTests` in [game.tests.cpp](src/game.tests.cpp).
Always run `-test` after touching `game.drive.cpp`, `game.track.cpp`, or any
shared physics/track-geometry code — the chain-drop suite catches a lot of
"car sinks through / floats above the road" regressions for free.

## Rendering system

A small, single-mode software rasterizer in `render.software.*`. The frame
buffer is a `uint32_t` BGRA backbuffer; the platform layer blits it to the
window each frame.

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