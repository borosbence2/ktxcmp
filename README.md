# ktxcmp

A desktop tool for inspecting KTX2 textures and comparing them against reference
images, with emphasis on mipmap chain analysis.

Read [CLAUDE.md](CLAUDE.md) for architecture, invariants and metric definitions,
and [PLAN.md](PLAN.md) for the milestone order and the UI specification.

## Status

**M2 — decode layer + viewer.** Opens `.ktx2` and `.ktx`, reports format,
dimensions, level chain, supercompression and DFD state, decodes ASTC, BC5, BC7
and uncompressed to RGBA32F, and displays it. Pan with the middle button or
space-drag, zoom with the wheel, isolate channels with `1`-`4` (`0` for RGB),
`F` to fit, `Ctrl+1` for 1:1. The status bar reads out the texel under the
cursor. Comparison against a reference is M4.

Files arrive by `File > Open`, by drag-and-drop onto the window, or as command
line arguments (which is what "Open With" delivers).

### Tests

`container_test` is a console harness. It generates its own corpus with libktx's
writer, so it needs nothing checked in and runs in CI on both platforms:

```
ctest --test-dir build --output-on-failure
build/bin/container_test --synthetic       # same thing, directly
build/bin/container_test <file>...         # report on real files
```

The corpus covers every supported format in both container versions, plus
non-power-of-two and non-multiple-of-block-size dimensions, a short chain, an
unsupported format, a non-container, a truncated file, and a header that lies
about its dimensions.

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
