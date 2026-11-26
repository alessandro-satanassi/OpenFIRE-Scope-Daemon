/*  openfire_scope.h - send a game's video to the OpenFIRE scopes through the OpenFIRE Scope Daemon.

    Single-file C library (C99, no dependencies). Windows, Linux and macOS.

    Usage: in ONE .c/.cpp file of the game write

        #define OPENFIRE_SCOPE_IMPLEMENTATION
        #include "openfire_scope.h"

    and include it normally everywhere else. On Windows link ws2_32 (MSVC does it by itself).

    A typical game loop:

        ofs_client scope;
        ofs_open(&scope, 0);                      // 0 = default discovery port 47810
        for (;;) {
            ofs_update(&scope);                   // once per game frame: discovery, pause packets
            for (int p = 1; p <= 4; p++) {
                if (!ofs_has_scope(&scope, p)) continue;
                render_scope_view(p, pixels);     // 240x240 image of what player p sees in the scope
                ofs_send_frame(&scope, p, OFS_PIXELS_RGBA8888, pixels, 240 * 4, 0);
            }
        }
        ofs_close(&scope);

    See README.md next to this file for the whole explanation.

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions: the above
    copyright notice and this permission notice shall be included in all copies or substantial portions of the
    Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
*/
#ifndef OPENFIRE_SCOPE_H
#define OPENFIRE_SCOPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------- constants */
#define OFS_WIDTH            240     /* the scope image is always 240 x 240 pixels                       */
#define OFS_HEIGHT           240
#define OFS_STRIPS           15      /* sent as 15 strips of 16 rows, one UDP datagram each              */
#define OFS_STRIP_ROWS       16
#define OFS_MAX_PLAYERS      4
#define OFS_DISCOVERY_PORT   47810   /* default; the user can change it in the daemon settings           */

/* Pixel layouts accepted by ofs_send_frame(). The first three go to the daemon as they are; RGBA8888 (the
   usual layout of a texture read back from the GPU) is converted to RGB888 while the strips are packed. */
#define OFS_PIXELS_RGB888    0       /* 3 bytes: R, G, B                                                   */
#define OFS_PIXELS_RGB565    1       /* 2 bytes: 16-bit little-endian, R in the 5 high bits                */
#define OFS_PIXELS_BGRX8888  2       /* 4 bytes: B, G, R, X (X ignored) - e.g. a DirectX BGRA texture      */
#define OFS_PIXELS_RGBA8888  3       /* 4 bytes: R, G, B, A (A ignored) - e.g. OpenGL/Vulkan/Unity RGBA32   */

/* Flags of ofs_send_frame(). */
#define OFS_FLIP_Y           1       /* the first row in memory is the BOTTOM of the image (OpenGL, Unity) */

/* Results of ofs_send_frame(). */
#define OFS_OK               0
#define OFS_NO_SCOPE         1       /* no scope for that player right now: nothing sent (not an error)    */
#define OFS_ERROR           -1       /* bad arguments, or the socket refused a datagram                    */

/* ------------------------------------------------------------------------------------------- the client */
typedef struct ofs_client {
    /* read-only for the game */
    uint16_t port[OFS_MAX_PLAYERS + 1];        /* video port of each player (index 1..4), 0 = no scope   */
    int      daemon_seen;                      /* 1 while the daemon answers the discovery               */
    /* settings, may be changed after ofs_open() */
    int      always_send;                      /* 1 = send to the default ports even without a scope:    */
                                               /*     for testing with the daemon's Preview tab only     */
    /* private */
    intptr_t sock;
    uint16_t discovery_port;
    uint32_t session;
    uint32_t frame[OFS_MAX_PLAYERS + 1];
    uint8_t  format[OFS_MAX_PLAYERS + 1];
    uint8_t  paused[OFS_MAX_PLAYERS + 1];
    int64_t  next_presence_ms[OFS_MAX_PLAYERS + 1];
    int64_t  next_whoami_ms;
    int64_t  last_answer_ms;
    uint8_t  packet[16 + OFS_WIDTH * OFS_STRIP_ROWS * 4];
} ofs_client;

