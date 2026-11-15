# Stunt Car Racer — Agent Instructions

A Windows C++ port of the 1989 racing game on libdesktop's `core`, `platform`,
`render` and `ui` libraries. [README.md](README.md) covers the game, its
renderer, building and the licence.

## Layout

- `src/main.cpp` runs the game in a libdesktop `ui::shell`: its menus, the
  canvas the scene is drawn on (`AppDraw`), the fixed step that reads the held
  driving keys (`AppStep`), and the keys the game takes directly.
- `src/game.h` and `src/game*.cpp` are the game, one subject per file; each
  file's header comment says what it holds. `BuildGameMenu` in `game.cpp`
  builds the menus and their accelerators.
- `src/render.scene.*` is the renderer: the game's 3D faces drawn as libdesktop
  polygons by the painter's algorithm. Its comments document the ordering.
- `Tracks/`, `Sounds/` and `Bitmap/` are compiled in by
  `libdesktop_add_app(EMBED ...)` and read with
  `platform::embedded_resource_data`.

## Build and test

- Verify with `.\dd.ps1 test --label stuntcarracer`, which builds both
  configurations and runs the headless self-tests (`-test`).
- Run them after touching `game.drive.cpp`, `game.track.cpp` or any shared
  physics or track geometry: the chain drop on every track catches a car that
  sinks through or floats above the road.
- Add cases to `AppRunTests` in [game.tests.cpp](src/game.tests.cpp). The tests
  open no window, log to stderr and to `test_results.log` in the working
  directory, end with a `Total failures: N` line, and exit non-zero on any
  failure.
- `.clang-format` is the layout (tabs, Allman braces, 120 columns):
  `.\dd.ps1 fmt` applies it to `src` and `tests`, given clang-format on PATH
  (Visual Studio installs one in `VC\Tools\Llvm\x64\bin`). Data tables keep
  their hand layout between `// clang-format off` and `// clang-format on`.
  Both targets build at `/W4` with no warnings, and `.clang-tidy` names the
  bug-finding and performance checks the code is linted against.
- `dd.ps1` and `.dd/` are the upstream [dd](https://github.com/ZacWalk/dd)
  files, with hashes in [docs/dd-upstream.json](docs/dd-upstream.json); do not
  edit them. Keep project configuration in `dd.psd1`. The one dependency,
  libdesktop, is pinned by the `GIT_TAG` in `CMakeLists.txt`, not in
  `cmake/dd-dependencies.json`. Upstream dd documents its
  [CLI and MCP modes](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes)
  and [built-in commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands).

## Rules

- **Draw a stretch of track as one `BeginSolid` group, and a car with its
  shadow as one `Placement::Nearest` group of convex parts.** The painter's
  order comes from them: a face drawn outside a group is placed alone, by its
  farthest point, and is easily painted over wrongly.
- **Never cull the track.** It is single-sided, so culling makes the road vanish
  whenever the eye drops below it; a solid draws its faces turned from the eye
  first instead. Only a car's convex parts drop faces turned away.
- **Reversed-Z projection.** `Mat4PerspectiveFovLH` maps near to 1 and far to
  0; the renderer reads the near plane back from it to clip against.
- **Keep the original's integer arithmetic.** Its integer divisions truncate on
  purpose (the car's `VCAR_WIDTH / 4` is 40, not 40.5), so never make one
  floating-point. A conversion warning takes a `static_cast` that keeps the
  value the implicit conversion gave.
- Format game text with `format()` from `game.h`. It is standard `vswprintf`,
  so wide-string arguments take `%ls`, and text that overflows its buffer
  throws instead of truncating.
