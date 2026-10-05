// msgold - gold price viewer with the Parabolic SAR indicator
// SDL3 + Dear ImGui (SDL_Renderer backend), C++17.
//
// Layout of the sources:
//   gold.h/.cpp  - domain: candles, Yahoo download, CSV cache, resampling, SAR
//   ui.h/.cpp    - app state, window shell, side panel, download handling
//   chart.cpp    - the price + SAR chart
//   main.cpp     - config, SDL init and the event/render loop
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "ui.h"

// An optional config.ini in the working directory:
//   data_root = ~/Documents      folder for the price caches (msgold.csv and
//                                msgold_eurusd.csv)
//   symbol = GC=F                Yahoo Finance ticker to chart
//   currency = USD               initial currency, USD or EUR
//   refresh_minutes = 15         automatic re-download, 0 disables
// Lines starting with # or ; are comments; ~/ expands to $HOME (falling back
// to %USERPROFILE% on Windows).
static void loadConfig(App& a) {
    std::ifstream f("config.ini");
    std::string line;
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t");
        size_t e = s.find_last_not_of(" \t");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (val.empty()) continue;
        if (key == "data_root") {
            if (val[0] == '~' && (val.size() == 1 || val[1] == '/' || val[1] == '\\')) {
                const char* home = getenv("HOME");
                if (!home) home = getenv("USERPROFILE");
                if (home) val = home + val.substr(1);
            }
            if (val.back() != '/' && val.back() != '\\') val += '/';
            a.cachePath = val + "msgold.csv";
            a.fxCachePath = val + "msgold_eurusd.csv";
        } else if (key == "symbol") {
            a.symbol = val;
        } else if (key == "currency") {
            a.currency = (val == "EUR" || val == "eur") ? 1 : 0;
        } else if (key == "refresh_minutes") {
            a.refreshMinutes = std::max(0, atoi(val.c_str()));
        }
    }
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("msgold - Oro con Parabolic SAR", 1400, 820,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
    if (!win || !ren) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);  // best effort; frame pacing does not rely on it

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed fullscreen layout, nothing to persist
    applyStyle();
    loadFonts();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    App a;
    loadConfig(a);
    a.daily = loadCsv(a.cachePath);
    a.fx = loadCsv(a.fxCachePath);
    if (!a.daily.empty())
        setStatus(a, "Caché cargada (" + a.cachePath + "), último cierre " +
                         fmtDateShort(a.daily.back().date) + ". Actualizando...");

    // debug hooks for headless screenshots:
    //   MSGOLD_SHOT=out.png  MSGOLD_PERIOD=0..5  MSGOLD_MOUSE=x,y
    //   MSGOLD_EUR=1  MSGOLD_ZOOM=YYYY-MM-DD,YYYY-MM-DD  MSGOLD_WHEEL=steps (at MOUSE)
    const char* shotPath = SDL_getenv("MSGOLD_SHOT");
    if (const char* p = SDL_getenv("MSGOLD_PERIOD")) a.period = std::clamp(atoi(p), 0, kNumPeriods - 1);
    if (const char* e = SDL_getenv("MSGOLD_EUR")) a.currency = atoi(e) ? 1 : 0;
    if (const char* z = SDL_getenv("MSGOLD_ZOOM")) {
        std::string zs = z;
        size_t comma = zs.find(',');
        a.zoomFrom = zs.substr(0, comma);
        if (comma != std::string::npos) a.zoomTo = zs.substr(comma + 1);
    }
    float mouseX = -1, mouseY = -1, wheel = 0;
    if (const char* w = SDL_getenv("MSGOLD_WHEEL")) wheel = (float)atof(w);
    if (const char* m = SDL_getenv("MSGOLD_MOUSE")) sscanf(m, "%f,%f", &mouseX, &mouseY);
    // a screenshot only needs a download when there is no cache yet
    if (!shotPath || a.daily.empty() || a.fx.empty()) startFetch(a);
    int frame = 0;

    bool running = true, hadEvents = false;
    Uint64 lastFrame = 0;
    while (running) {
        // Event-driven loop: sleep until input arrives instead of redrawing at
        // full speed. The timeout also picks up a finished background download
        // and drives the automatic refresh. After a batch of events one more
        // frame is drawn straight away, since ImGui widgets (and the chart's
        // zoom/pan) apply what they saw in one frame on the next one.
        if (!shotPath && !hadEvents) SDL_WaitEventTimeout(nullptr, 250);
        hadEvents = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            hadEvents = true;
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }
        Uint64 now = SDL_GetTicks();
        if (!shotPath && now - lastFrame < 16) SDL_Delay((Uint32)(16 - (now - lastFrame)));
        lastFrame = SDL_GetTicks();

        pollFetch(a);
        if (!shotPath && a.refreshMinutes > 0 && !fetching(a) &&
            SDL_GetTicks() - a.lastFetchAt > (Uint64)a.refreshMinutes * 60000)
            startFetch(a);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        // after the backend, which would otherwise overwrite it with the real mouse
        if (shotPath && mouseX >= 0) io.AddMousePosEvent(mouseX, mouseY);
        if (shotPath && wheel != 0 && !fetching(a) && frame == 1) io.AddMouseWheelEvent(0, wheel);
        ImGui::NewFrame();
        drawUI(a);
        ImGui::Render();

        SDL_SetRenderDrawColor(ren, (Uint8)(C_BG.x * 255), (Uint8)(C_BG.y * 255),
                               (Uint8)(C_BG.z * 255), 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);

        // screenshot once data is there (or the download failed)
        if (shotPath && !fetching(a) && ++frame == 5) {
            if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
                SDL_SavePNG(s, shotPath);
                SDL_DestroySurface(s);
            }
            running = false;
        }
        SDL_RenderPresent(ren);
    }

    if (fetching(a)) a.fetch.wait();  // let curl finish (bounded by its timeout)
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