/* Opens the UDP socket and sends the first discovery request. discovery_port 0 = 47810.
   Returns 0, or -1 if no socket can be opened. */
int  ofs_open(ofs_client *c, uint16_t discovery_port);

/* Closes the socket. The scopes show "NO SIGNAL" (or the daemon's screen capture) about 0.5 s later. */
void ofs_close(ofs_client *c);

/* Call once per game frame (it never blocks): asks the daemon which scopes exist once a second, reads its
   answers, and sends the "paused" packet every 200 ms for the players set with ofs_set_paused(). */
void ofs_update(ofs_client *c);

/* 1 if player 1..4 has a paired scope (or always_send is on). */
int  ofs_has_scope(const ofs_client *c, int player);

/* Sends one 240x240 image to the scope of a player. pixels: OFS_PIXELS_*, stride: bytes from one row to the
   next (0 = tightly packed), flags: OFS_FLIP_Y or 0. It also ends the pause of that player. Never blocks:
   if the system has no room for a strip it is dropped, and the daemon simply waits for the next frame. */
int  ofs_send_frame(ofs_client *c, int player, int pixels, const void *image, int stride, int flags);

/* Game paused (menu, pause screen) for one player (1..4) or for all (0): the scope keeps the last image
   instead of showing "NO SIGNAL". ofs_update() sends the packet every 200 ms. */
void ofs_set_paused(ofs_client *c, int player, int paused);

/* A new random session: call it when a new game/match starts from scratch (ofs_open() already does). */
void ofs_new_session(ofs_client *c);

/* Monotonic milliseconds, the clock used by the library (handy for the game's own timing too). */
int64_t ofs_now_ms(void);

#ifdef __cplusplus
}
#endif
#endif /* OPENFIRE_SCOPE_H */

/* =========================================================================================================
   IMPLEMENTATION
   ========================================================================================================= */
#ifdef OPENFIRE_SCOPE_IMPLEMENTATION
#ifndef OPENFIRE_SCOPE_IMPLEMENTED
#define OPENFIRE_SCOPE_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  #ifndef SIO_UDP_CONNRESET
    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
  #endif
  typedef SOCKET ofs__sock_t;
  #define OFS__BAD_SOCKET INVALID_SOCKET
  #define ofs__closesocket closesocket
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <time.h>
  #include <unistd.h>
  typedef int ofs__sock_t;
  #define OFS__BAD_SOCKET (-1)
  #define ofs__closesocket close
#endif

#define OFS__WHOAMI_EVERY_MS    1000   /* discovery request                                       */
#define OFS__ANSWER_TIMEOUT_MS  3000   /* no answer for so long: daemon gone, no scopes            */
#define OFS__PRESENCE_EVERY_MS  200    /* pause packet; the daemon forgets a source after 500 ms   */

int64_t ofs_now_ms(void)
{
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static struct sockaddr_in ofs__addr(uint16_t port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* the daemon only listens on this PC */
    return a;
}

static void ofs__sendto(ofs_client *c, const void *data, int len, uint16_t port)
{
    struct sockaddr_in to = ofs__addr(port);
    /* errors are ignored on purpose: a lost strip only makes that frame incomplete */
    (void)sendto((ofs__sock_t)c->sock, (const char *)data, len, 0, (const struct sockaddr *)&to, sizeof to);
}

void ofs_new_session(ofs_client *c)
{
    /* any random 32-bit value except 0: time, address and the previous value mixed together */
    uint32_t x = (uint32_t)ofs_now_ms() ^ (uint32_t)(uintptr_t)c ^ (c->session * 2654435761u) ^ (uint32_t)rand();
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->session = x ? x : 1;
}

int ofs_open(ofs_client *c, uint16_t discovery_port)
{
    ofs__sock_t s;
    struct sockaddr_in me;
    int sndbuf = 4 * 1024 * 1024;   /* 15 datagrams in a row always fit; on macOS it also allows > 9 KB */
    int p;

    memset(c, 0, sizeof *c);
    c->sock = (intptr_t)OFS__BAD_SOCKET;
    c->discovery_port = discovery_port ? discovery_port : OFS_DISCOVERY_PORT;
#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    }
#endif
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == OFS__BAD_SOCKET) return -1;
    me = ofs__addr(0);   /* any free local port: the discovery answer comes back here */
    if (bind(s, (const struct sockaddr *)&me, sizeof me) != 0) { ofs__closesocket(s); return -1; }
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf, sizeof sndbuf);
#ifdef _WIN32
    {
        u_long on = 1;
        BOOL off = FALSE;
        DWORD bytes = 0;
        ioctlsocket(s, FIONBIO, &on);
        /* without this, a datagram sent while the daemon is closed makes the next recvfrom() fail */
        WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &bytes, NULL, NULL);
    }
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    c->sock = (intptr_t)s;
    for (p = 0; p <= OFS_MAX_PLAYERS; p++) c->format[p] = OFS_PIXELS_RGB888;
    ofs_new_session(c);
    ofs_update(c);   /* first discovery request at once */
    return 0;
}

