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

