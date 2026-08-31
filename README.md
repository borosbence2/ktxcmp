# ktxcmp

A desktop tool for inspecting KTX2 textures and comparing them against reference
images, with emphasis on mipmap chain analysis.

Read [CLAUDE.md](CLAUDE.md) for architecture, invariants and metric definitions,
and [PLAN.md](PLAN.md) for the milestone order and the UI specification.

## Status

**M4 — reference comparison.** Everything above, plus a PNG reference in slot B
(8-bit and 16-bit) and compare mode 1: KTX mip 0 against the reference with no
resampling. Reports PSNR-RGB, RMSE, SSIM, and max error with its coordinate,
with alpha always separate. Diff view mode with a 1x/4x/8x/16x gain, and the
status bar reads A, B and the delta under the cursor. Modes 2 and 3 are M5.

Drop a file on a slot card to load it there, or anywhere else and the type
decides. PNG carries no dependable colour-space tag, so sRGB is assumed and the
slot B card exposes the override; gAMA is never consulted.

Metrics run on a worker: SSIM alone is five Gaussian blurs over every texel.
Measured over a 1307-frame session driving a 12-level 2048 square ASTC file:
mean 6.96 ms, and exactly one frame over 16.7 ms, which is the initial texture
upload.

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
