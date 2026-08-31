# ktxcmp

A desktop tool for inspecting KTX2 textures and comparing them against reference
images, with emphasis on mipmap chain analysis.

Read [CLAUDE.md](CLAUDE.md) for architecture, invariants and metric definitions,
and [PLAN.md](PLAN.md) for the milestone order and the UI specification.

## Status

**M1 — container layer.** Opens `.ktx2` and `.ktx`, reports format, dimensions,
level chain, supercompression and DFD state, and lets you step the mip levels.
No pixels are decoded yet; that is M2.

Files arrive by `File > Open`, by drag-and-drop onto the window, or as command
line arguments (which is what "Open With" delivers).

`container_test` is a console harness that opens files and checks the M1
invariants without needing a window:

```
build/bin/container_test <file>...
```

## Build

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

The binary lands in `build/bin/`. The first configure clones SDL3, Dear ImGui,
astc-encoder and KTX-Software into `build/_deps` and the first build compiles all
of them, which takes several minutes; later builds are incremental.

- **Windows**: MSVC x64, static CRT (`/MT`), so the executable is portable.
  With the default Visual Studio generator, add `--config RelWithDebInfo` to the
  build command. A developer command prompt plus `-G Ninja` is faster.
- **macOS**: add `-DCMAKE_OSX_ARCHITECTURES=arm64`.

`CMakePresets.json` wraps the above as the `default`, `debug` and `macos-arm64`
presets.

## Dependencies

Pinned in [cmake/Dependencies.cmake](cmake/Dependencies.cmake); the list is fixed
by CLAUDE.md and is not extended without asking.

| Purpose | Library | Version | Integration |
|---|---|---|---|
| Window + input | SDL3 | `release-3.4.14` | fetched, static |
| GUI | Dear ImGui (docking) | `v1.92.9b-docking` | fetched, built by `third_party/imgui` |
| ASTC block decode | ARM astc-encoder | `5.7.0` | fetched, decompressor only |
| KTX2 + KTX1 container, DFD, supercompression | KTX-Software | `v4.4.2` | fetched, static |
| BC5 + BC7 block decode | `bcdec.h` | v0.97 | vendored |
| PNG load | `stb_image.h` | v2.30 | vendored |

No OpenGL loader is fetched. ImGui's GL3 backend carries its own, and our code
only calls entry points `opengl32.lib` exports directly.

## Layout

The panel layout is built in code the first time the app runs and then owned by
`imgui.ini`, which lives beside the other per-user settings
(`%APPDATA%\ktxcmp\ktxcmp\` on Windows, `~/Library/Application Support/ktxcmp/`
on macOS) rather than in the working directory. **View > Reset layout** rebuilds
the default.

The side rails are a fixed width and the viewport takes the slack, so the layout
holds down to the 680px-wide minimum the window enforces.
