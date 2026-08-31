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
    src/ui/Theme.cpp
    src/ui/Ui.cpp
    src/ui/Panels.cpp)

target_include_directories(ktxcmp PRIVATE src)

target_link_libraries(ktxcmp PRIVATE
    ktxcmp::imgui
    SDL3::SDL3-static
    SDL3::Headers
    OpenGL::GL)

# M1+ link these; fetched and built now so a dependency problem surfaces at M0
# rather than mid-milestone.
if(TARGET ktx)
    target_link_libraries(ktxcmp PRIVATE ktx)
    target_compile_definitions(ktxcmp PRIVATE KTXCMP_HAVE_LIBKTX=1)
endif()
if(TARGET ${KTXCMP_ASTCDEC_TARGET})
    target_link_libraries(ktxcmp PRIVATE ${KTXCMP_ASTCDEC_TARGET})
    target_compile_definitions(ktxcmp PRIVATE KTXCMP_HAVE_ASTCDEC=1)
endif()

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