void ofs_close(ofs_client *c)
{
    if ((ofs__sock_t)c->sock != OFS__BAD_SOCKET) {
        ofs__closesocket((ofs__sock_t)c->sock);
#ifdef _WIN32
        WSACleanup();
#endif
    }
    c->sock = (intptr_t)OFS__BAD_SOCKET;
    memset(c->port, 0, sizeof c->port);
    c->daemon_seen = 0;
}

/* "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> port[] */
static void ofs__parse_answer(ofs_client *c, char *text)
{
    uint16_t ports[OFS_MAX_PLAYERS + 1];
    char *line = text;
    memset(ports, 0, sizeof ports);
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (strncmp(line, "OF_SCOPE:P", 10) == 0 && line[10] >= '1' && line[10] <= '4' && (line[11] == ';' || line[11] == 0)) {
            const char *port = strstr(line, ";PORT=");
            const long value = port ? strtol(port + 6, NULL, 10) : 0;
            if (value > 0 && value < 65536) ports[line[10] - '0'] = (uint16_t)value;
        }
        line = next;
    }
    memcpy(c->port, ports, sizeof ports);
}

void ofs_update(ofs_client *c)
{
    const int64_t now = ofs_now_ms();
    char buf[2048];
    int p;
    if ((ofs__sock_t)c->sock == OFS__BAD_SOCKET) return;

    /* 1. answers of the daemon (non-blocking socket: returns at once when there is nothing) */
    for (;;) {
        struct sockaddr_in from;
        socklen_t fromLen = sizeof from;
        const int n = (int)recvfrom((ofs__sock_t)c->sock, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fromLen);
        if (n < 0) break;
        if (from.sin_port != htons(c->discovery_port)) continue;   /* only the daemon's answers */
        buf[n] = 0;
        ofs__parse_answer(c, buf);
        c->last_answer_ms = now;
        c->daemon_seen = 1;
    }
    if (c->daemon_seen && now - c->last_answer_ms > OFS__ANSWER_TIMEOUT_MS) {   /* daemon closed */
        memset(c->port, 0, sizeof c->port);
        c->daemon_seen = 0;
    }

    /* 2. discovery request once a second: scopes can be paired, switched off or moved at any time */
    if (now >= c->next_whoami_ms) {
        ofs__sendto(c, "WHOAMI", 6, c->discovery_port);
        c->next_whoami_ms = now + OFS__WHOAMI_EVERY_MS;
    }

    /* 3. pause packets: the 16-byte header alone, strip 0xFF */
    for (p = 1; p <= OFS_MAX_PLAYERS; p++) {
        uint8_t h[16];
        if (!c->paused[p] || !ofs_has_scope(c, p) || now < c->next_presence_ms[p]) continue;
        h[0] = 'O'; h[1] = 'F'; h[2] = 1; h[3] = c->format[p];
        h[4] = (uint8_t)c->frame[p]; h[5] = (uint8_t)(c->frame[p] >> 8);
        h[6] = (uint8_t)(c->frame[p] >> 16); h[7] = (uint8_t)(c->frame[p] >> 24);
        h[8] = 0xFF; h[9] = OFS_STRIPS; h[10] = (uint8_t)p;
        h[11] = (uint8_t)c->session; h[12] = (uint8_t)(c->session >> 8);
        h[13] = (uint8_t)(c->session >> 16); h[14] = (uint8_t)(c->session >> 24);
        h[15] = 0;
        ofs__sendto(c, h, 16, c->port[p] ? c->port[p] : (uint16_t)(47810 + p));
        c->next_presence_ms[p] = now + OFS__PRESENCE_EVERY_MS;
    }
}

