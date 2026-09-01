# ktxcmp

A desktop tool for inspecting KTX2 textures and comparing them against reference
images, with emphasis on mipmap chain analysis.

Read [CLAUDE.md](CLAUDE.md) for architecture, invariants and metric definitions,
and [PLAN.md](PLAN.md) for the milestone order and the UI specification.

## Status

All eight milestones are complete and the UI has been driven through end to end.

Opens `.ktx2` and `.ktx`, decodes ASTC, BC5, BC7 and uncompressed to RGBA32F on a
worker pool, and compares against a PNG reference three ways: mip 0 against the
reference with no resampling, the reference downsampled to each level (or an
explicit chain of PNGs matched by dimension), and the chain against itself.

BC5 is read as a normal map by default and the metrics change wholesale to
angular error; a raw RG override puts the colour metrics back. Reports PSNR-RGB,
RMSE, SSIM and max error with its coordinate, alpha always separate, an
error-by-level plot, a level table, warning badges on the mip strip, and CSV
export.

Five view modes: A, B, Diff with a gain selector, Split with a wipe, Onion with
a blend. Pan with the middle button or space-drag, zoom with the wheel, isolate
channels with `1`-`4` (`0` for RGB), `F` to fit, `Ctrl+1` for 1:1, `[` and `]` to
step levels. The status bar reads out the texel under the cursor, in directions
and degrees when the texture is a normal map.

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
