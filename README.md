# ktxcmp

A desktop tool for inspecting KTX2 textures and comparing them against reference
images, with emphasis on mipmap chain analysis.

Read [CLAUDE.md](CLAUDE.md) for architecture, invariants and metric definitions,
and [PLAN.md](PLAN.md) for the milestone order and the UI specification.

## Status

**M6 — normal-map mode.** All three compare modes, plus BC5 normal-map
interpretation. BC5 is read as a normal map by default: z is reconstructed as
sqrt(max(0, 1 - x^2 - y^2)), both sides are remapped the same way, and the
metrics change wholesale to mean, median, p95 and max angular error in degrees
with the deviation of |n| from 1. PSNR is not reported for a normal map at all.
The format banner states the detection and offers a raw RG override, which puts
the colour metrics back. The pixel inspector follows: directions and an angle,
not 8-bit ints.

Mode 1 is KTX mip 0 against a PNG reference with no resampling; mode 2
downsamples the reference to each level; mode 3 halves level N-1 and compares it
to level N, needing no reference. Error-by-level plot you can click to select a
level, warning badges on the mip strip, level table, and CSV export whose header
carries mode, filter, both linear-light flags and normal-map mode.

Five resampling filters (box, triangle, Kaiser, Lanczos3, Mitchell). Two
linear-light toggles with deliberately opposite defaults: resampling in linear
light is **on**, the linear-light *metric* is **off**. Resampled normal maps are
renormalised before any angle is measured.

Chain validation flags truncated chains, wrong level dimensions, constant or
black levels, NaN and Inf, and alpha-coverage drift.

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
