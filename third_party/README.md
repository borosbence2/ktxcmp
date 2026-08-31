# Vendored sources

Single-header libraries, committed rather than fetched so the decode path has no
configure-time network dependency.

| File | Version | Upstream |
|---|---|---|
| `bcdec/bcdec.h` | v0.97 | https://github.com/iOrange/bcdec |
| `stb/stb_image.h` | v2.30 | https://github.com/nothings/stb |

`imgui/CMakeLists.txt` is not vendored source: it is build glue for the Dear ImGui
checkout that `cmake/Dependencies.cmake` fetches, because upstream ships no CMake
build of its own.

Fetched dependencies and their pinned tags live in `cmake/Dependencies.cmake`.
