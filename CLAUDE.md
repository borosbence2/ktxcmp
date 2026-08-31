# ktxcmp

A desktop tool for inspecting KTX textures and comparing them against reference images,
with emphasis on mipmap chain analysis. Similar in spirit to PVRTexToolGUI, narrower in scope.

## Scope

**In scope**
- KTX2 container reading (`.ktx2`)
- KTX1 container reading (`.ktx`), read-only, same supported format subset
- Formats: ASTC (LDR + sRGB, all 2D block sizes), BC5 (UNORM + SNORM), BC7, uncompressed RGBA8/RGBA16
- Supercompression: Zstd, ZLIB, UASTC transcode
- PNG as reference image (8-bit and 16-bit)
- RGB error metrics against the reference
- Mipmap chain navigation, comparison, and validation

**Out of scope — do not add without being asked**
- Encoding or compression of any kind
- Format conversion, file writing
- BC1–BC4, BC6H, ETC/EAC, PVRTC, Basis ETC1S
- Batch CLI, network features, telemetry

Deferred, plausible later: DDS reading, BC6H + HDR pipeline.

## Stack

- C++23 for the app target (`std::expected`), C++20 for dependencies. CMake 3.25+
- SDL3 (window + input)
- Dear ImGui (docking branch) with `imgui_impl_sdl3` + `imgui_impl_opengl3`
- OpenGL 3.3 core (deprecated on macOS but functional; GPU work is only "upload RGBA and draw a quad")

Targets: Windows x64 (MSVC), macOS arm64. No Linux target required, but do not write
gratuitously non-portable code.

## Dependencies

| Purpose | Library | Integration |
|---|---|---|
| KTX2 + KTX1 container, DFD, supercompression, UASTC transcode | KTX-Software (libktx) | submodule or vcpkg; disable tools/tests/docs targets |
| BC5 + BC7 block decode | `bcdec.h` | vendored single header |
| ASTC block decode | ARM astc-encoder (`astcenc_decompress`) | submodule; decoder-only build if possible |
| PNG load | `stb_image.h` (or lodepng if 16-bit handling proves fiddly) | vendored |
| GUI | Dear ImGui | submodule |

Do not add dependencies beyond these without flagging it first. In particular: no Qt, no Boost,
no image library that pulls in a codec zoo.

## Architecture

```
UI (ImGui panels)
  -> ViewState        selection: {slot, level, layer, face, channel mask, view mode}
  -> CompareEngine    metrics, diff image generation, chain validation
  -> ImageCache       LRU of decoded surfaces, keyed by subresource
  -> DecodeLayer      block format -> Surface
  -> ContainerLayer   libktx wrapper -> format id, dims, counts, raw block bytes
```

Layers depend downward only. The UI never touches libktx or a decoder directly.

## Core invariant: the Surface type

```cpp
enum class TransferFn { Linear, Srgb };

struct Surface {
    int w = 0, h = 0, d = 1;
    std::vector<float> rgba;   // w*h*d*4, NOT clamped to [0,1]
    TransferFn tf = TransferFn::Srgb;
    bool premultiplied = false;
};
```

Rules that must not be violated:

1. **RGBA32F is the only intermediate.** Never decode to RGBA8 as a staging step. It silently
   destroys any future HDR format and loses precision on 16-bit PNG input.
2. **Never clamp to [0,1]** anywhere in the decode or metric path.
3. **`tf` is recorded, not applied.** Decoders tag the surface; conversion happens explicitly and
   only where a specific operation requires it.
4. **CompareEngine refuses mismatched `tf`.** Return an error rather than producing a number from
   two surfaces in different spaces. A wrong metric is worse than no metric.
5. **Crop to logical dimensions after block decode.** Block-padded edge texels must never reach a
   metric.

## Metric definitions

These are contractual. Do not "improve" them without changing this file too.

- **PSNR-RGB**: MSE pooled across R, G, B together (3·N samples), peak 255, computed on
  sRGB-encoded values. Not the average of three per-channel PSNRs — that is a different number.
- **RMSE**: 0–255 units.
- **SSIM**: on Rec.709 luma, 11x11 Gaussian sigma 1.5, standard C1/C2.
- **Max absolute error**: value plus pixel coordinate.
- Alpha metrics are computed and reported separately. Alpha never enters an RGB figure.
- A linear-light PSNR toggle exists but is off by default and must be labelled in any output.

