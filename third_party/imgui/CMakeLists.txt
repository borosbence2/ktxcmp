# Build glue for Dear ImGui (docking branch). Upstream ships no CMakeLists;
# cmake/Dependencies.cmake fetches the sources and this defines the target.
#
# imgui_demo.cpp is compiled but never referenced, so the linker drops it. It
# stays in the list because it is the widget reference while building panels.

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")

target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")

target_compile_features(imgui PUBLIC cxx_std_20)

target_link_libraries(imgui PUBLIC
    SDL3::SDL3-static
    SDL3::Headers
    OpenGL::GL)

if(APPLE)
    target_compile_definitions(imgui PUBLIC GL_SILENCE_DEPRECATION)
endif()

add_library(ktxcmp::imgui ALIAS imgui)
