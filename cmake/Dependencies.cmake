# Dependency acquisition. CLAUDE.md fixes this list; do not extend it without
# flagging the addition first.
#
#   SDL3           window + input                    fetched, static
#   Dear ImGui     GUI (docking branch)              fetched, built by third_party/imgui
#   astc-encoder   ASTC block decode                 fetched, decompressor only
#   libktx         KTX2 + KTX1 container / DFD       fetched, static
#   bcdec.h        BC5 + BC7 block decode            vendored, third_party/bcdec
#   stb_image.h    PNG load                          vendored, third_party/stb
#
# No GL loader is fetched on purpose. ImGui's OpenGL 3 backend carries its own,
# and our code only calls entry points that opengl32.lib exports directly
# (GL 1.1: textures, viewport, clear). Newer enum values passed to those
# functions are fine; it is new *functions* that would need a loader.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------- SDL3 -----

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.14
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

block(SCOPE_FOR VARIABLES)
    set(SDL_SHARED        OFF)
    set(SDL_STATIC        ON)
    set(SDL_TEST_LIBRARY  OFF)
    set(SDL_TESTS         OFF)
    set(SDL_EXAMPLES      OFF)
    set(SDL_INSTALL       OFF)
    set(SDL_DISABLE_INSTALL ON)
    FetchContent_MakeAvailable(SDL3)
endblock()

# ------------------------------------------------------------ Dear ImGui ----

# ImGui ships no CMakeLists, so MakeAvailable only populates the source tree;
# third_party/imgui/CMakeLists.txt defines the target.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b-docking
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(imgui)

# ---------------------------------------------------------------- libktx ---

FetchContent_Declare(ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG        v4.4.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    GIT_SUBMODULES "")   # the only submodule is the CTS test corpus

block(SCOPE_FOR VARIABLES)
    set(KTX_FEATURE_TESTS          OFF)
    set(KTX_FEATURE_TOOLS          OFF)
    set(KTX_FEATURE_TOOLS_CTS      OFF)
    set(KTX_FEATURE_DOC            OFF)
    set(KTX_FEATURE_JNI            OFF)
    set(KTX_FEATURE_PY             OFF)
    set(KTX_FEATURE_LOADTEST_APPS  OFF)
    set(KTX_FEATURE_STATIC_LIBRARY ON)
    set(KTX_FEATURE_KTX1           ON)   # .ktx read support, added 2026-08-31
    set(KTX_FEATURE_KTX2           ON)
    set(KTX_FEATURE_GL_UPLOAD      OFF)  # we decode on the CPU, not upload
    set(KTX_FEATURE_VK_UPLOAD      OFF)
    set(KTX_FEATURE_ETC_UNPACK     OFF)  # scope: no ETC/EAC
    set(BUILD_TESTING              OFF)
    set(BUILD_SHARED_LIBS          OFF)
    FetchContent_MakeAvailable(ktx)
endblock()

# --------------------------------------------------------- astc-encoder ----

# The version is derived from libktx, not chosen. libktx vendors its own copy of
# astc-encoder, and on Apple it merges that copy into its own static archive -
# for ktx_read as well as ktx. A Mach-O link then resolves astcenc symbols from
# whichever archive it sees first, silently, with no diagnostic. If the two
# copies are different versions, the winning code is called through the losing
# version's headers: structs disagree, and ASTC decodes to a constant instead of
# an image. That is not hypothetical; it is what macOS CI caught.
#
# Pinning to whatever libktx carries makes the two ABI-identical, so it no longer
# matters which one wins. If libktx bumps its copy, this follows automatically.
FetchContent_GetProperties(ktx SOURCE_DIR KTXCMP_KTX_SOURCE_DIR)
set(KTXCMP_KTX_ASTCENC_CMAKE "${KTXCMP_KTX_SOURCE_DIR}/external/astc-encoder/CMakeLists.txt")
if(NOT EXISTS "${KTXCMP_KTX_ASTCENC_CMAKE}")
    message(FATAL_ERROR
        "Cannot find libktx's vendored astc-encoder at ${KTXCMP_KTX_ASTCENC_CMAKE}. "
        "The version-matching guard in cmake/Dependencies.cmake needs updating.")
endif()
file(READ "${KTXCMP_KTX_ASTCENC_CMAKE}" KTXCMP_KTX_ASTCENC_TEXT)
string(REGEX MATCH "project\\(astcencoder VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)\\)"
       KTXCMP_ASTCENC_MATCH "${KTXCMP_KTX_ASTCENC_TEXT}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR
        "Could not read the astc-encoder version out of libktx. "
        "See the version-matching guard in cmake/Dependencies.cmake.")
endif()
set(KTXCMP_ASTCENC_VERSION "${CMAKE_MATCH_1}")
message(STATUS "astc-encoder ${KTXCMP_ASTCENC_VERSION} (matched to libktx's vendored copy)")

# One ISA variant only; the target name embeds it.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(KTXCMP_ASTCDEC_ISA neon)
else()
    set(KTXCMP_ASTCDEC_ISA sse4.1)
endif()
set(KTXCMP_ASTCDEC_TARGET "astcdec-${KTXCMP_ASTCDEC_ISA}-static")

FetchContent_Declare(astcenc
    GIT_REPOSITORY https://github.com/ARM-software/astc-encoder.git
    GIT_TAG        ${KTXCMP_ASTCENC_VERSION}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# Scoped so these never leak into libktx, which vendors its own copy of
# astc-encoder and picks its own ISA settings.
block(SCOPE_FOR VARIABLES)
    set(ASTCENC_DECOMPRESSOR    ON)
    set(ASTCENC_CLI             OFF)
    set(ASTCENC_SHAREDLIB       OFF)
    set(ASTCENC_UNITTEST        OFF)
    set(ASTCENC_WERROR          OFF)
    set(ASTCENC_UNIVERSAL_BUILD OFF)
    set(ASTCENC_ISA_NATIVE      OFF)
    set(ASTCENC_ISA_NONE        OFF)
    set(ASTCENC_ISA_SVE_256     OFF)
    set(ASTCENC_ISA_SVE_128     OFF)
    set(ASTCENC_ISA_AVX2        OFF)
    set(ASTCENC_ISA_SSE2        OFF)
    if(KTXCMP_ASTCDEC_ISA STREQUAL neon)
        set(ASTCENC_ISA_NEON  ON)
        set(ASTCENC_ISA_SSE41 OFF)
    else()
        set(ASTCENC_ISA_NEON  OFF)
        set(ASTCENC_ISA_SSE41 ON)
    endif()
    set(BUILD_TESTING OFF)
    FetchContent_MakeAvailable(astcenc)
endblock()

if(NOT TARGET ${KTXCMP_ASTCDEC_TARGET})
    message(FATAL_ERROR
        "astc-encoder did not produce the expected target '${KTXCMP_ASTCDEC_TARGET}'. "
        "Check the ISA selection in cmake/Dependencies.cmake.")
endif()

