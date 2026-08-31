# ktxcmp — build plan

Read `CLAUDE.md` first for architecture, invariants, and metric definitions. This file is the
sequence of work and the UI specification.

Work one milestone at a time. Each ends with something runnable. Do not begin a milestone until
the previous one builds and runs on at least one target.

---

## M0 — Skeleton

- CMake project, C++20, dependency fetch (SDL3, ImGui, bcdec, astc-encoder, stb_image, libktx)
- SDL3 window, OpenGL 3.3 context, ImGui docking initialised
- Empty docked panels matching the final layout: left rail, viewport, right rail, bottom strip,
  status bar
- GitHub Actions matrix: `windows-latest`, `macos-14`. Build only, artifact upload.

Done when: window opens on both targets, panels dock and persist via `imgui.ini`.

---

## M1 — Container layer

- libktx wrapper: open `.ktx2`, read header and DFD
- `VkFormat` -> `FormatId { family, blockW, blockH, bytesPerBlock, channels, transferFn, signed }`
  for the supported subset only (ASTC LDR/sRGB block sizes, BC5 UNORM/SNORM, BC7 UNORM/sRGB,
  uncompressed RGBA8/RGBA16)
- Read premultiplied-alpha and sRGB flags from the DFD
- Expose level/layer/face counts and per-level byte ranges
- Assert level dimensions against `max(1, base >> level)` independently of libktx
- Reject unsupported formats with a clear message naming the format

No pixels yet. The metadata panel shows format, dimensions, level count, supercompression scheme,
DFD flags, byte size.

Done when: a real ASTC and a real BC7 `.ktx2` both report correct metadata.

---

## M2 — Decode layer + viewer

- `decode(FormatId, span<const uint8_t> blocks, int w, int h, int d) -> Result<Surface>`
- BC5 and BC7 via bcdec; handle SNORM
- ASTC via `astcenc_decompress`, profile chosen from the format id's sRGB flag
- Uncompressed passthrough
- Crop block padding to logical dimensions
- Viewport: upload `Surface` to a GL texture, pan (MMB / space-drag), zoom (wheel),
  nearest-neighbour above 1:1, box-filtered below, pixel grid above ~8x
- Channel isolation buttons R / G / B / A
- Status bar pixel inspector: coordinates and value under cursor

Done when: an ASTC file and a BC7 file both display correctly and values in the inspector are
plausible.

---

## M3 — Subresource navigation + async

- Worker pool for decode; UI never blocks
- `ImageCache`: LRU keyed by `{slot, level, layer, face}`, budgeted in bytes
- Mip strip along the bottom: one thumbnail per level with its dimensions
- Layer / face / depth-slice selectors
- Progressive population: small levels decode first so the strip fills quickly
- Explicit pending state in viewport, metrics panel, and strip — decide the visual once and use it
  everywhere

Done when: a 12-level 2048² ASTC file is navigable with no UI stall.

---

## M4 — Reference + mode 1

- PNG loader (8-bit and 16-bit), sRGB assumed, override exposed in the slot B panel
- Slot B in the left rail; drag-and-drop onto either slot, drop on the body fills the first empty
- Compare mode 1: KTX mip 0 vs reference, dimensions must match
- Metrics: PSNR-RGB, RMSE, SSIM, max error + coordinate, alpha reported separately
- Diff view mode with gain selector (1x / 4x / 8x / 16x)
- Pixel inspector shows A, B, and delta

Done when: PSNR of a known-good ASTC encode lands in a sane range and mismatched dimensions
produce a clear inline reason rather than a number.

---

## M5 — Mode 2 and mode 3

- Resampler: box, triangle, Kaiser, Lanczos3, Mitchell
- Linear-light toggle, on by default for colour
- Mode 2: reference downsampled per level, compared to each KTX level
- Mode 3: KTX level N-1 downsampled x2, compared to level N. No reference needed — slot B dims
- Per-level metrics table, exportable as CSV including filter and linear-light state
- Error-by-level plot in the right rail; clicking a point selects that level everywhere
- Warning badges on mip strip thumbnails where a level exceeds a configurable error threshold
- Chain validation checks from `CLAUDE.md` surfaced as badges

