/*  example_crop.cpp - the "crop around the aim point" technique, for 2D games or as a first quick test.

    The game already has its full frame in memory (here a 960x540 BGRX "screen" drawn by the example). For every
    player with a scope it cuts a square around that player's aim point, scales it up to 240x240 (4x zoom), draws a
    reticle and sends it. The aim points move by themselves; in a game they come from the lightguns.

    Build (the C library is in ../C):
        Linux / macOS :  c++ -std=c++17 -O2 -I../C -o example_crop example_crop.cpp
        Windows MinGW :  g++ -std=c++17 -O2 -I../C -o example_crop.exe example_crop.cpp -lws2_32
        Windows MSVC  :  cl /std:c++17 /O2 /EHsc /I..\C example_crop.cpp

    Run:  ./example_crop [--seconds 20] [--zoom 4] [--always]

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
*/
#define OPENFIRE_SCOPE_IMPLEMENTATION
#include "openfire_scope.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int ScreenW = 960, ScreenH = 540;

// B, G, R, X in memory: the layout of a Direct3D B8G8R8A8 back buffer.
inline std::uint32_t bgrx(int r, int g, int b) { return std::uint32_t(b) | std::uint32_t(g) << 8 | std::uint32_t(r) << 16; }

// The "game": a sky, some hills, a few targets moving over them.
void drawScreen(std::vector<std::uint32_t> &screen, double t)
{
    static double hill[ScreenW];
    for (int x = 0; x < ScreenW; ++x) hill[x] = 250 + 40 * std::sin(x * 0.012 + t * 0.2) + 15 * std::sin(x * 0.05);
    for (int y = 0; y < ScreenH; ++y)
        for (int x = 0; x < ScreenW; ++x) {
            const std::uint32_t c = y < hill[x] ? bgrx(100 + y / 6, 160 + y / 8, 235)
                                       : bgrx(50 + ((x / 8 + y / 8) & 1) * 12, 150 - (y - 250) / 4, 45);
            screen[std::size_t(y) * ScreenW + x] = c;
        }
    for (int i = 0; i < 6; ++i) {   // targets: rings, so that the zoom shows detail
        const int cx = int(480 + 380 * std::sin(t * 0.3 + i * 1.7)), cy = int(230 + 90 * std::cos(t * 0.5 + i));
        for (int y = cy - 14; y <= cy + 14; ++y)
            for (int x = cx - 14; x <= cx + 14; ++x) {
                if (x < 0 || y < 0 || x >= ScreenW || y >= ScreenH) continue;
                const int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (d2 <= 14 * 14) screen[std::size_t(y) * ScreenW + x] = (d2 / 30) & 1 ? bgrx(240, 240, 240) : bgrx(220, 30, 30);
            }
    }
}

// The scope view: a (240/zoom)-pixel square around the aim point, scaled to 240x240 with bilinear filtering,
// plus the reticle (the scope draws none). Outside the screen it is black.
void cropToScope(const std::vector<std::uint32_t> &screen, double aimX, double aimY, double zoom, std::uint32_t *out)
{
    const double step = 1.0 / zoom;
    for (int y = 0; y < openfire::Height; ++y)
        for (int x = 0; x < openfire::Width; ++x) {
            const double sx = aimX + (x - 119.5) * step, sy = aimY + (y - 119.5) * step;
            const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
            const double fx = sx - x0, fy = sy - y0;
            double acc[3] = { 0, 0, 0 };
            for (int k = 0; k < 4; ++k) {
                const int px = x0 + (k & 1), py = y0 + (k >> 1);
                const double w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                if (px < 0 || py < 0 || px >= ScreenW || py >= ScreenH) continue;
                const std::uint32_t c = screen[std::size_t(py) * ScreenW + px];
                acc[0] += w * (c & 255); acc[1] += w * (c >> 8 & 255); acc[2] += w * (c >> 16 & 255);
            }
            out[y * openfire::Width + x] = std::uint32_t(acc[0]) | std::uint32_t(acc[1]) << 8 | std::uint32_t(acc[2]) << 16;
        }
    // reticle: a thin cross with a gap and a dot, in the exact centre
    for (int i = 0; i < openfire::Width; ++i) {
        const int d = std::abs(i - 120);
        if (d < 8 || d > 90) continue;
        out[120 * openfire::Width + i] = out[119 * openfire::Width + i] = 0;
        out[i * openfire::Width + 120] = out[i * openfire::Width + 119] = 0;
    }
    for (int y = 118; y <= 121; ++y)
        for (int x = 118; x <= 121; ++x) out[y * openfire::Width + x] = bgrx(255, 40, 40);
}

}   // namespace

int main(int argc, char **argv)
{
    int seconds = 20;
    double zoom = 4.0;
    bool always = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (a == "--zoom" && i + 1 < argc) zoom = std::max(1.0, std::atof(argv[++i]));
        else if (a == "--always") always = true;
        else { std::printf("usage: %s [--seconds n] [--zoom z] [--always]\n", argv[0]); return 1; }
    }

    openfire::Scope scope;
    if (!scope.isOpen()) { std::printf("cannot open a UDP socket\n"); return 1; }
    scope.setAlwaysSend(always);

    std::vector<std::uint32_t> screen(std::size_t(ScreenW) * ScreenH);
    std::vector<std::uint32_t> view(std::size_t(openfire::Width) * openfire::Height);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto next = start;
    auto lastPrint = start - std::chrono::seconds(1);
    for (;;) {
        const double t = std::chrono::duration<double>(clock::now() - start).count();
        if (t >= seconds) break;

        drawScreen(screen, t);   // the game's normal frame

        scope.update();
        for (int p = 1; p <= openfire::MaxPlayers; ++p) {
            if (!scope.hasScope(p)) continue;
            // the aim point of player p on the screen, in screen pixels (from the lightgun in a real game)
            const double aimX = 480 + 400 * std::sin(t * 0.7 + p * 1.3), aimY = 250 + 90 * std::sin(t * 1.1 + p);
            cropToScope(screen, aimX, aimY, zoom, view.data());
            scope.sendFrame(p, openfire::Pixels::BGRX8888, view.data());
        }

        if (clock::now() - lastPrint >= std::chrono::seconds(1)) {
            lastPrint = clock::now();
            std::printf("%5.1f s  daemon %s  scopes:", t, scope.daemonSeen() ? "yes" : "no ");
            for (int p = 1; p <= openfire::MaxPlayers; ++p) std::printf(" P%d=%s", p, scope.port(p) ? "yes" : "-");
            std::printf("\n");
            std::fflush(stdout);
        }

        next += std::chrono::microseconds(16667);   // 60 fps; a real game calls this from its own loop
        if (next > clock::now()) std::this_thread::sleep_until(next); else next = clock::now();
    }
    return 0;   // the destructor of scope closes the socket: the scopes show NO SIGNAL about 0.5 s later
}