For BC5 in normal-map mode, PSNR is not reported. Instead:
- Angular error in degrees: `acos(clamp(dot(n_ref, n_test), -1, 1))` — mean, median, p95, max.
- Deviation of `|n|` from 1.0, pre-normalisation.
- Reference downsampling must renormalise after filtering.

## Compare modes

1. **Encode fidelity** — KTX mip 0 vs reference, identical dimensions, no resampling. The only
   unconfounded measurement. Default when a reference is loaded.
2. **Chain vs reference** — reference downsampled to each mip's dimensions, compared per level.
   Result depends on the filter choice, so the filter and the linear-light flag must appear in
   every table and every export.
3. **Self-consistency** — KTX mip N-1 downsampled x2 vs KTX mip N. Needs no reference.

Explicit reference chains (a set of PNGs matched to levels by dimension) remove the filter
ambiguity in mode 2. Support this.

## Chain validation checks

- Expected level count `floor(log2(max(w,h))) + 1`; flag truncation
- Dimension chain `max(1, base >> level)`
- Alpha-test coverage per level at a configurable threshold; report drift
- Mean luminance per level
- Constant / black / NaN / Inf level detection

## Known traps

1. ASTC profile: pick `ASTCENC_PRF_LDR_SRGB` vs `ASTCENC_PRF_LDR` from the KTX2 format id's sRGB
   flag. Guessing shifts every value and every metric derived from it.
2. BC5 UNORM vs SNORM: different unpack, and the normal-map remap differs accordingly.
3. Block-padded dimensions not cropped: poisons edge-block metrics.
4. PNG has no reliable colour-space signal. Assume sRGB, expose an override, never infer from
   `gAMA`.
5. 16-bit PNG truncated to 8-bit by a careless loader path.
6. Premultiplied alpha (from the DFD) not surfaced: produces invalid comparisons silently.
7. Normal-map reference downsampling without renormalisation.
8. macOS notarization left until the end.
9. KTX1 has no DFD. Format identity comes from `glInternalFormat`, not `VkFormat`, and the
   sRGB flag comes with it; there is no DFD premultiplied-alpha flag to read, so that state
   is unknown rather than false. `FormatId` must be reachable from both, and anything the
   UI reports as "from the DFD" must say so only when a DFD actually exists.
10. astc-encoder returns **NaN** for an undecodable ASTC block, by design, not a colour. Since
    nothing in the decode path clamps, those NaNs reach the metric path and would silently
    poison a mean. Every metric must exclude or report non-finite texels, and the viewer must
    render them as a distinct colour rather than whatever the hardware makes of a NaN. BC and
    uncompressed formats cannot produce this; ASTC can, from any corrupt payload.
11. libktx's full build embeds its own copy of astc-encoder. Linking it alongside our
    decoder-only build gives the linker two definitions of every internal astcenc symbol, and
    it resolves them silently rather than erroring. Link `ktx_read` everywhere except the
    fixture writer.

## Conventions

- C++23 on the `ktxcmp` target only; dependencies stay at C++20. `std::filesystem`,
  `std::span`, designated initialisers are fine. MSVC needs `/std:c++latest` for
  `std::expected`, which is why the app target asks for `cxx_std_23` rather than the
  project defaulting to it: there is no reason to push basisu or SDL through a preview mode.
- Error handling: `Result<T>` in `core/Error.hpp`, an alias for `std::expected<T, Error>`.
  Return it from the non-UI layers. No exceptions across layer boundaries. libktx returns
  error codes — wrap them, do not let `ktx_error_code_e` leak upward. The alias is the seam:
  if a target's standard library lacks `<expected>`, only that line changes.
- No singletons. `AppState` is constructed in `main` and passed by reference.
- Decode work runs on a worker pool; the UI thread never blocks on a decode. Panels render a
  placeholder for a pending subresource.
- Naming: `PascalCase` types, `camelCase` functions and locals, `m_` prefix only where a class
  genuinely has invariants to protect.
- Tests: prefer a small hand-rolled harness over pulling in a framework. Golden-hash decoded
  output per format.

## Build

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

macOS: `-DCMAKE_OSX_ARCHITECTURES=arm64`.
Windows: MSVC x64, static CRT (`/MT`).

Always build and run before reporting a milestone complete. The tool must open a real KTX2 file,
not just compile.

## Working agreement

- Follow `PLAN.md` milestone order. Do not skip ahead to compare features before the decode layer
  is verified against real files.
- When a design decision is genuinely ambiguous, ask rather than picking silently.
- If a trap above turns out to be wrong or incomplete, update this file.