Done when: a texture with deliberately gamma-space mips shows a clear error spike, and toggling
linear-light off collapses it.

---

## M6 — BC5 normal mode

- Auto-detect from format id: BC5 selects normal-map interpretation by default
- Reconstruct `z = sqrt(max(0, 1 - x² - y²))` after remapping [0,1] -> [-1,1]
- Remap the reference the same way
- Metrics swap wholesale: mean / median / p95 / max angular error, `|n|` deviation. Hero number,
  supporting rows, and plot axis label all change together
- Reference downsampling renormalises
- Format banner in the right rail stating the detection, with a manual override to raw RG mode
- Pixel inspector becomes format-aware: floats and an angular delta, not 8-bit ints

Done when: a BC5 normal map reports sub-degree mean angular error against its source, and the raw
RG override produces PSNR instead.

---

## M7 — Reference chains + export

- Accept a set of PNGs (multi-select or a folder) matched to levels by dimension
- Report unmatched levels rather than silently falling back to generated references
- CSV export of the full per-level table, including mode, filter, linear-light flag, and both file
  paths in a header comment

---

## M8 — Packaging

- Windows: MSVC x64, `/MT`, portable `.exe`, optional Inno Setup installer
- macOS: `.app` bundle, `Info.plist` declaring `.ktx2` and `.png` document types for Finder
  drag-drop and Open With, `CMAKE_OSX_ARCHITECTURES=arm64`
- Codesign and notarize the macOS build. Budget real time for this; it is a first-time rabbit hole
- CI publishes both artifacts per tag

---

## UI specification

Single window, ImGui docking. Layout holds at 680px wide, so it holds at any real window size —
side rails stay fixed-width, the viewport takes the slack.

**Left rail (~136px fixed)**
Slot A card: filename, format, dimensions, level/layer counts, supercompression, DFD flags.
Slot B card: filename, format, dimensions, chain source (generated vs explicit).
Drop target below.
Slot B greys to grayscale — not reduced opacity — when mode 3 is selected, and its header reads
"not used". Text stays legible; only the state changes.

**Centre**
View mode row: A / B / Diff / Split / Onion.
Viewport with overlay labels: `{mode} · mip {n} · {w}²` top-left, zoom bottom-right.
Control row: channel buttons always present; one contextual control on the right, changing with
view mode — gain for Diff, wipe position for Split, blend for Onion, nothing for A/B. There is no
second permanent control row.

**Right rail (~158px fixed)**
Format banner (accent tint, not warning — successful auto-detection is normal operation).
Compare mode as a dropdown, not tabs: modes 2 and 3 need filter controls that mode 1 does not, and
a dropdown makes the dependent controls appearing feel intentional.
Filter controls (hidden for mode 1).
Hero metric card, then four supporting rows.
Error-by-level plot. Make it tall enough to click points on.

The full per-level table lives behind a toggle or in the CSV export, not permanently on screen.
You scan the plot; you do not read fifteen rows of numbers.

**Bottom**
Mip strip: thumbnail per level with dimensions and a warning badge where validation flags a
problem. Warnings live here, not in a separate problems panel — the strip is already the
navigation, so one glance shows which level to look at.

**Status bar**
Pixel inspector: coordinates, A value, B value, delta. Format-aware.

**Keyboard**
`1`–`4` channel isolate, `0` all RGB, `space` A/B toggle, `[` / `]` level step, `F` fit,
`Ctrl+1` reset to 1:1.

---

## Test corpus

Generate with `toktx` and `basisu` from KTX-Software:
- One source image encoded to each supported format
- Deliberately broken files: truncated chain, gamma-space mips, mismatched DFD, NaN payload
- Non-power-of-two dimensions
- Non-multiple-of-block-size dimensions (exercises the crop path)
- A BC5 normal map with a known ground-truth source
- 8-bit and 16-bit PNG references

Golden-hash decoded output per format once verified by eye. Also pull the Khronos KTX-Software
conformance files, which include intentionally invalid cases.

Without this corpus a wrong ASTC profile ships and goes unnoticed for months.
