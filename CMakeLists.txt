# ktxcmp - KTX2 inspector and mipmap chain comparison tool.
# See CLAUDE.md for architecture and invariants, PLAN.md for milestone order.

cmake_minimum_required(VERSION 3.25)

project(ktxcmp
    VERSION 0.1.0
    DESCRIPTION "KTX2 inspector and mipmap chain comparison tool"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Must be set before dependencies are added so it propagates into them.
# CLAUDE.md: Windows ships a portable exe, so static CRT.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(cmake/Dependencies.cmake)

add_subdirectory(third_party/imgui)

add_executable(ktxcmp
    src/main.cpp
    src/app/AppState.cpp
    src/core/Error.cpp
    src/container/FormatId.cpp
    src/container/KtxFile.cpp
    src/decode/Decoder.cpp
    src/ui/ImageTexture.cpp
    src/ui/Theme.cpp
    src/ui/Ui.cpp
    src/ui/Panels.cpp)

target_include_directories(ktxcmp PRIVATE src)

# C++23 for std::expected, on our target only. The dependencies stay at the
# global C++20 default: /std:c++latest is a preview mode and there is no reason
# to push basisu or SDL through it.
target_compile_features(ktxcmp PRIVATE cxx_std_23)

target_link_libraries(ktxcmp PRIVATE
    ktxcmp::imgui
    SDL3::SDL3-static
    SDL3::Headers
    OpenGL::GL)

# ktx_read, not ktx: the read-only libktx variant matches our scope and, more
# importantly, leaves out libktx's bundled ASTC *encoder*. Linking that
# alongside our decoder-only astc-encoder gives the linker two copies of the
# same internal symbols, and it silently resolves them from whichever it sees
# first - which on this machine meant an AVX2 build selected by libktx's own
# CPU probe, not the SSE4.1 one we asked for.
if(NOT TARGET ktx_read)
    message(FATAL_ERROR "libktx target 'ktx_read' was not created; see cmake/Dependencies.cmake")
endif()
target_link_libraries(ktxcmp PRIVATE ktx_read)

# The decode layer needs the ASTC decompressor from M2 on.
target_link_libraries(ktxcmp PRIVATE ${KTXCMP_ASTCDEC_TARGET})

target_include_directories(ktxcmp PRIVATE
    third_party/bcdec
    third_party/stb)

if(APPLE)
    # OpenGL is deprecated on macOS but functional; we only upload and blit.
    target_compile_definitions(ktxcmp PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    target_compile_options(ktxcmp PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(ktxcmp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# M8 flips this on for a windowed release build; the console is useful now.
# set_target_properties(ktxcmp PROPERTIES WIN32_EXECUTABLE TRUE)

# ---------------------------------------------------------------- tests -----
# Hand-rolled harnesses, no framework (CLAUDE.md). They link the layer under
# test directly, so they stay runnable without a window.

enable_testing()

# One writer, two readers. make_fixtures is the only binary that links the full
# libktx: its ASTC encoder is a second copy of astc-encoder, and linking it
# beside our decoder-only build duplicates every internal symbol.
set(KTXCMP_LAYER_SOURCES
    src/core/Error.cpp
    src/container/FormatId.cpp
    src/container/KtxFile.cpp
    src/decode/Decoder.cpp)

add_executable(make_fixtures
    tests/make_fixtures.cpp
    tests/CorpusSpec.cpp
    tests/CorpusWriter.cpp)
target_include_directories(make_fixtures PRIVATE src tests)
target_compile_features(make_fixtures PRIVATE cxx_std_23)
target_link_libraries(make_fixtures PRIVATE ktx)

add_executable(container_test
    tests/container_test.cpp
    tests/CorpusSpec.cpp
    ${KTXCMP_LAYER_SOURCES})
target_include_directories(container_test PRIVATE src tests third_party/bcdec)
target_compile_features(container_test PRIVATE cxx_std_23)
target_link_libraries(container_test PRIVATE ktx_read ${KTXCMP_ASTCDEC_TARGET})

add_executable(decode_test
    tests/decode_test.cpp
    tests/CorpusSpec.cpp
    ${KTXCMP_LAYER_SOURCES})
target_include_directories(decode_test PRIVATE src tests third_party/bcdec)
target_compile_features(decode_test PRIVATE cxx_std_23)
target_link_libraries(decode_test PRIVATE ktx_read ${KTXCMP_ASTCDEC_TARGET})

foreach(t make_fixtures container_test decode_test)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

set(KTXCMP_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")

add_test(NAME fixtures COMMAND make_fixtures "${KTXCMP_FIXTURE_DIR}")
set_tests_properties(fixtures PROPERTIES FIXTURES_SETUP corpus)

add_test(NAME container COMMAND container_test --corpus "${KTXCMP_FIXTURE_DIR}")
add_test(NAME decode COMMAND decode_test "${KTXCMP_FIXTURE_DIR}")
set_tests_properties(container decode PROPERTIES FIXTURES_REQUIRED corpus)
