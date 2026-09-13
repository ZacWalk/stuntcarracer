# Stunt Car Racer

[![Build](https://github.com/ZacWalk/stuntcarracer/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/stuntcarracer/actions/workflows/build.yml)

A Windows C++ port of Geoff Crammond's 1989 elevated-track racing game, based on
[stuntcarremake](http://sourceforge.net/projects/stuntcarremake/). Two cars race
head-to-head over ramps and broken bridges; the track is the obstacle.

I used to love this game when I was a kid.

![Stunt Car Racer running on Windows](stuntcarracer.png)

## How it works

Everything on screen comes from a **software 3D renderer** in
[src/render.software.cpp](src/render.software.cpp) — no Direct3D, OpenGL or GPU
acceleration:

- 4×4 matrix pipeline (world / view / perspective), near-plane clipping in
  homogeneous space
- Edge-function rasterizer with perspective-correct interpolation via 1/w
- Z-buffer depth testing, backface culling, flat shading with one directional light
- Affine 2D primitives for the HUD and text

The finished RGBA framebuffer is blitted to the window by the platform layer's
`present_pixels`, outside any paint handler, so the game keeps control of its
frame loop. Audio is XAudio2 through `pf::sound_buffer`.

All game data — tracks, sounds and textures — is compiled into the executable.

## Playing

Arrow keys drive, <kbd>Ctrl</kbd>+<kbd>Up</kbd> is boost, <kbd>Space</kbd>
toggles the camera. In the track menu, <kbd>1</kbd>–<kbd>8</kbd> pick one of the
eight tracks and <kbd>S</kbd> starts the race. <kbd>F5</kbd> shows statistics and
<kbd>F9</kbd>/<kbd>F10</kbd> change game speed.

## Building

Requires Windows x64 and Visual Studio with the Desktop C++ workload. The
vendored [dd](https://github.com/ZacWalk/dd) runtime locates Visual Studio and
uses the CMake and Ninja that ship with it.

```powershell
.\dd.ps1 build        # both configurations
.\dd.ps1 test         # build and run the suite
```

Output is `exe\stunt-car-racer-64.exe`. The platform layer comes from
[platform-h](https://github.com/ZacWalk/platform-h) via `FetchContent`; a sibling
`../platform-h` checkout is used automatically when present.

## Documentation

[AGENTS.md](AGENTS.md) — conventions for contributors and coding agents.
