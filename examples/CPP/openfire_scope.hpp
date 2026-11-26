/*  openfire_scope.hpp - C++17 wrapper of openfire_scope.h (the C library in ../C).

    Copy openfire_scope.h and this file into your game. In ONE .cpp file write

        #define OPENFIRE_SCOPE_IMPLEMENTATION
        #include "openfire_scope.hpp"

    and include it normally everywhere else. On Windows link ws2_32 (MSVC does it by itself).

        openfire::Scope scope;                    // opens the socket and starts the discovery
        // every game frame:
        scope.update();
        for (int p = 1; p <= openfire::MaxPlayers; ++p)
            if (scope.hasScope(p)) {
                renderScopeView(p, pixels);       // 240x240
                scope.sendFrame(p, openfire::Pixels::RGBA8888, pixels.data());
            }
        // the destructor closes the socket

    See README.md next to this file.

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
*/
#ifndef OPENFIRE_SCOPE_HPP
#define OPENFIRE_SCOPE_HPP

#include "openfire_scope.h"   // the C library: keep it next to this file

#include <cstdint>

namespace openfire {

constexpr int Width = OFS_WIDTH;              // the scope image is always 240 x 240
constexpr int Height = OFS_HEIGHT;
constexpr int MaxPlayers = OFS_MAX_PLAYERS;   // players 1..4
constexpr std::uint16_t DefaultDiscoveryPort = OFS_DISCOVERY_PORT;

// How the image is laid out in memory.
enum class Pixels : int {
    RGB888 = OFS_PIXELS_RGB888,       // 3 bytes R, G, B
    RGB565 = OFS_PIXELS_RGB565,       // 2 bytes, 16-bit little-endian
    BGRX8888 = OFS_PIXELS_BGRX8888,   // 4 bytes B, G, R, X (Direct3D B8G8R8A8, Windows bitmaps)
    RGBA8888 = OFS_PIXELS_RGBA8888,   // 4 bytes R, G, B, A (OpenGL, Vulkan R8G8B8A8)
};

enum class Result : int {
    Ok = OFS_OK,
    NoScope = OFS_NO_SCOPE,   // that player has no scope right now: nothing sent, not an error
    Error = OFS_ERROR,
};

class Scope {
public:
    // Opens the UDP socket and sends the first discovery request. discoveryPort 0 = 47810.
    explicit Scope(std::uint16_t discoveryPort = 0) { m_open = ofs_open(&m_c, discoveryPort) == 0; }
    ~Scope() { ofs_close(&m_c); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    // false if the socket could not be opened (then every call does nothing).
    bool isOpen() const { return m_open; }

    // Once per game frame, never blocks: discovery once a second, answers, pause packets.
    void update() { ofs_update(&m_c); }

    // true if player 1..4 has a paired scope right now (or alwaysSend is on).
    bool hasScope(int player) const { return ofs_has_scope(&m_c, player) != 0; }

    // Sends one 240x240 image. stride = bytes from one row to the next (0 = tightly packed);
    // flipY = true when the first row in memory is the bottom of the image (glReadPixels).
    Result sendFrame(int player, Pixels pixels, const void *image, int stride = 0, bool flipY = false)
    {
        return static_cast<Result>(ofs_send_frame(&m_c, player, static_cast<int>(pixels), image, stride,
                                                  flipY ? OFS_FLIP_Y : 0));
    }

    // Pause menu: the scope keeps the last image. player 0 = all players. sendFrame() ends it.
    void setPaused(int player, bool paused) { ofs_set_paused(&m_c, player, paused ? 1 : 0); }

    // A new match from scratch (optional: a new session is made at every start).
    void newSession() { ofs_new_session(&m_c); }

    // Testing only: send to the default ports 47811-47814 even without scopes (daemon Preview tab, fake_daemon.py).
    void setAlwaysSend(bool on) { m_c.always_send = on ? 1 : 0; }

    bool daemonSeen() const { return m_c.daemon_seen != 0; }                 // the daemon answers
    std::uint16_t port(int player) const                                      // video port, 0 = no scope
    {
        return player >= 1 && player <= MaxPlayers ? m_c.port[player] : 0;
    }

    static std::int64_t nowMs() { return ofs_now_ms(); }                     // monotonic clock in ms

private:
    ofs_client m_c{};
    bool m_open = false;
};

}   // namespace openfire

#endif   // OPENFIRE_SCOPE_HPP
