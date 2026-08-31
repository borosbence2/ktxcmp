// ktxcmp entry point: window, OpenGL 3.3 context, ImGui lifetime, frame loop.
// Everything above this is UI; see CLAUDE.md for the layering below it.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "app/AppState.hpp"
#include "ui/Ui.hpp"

namespace {

constexpr int kDefaultWidth  = 1280;
constexpr int kDefaultHeight = 820;

// The layout is claimed to hold at 680px wide, so the window may not go below it.
constexpr int kMinWidth  = 680;
constexpr int kMinHeight = 460;

constexpr const char* kGlslVersion = "#version 330";

// Layout persists per user rather than per working directory.
const char* iniPath() {
    static std::string path = [] {
        char* pref = SDL_GetPrefPath("ktxcmp", "ktxcmp");
        if (!pref)
            return std::string("imgui.ini");
        std::string p = std::string(pref) + "imgui.ini";
        SDL_free(pref);
        return p;
    }();
    return path.c_str();
}

void reportFatal(const char* what) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", what, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ktxcmp", what, nullptr);
}

// SDL hands back UTF-8. Constructing a path from char* would reinterpret it in
// the local code page on Windows, which mangles any non-ASCII directory name.
std::filesystem::path fromUtf8(const char* s) {
    return std::filesystem::path(reinterpret_cast<const char8_t*>(s));
}

void SDLCALL onFilesChosen(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* app = static_cast<ktxcmp::AppState*>(userdata);
    if (app == nullptr || filelist == nullptr || *filelist == nullptr)
        return;  // cancelled, or the dialog failed
    for (const char* const* it = filelist; *it != nullptr; ++it)
        app->enqueueOpen(fromUtf8(*it));
}

void showOpenDialog(SDL_Window* window, ktxcmp::AppState& app) {
    static const SDL_DialogFileFilter filters[] = {
        {"KTX textures", "ktx2;ktx"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(onFilesChosen, &app, window, filters, SDL_arraysize(filters),
                           nullptr, false);
}

float displayScale() {
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    return scale > 0.0f ? scale : 1.0f;
}

}  // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        reportFatal("SDL_Init failed");
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    const float scale = displayScale();

    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("ktxcmp",
                                          static_cast<int>(kDefaultWidth * scale),
                                          static_cast<int>(kDefaultHeight * scale), flags);
    if (!window) {
        reportFatal("SDL_CreateWindow failed");
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, static_cast<int>(kMinWidth * scale),
                             static_cast<int>(kMinHeight * scale));

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        reportFatal("OpenGL 3.3 core context could not be created");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    SDL_Log("GL %s | %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER),
            glGetString(GL_VENDOR));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = iniPath();

    ktxcmp::ui::applyTheme(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    if (!ImGui_ImplSDL3_InitForOpenGL(window, gl) || !ImGui_ImplOpenGL3_Init(kGlslVersion)) {
        reportFatal("ImGui backend initialisation failed");
        SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    ktxcmp::AppState app;
    app.uiScale = scale;
    ktxcmp::ui::UiState uiState;

    // Files named on the command line. This is what "Open With" and dropping a
    // file on the executable deliver, which M8 wants for Finder too. It is not a
    // batch CLI: there is no processing here, only a file to show.
    for (int i = 1; i < argc; ++i)
        app.enqueueOpen(fromUtf8(argv[i]));

    SDL_ShowWindow(window);

    // The M3 claim is "navigable with no UI stall", so measure it rather than
    // assert it. Reported once on exit; a decode landing on the UI thread would
    // show up here immediately.
    double worstFrameMs = 0.0;
    double totalFrameMs = 0.0;
    int frameCount = 0;
    int framesOver16 = 0;
    int framesOver33 = 0;
    std::uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double counterPeriod = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());

    while (app.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                app.running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
                app.running = false;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr)
                app.enqueueOpen(fromUtf8(event.drop.data));
        }

        if (app.openDialogRequested) {
            app.openDialogRequested = false;
            showOpenDialog(window, app);
        }
        app.processPendingOpens();

        // Only asks; the workers deliver into the cache on their own schedule.
        const int levelBefore = app.view.level;
        app.requestVisible();
        if (app.view.level != levelBefore)
            uiState.fitRequested = true;

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ktxcmp::ui::drawFrame(app, uiState);

        ImGui::Render();

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        const ImVec4 clear = ktxcmp::ui::viewportBgColor();
        glClearColor(clear.x, clear.y, clear.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);

        const std::uint64_t now = SDL_GetPerformanceCounter();
        const double frameMs = static_cast<double>(now - lastCounter) * counterPeriod;
        lastCounter = now;
        // The first frames include window creation and the initial file load.
        if (++frameCount > 5) {
            totalFrameMs += frameMs;
            if (frameMs > worstFrameMs)
                worstFrameMs = frameMs;
            if (frameMs > 16.7)
                ++framesOver16;
            if (frameMs > 33.3)
                ++framesOver33;
        }
    }

    if (frameCount > 5)
        SDL_Log("frames %d | mean %.2f ms | worst %.2f ms | over 16.7ms: %d | over 33ms: %d "
                "| cache %zu MB in %zu entries",
                frameCount, totalFrameMs / static_cast<double>(frameCount - 5), worstFrameMs,
                framesOver16, framesOver33, app.cache.bytesUsed() / (1024u * 1024u),
                app.cache.entryCount());

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
