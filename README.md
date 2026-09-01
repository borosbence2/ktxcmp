# ktxcmp

A desktop tool for inspecting KTX2 textures and comparing them against reference
images, with emphasis on mipmap chain analysis.

Read [CLAUDE.md](CLAUDE.md) for architecture, invariants and metric definitions,
and [PLAN.md](PLAN.md) for the milestone order and the UI specification.

## Status

**M7 — reference chains and export.** All three compare modes, BC5 normal-map
interpretation, and explicit reference chains.

Mode 1 is KTX mip 0 against a PNG reference with no resampling. Mode 2
downsamples the reference to each level, or uses a supplied chain: point
`File > Open reference chain` at a folder of PNGs and they are matched to the
levels by dimension. Matching is by size alone - filenames are never parsed,
because a name like `_mip3` is a convention rather than a fact. A level with no
matching image is reported and left unmeasured, never filled in from somewhere
else. A supplied chain makes the filter irrelevant, which is the reason to
supply one. Mode 3 halves level N-1 and compares it to level N, needing no
reference.

BC5 is read as a normal map by default: z is reconstructed, both sides are
remapped the same way, and the metrics change wholesale to angular error in
degrees. The format banner offers a raw RG override that puts the colour
metrics back.

CSV export carries mode, filter, both linear-light flags, normal-map mode,
whether the chain was supplied or generated, and both file paths, before any
numbers - the per-level figures are not interpretable without them. Column names
follow the metric family.

Five resampling filters. Two linear-light toggles with deliberately opposite
defaults: resampling in linear light is **on**, the linear-light *metric* is
**off**. Resampled normal maps are renormalised before any angle is measured.

Chain validation flags truncated chains, wrong level dimensions, constant or
black levels, NaN and Inf, alpha-coverage drift, and missing chain references.

## Build

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The binary lands in `build/bin/`. The first configure clones SDL3, Dear ImGui,
astc-encoder and KTX-Software into `build/_deps` and the first build compiles all
of them, which takes several minutes; later builds are incremental.

- **Windows**: MSVC x64, static CRT (`/MT`), so the executable is portable with
  no runtime to ship beside it. With the default Visual Studio generator, add
  `--config RelWithDebInfo` to the build command. A developer command prompt plus
  `-G Ninja` is faster. `-DKTXCMP_CONSOLE=OFF` drops the console window, which is
  what the release build does; the console carries `SDL_Log` output and the
  frame-time summary and is worth having while developing.
- **macOS**: add `-DCMAKE_OSX_ARCHITECTURES=arm64`. The app is built as a bundle
  so Finder can associate the document types `packaging/Info.plist.in` declares
  (`.ktx2`, `.ktx`, `.png`); Open With and a Finder drop both arrive as command
  line arguments, which the app already accepts.

`CMakePresets.json` wraps the above as the `default`, `debug` and `macos-arm64`
presets.

## Releasing

Push a `v*` tag. `.github/workflows/release.yml` builds both platforms, runs the
tests, and attaches a zip per platform to a GitHub Release.

macOS signing and notarization need an Apple Developer account. Set these
repository secrets to enable them:

| Secret | What it is |
|---|---|
| `MACOS_CERT_P12` | Developer ID Application certificate, base64 of the .p12 |
| `MACOS_CERT_PASSWORD` | password for that .p12 |
| `MACOS_NOTARY_APPLE_ID` | Apple ID for notarytool |
| `MACOS_NOTARY_PASSWORD` | app-specific password for that Apple ID |
| `MACOS_NOTARY_TEAM_ID` | developer team id |

Without them the release still publishes, unsigned, and the workflow says so in
a warning rather than failing. An unsigned app needs a right-click Open on first
launch.

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