int ofs_has_scope(const ofs_client *c, int player)
{
    if (player < 1 || player > OFS_MAX_PLAYERS) return 0;
    return c->port[player] != 0 || c->always_send;
}

void ofs_set_paused(ofs_client *c, int player, int paused)
{
    int p;
    for (p = 1; p <= OFS_MAX_PLAYERS; p++) {
        if (player != 0 && p != player) continue;
        if (paused && !c->paused[p]) c->next_presence_ms[p] = 0;   /* first packet at the next update */
        c->paused[p] = (uint8_t)(paused != 0);
    }
}

int ofs_send_frame(ofs_client *c, int player, int pixels, const void *image, int stride, int flags)
{
    static const int bpp_in[4] = { 3, 2, 4, 4 };
    const uint8_t *src = (const uint8_t *)image;
    uint16_t port;
    int wire, bpp_wire, strip;
    if ((ofs__sock_t)c->sock == OFS__BAD_SOCKET || player < 1 || player > OFS_MAX_PLAYERS || !image
        || pixels < 0 || pixels > OFS_PIXELS_RGBA8888) return OFS_ERROR;
    if (!ofs_has_scope(c, player)) return OFS_NO_SCOPE;
    port = c->port[player] ? c->port[player] : (uint16_t)(47810 + player);
    if (stride == 0) stride = OFS_WIDTH * bpp_in[pixels];
    wire = pixels == OFS_PIXELS_RGBA8888 ? OFS_PIXELS_RGB888 : pixels;   /* format written in the header */
    bpp_wire = wire == OFS_PIXELS_RGB565 ? 2 : wire == OFS_PIXELS_BGRX8888 ? 4 : 3;

    c->paused[player] = 0;
    c->format[player] = (uint8_t)wire;
    c->frame[player]++;   /* one number per frame and per player; it may wrap */

    for (strip = 0; strip < OFS_STRIPS; strip++) {
        uint8_t *h = c->packet, *out = c->packet + 16;
        int row;
        h[0] = 'O'; h[1] = 'F'; h[2] = 1; h[3] = (uint8_t)wire;
        h[4] = (uint8_t)c->frame[player]; h[5] = (uint8_t)(c->frame[player] >> 8);
        h[6] = (uint8_t)(c->frame[player] >> 16); h[7] = (uint8_t)(c->frame[player] >> 24);
        h[8] = (uint8_t)strip; h[9] = OFS_STRIPS; h[10] = (uint8_t)player;
        h[11] = (uint8_t)c->session; h[12] = (uint8_t)(c->session >> 8);
        h[13] = (uint8_t)(c->session >> 16); h[14] = (uint8_t)(c->session >> 24);
        h[15] = 0;
        for (row = 0; row < OFS_STRIP_ROWS; row++) {
            const int y = strip * OFS_STRIP_ROWS + row;
            const uint8_t *line = src + (size_t)((flags & OFS_FLIP_Y) ? OFS_HEIGHT - 1 - y : y) * (size_t)stride;
            if (pixels == OFS_PIXELS_RGBA8888) {
                int x;
                for (x = 0; x < OFS_WIDTH; x++, line += 4) { *out++ = line[0]; *out++ = line[1]; *out++ = line[2]; }
            } else {
                memcpy(out, line, (size_t)OFS_WIDTH * bpp_wire);
                out += OFS_WIDTH * bpp_wire;
            }
        }
        ofs__sendto(c, c->packet, 16 + OFS_WIDTH * OFS_STRIP_ROWS * bpp_wire, port);
    }
    return OFS_OK;
}

#endif /* OPENFIRE_SCOPE_IMPLEMENTED */
#endif /* OPENFIRE_SCOPE_IMPLEMENTATION */
