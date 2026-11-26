/*  example_basic.c - the smallest complete "game" for the OpenFIRE scopes.

    It draws an animated 240x240 scope view (moving landscape, a target, a crosshair) and sends it at 60 fps to
    every player that has a scope. After 10 seconds it "pauses the game" for 3 seconds (the scopes keep the last
    image), then plays again, and at the end it closes (the scopes show NO SIGNAL).

    Build:
        Linux / macOS :  cc -O2 -o example_basic example_basic.c -lm
        Windows MinGW :  gcc -O2 -o example_basic.exe example_basic.c -lws2_32
        Windows MSVC  :  cl /O2 example_basic.c

    Run:  ./example_basic [--fps 60] [--seconds 20] [--format rgb888|rgb565|bgrx|rgba] [--always]
          --always sends to P1-P4 even without scopes (to watch the daemon's Preview tab without hardware).

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
*/
#define OPENFIRE_SCOPE_IMPLEMENTATION
#include "openfire_scope.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
  #include <windows.h>
  static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
  #include <time.h>
  static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
#endif

/* -------------------------------------------------------------------------- a fake "scope view" renderer */
/* In a real game this is a second camera looking where the gun aims, with a narrow field of view, rendered
   into a 240x240 target (see ../README.md, "What to show on the scope"). Here we just draw something. */
static void render_scope_view(int player, double t, uint8_t *rgba)
{
    const double pan = t * 40.0 + player * 300.0;   /* the view slowly pans across the landscape */
    const double tx = 120 + 70 * sin(t * 1.3 + player), ty = 130 + 30 * cos(t * 0.9);   /* a moving target */
    int x, y;
    for (y = 0; y < OFS_HEIGHT; y++) {
        for (x = 0; x < OFS_WIDTH; x++) {
            uint8_t *p = rgba + (y * OFS_WIDTH + x) * 4;
            const double hills = 140 + 25 * sin((x + pan) * 0.03) + 10 * sin((x + pan) * 0.11);
            int r, g, b;
            if (y < hills) { r = 90 + y / 3; g = 150 + y / 4; b = 230; }                       /* sky   */
            else           { r = 40 + (int)(20 * sin((x + pan) * 0.2)); g = 120 - (y - 140) / 3; b = 40; } /* grass */
            if ((x - tx) * (x - tx) + (y - ty) * (y - ty) < 12 * 12) { r = 230; g = 40; b = 30; }  /* target */
            /* crosshair: the scope only rotates the image, the game draws its own reticle */
            if ((abs(x - 120) <= 1 && abs(y - 120) > 6 && abs(y - 120) < 60) ||
                (abs(y - 120) <= 1 && abs(x - 120) > 6 && abs(x - 120) < 60)) { r = g = b = 0; }
            p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255;
        }
    }
}

/* RGBA -> the other layouts, only to show them; a real game sends what it already has in memory */
static void convert(const uint8_t *rgba, int pixels, uint8_t *out)
{
    int i;
    for (i = 0; i < OFS_WIDTH * OFS_HEIGHT; i++, rgba += 4) {
        switch (pixels) {
        case OFS_PIXELS_RGB888:   *out++ = rgba[0]; *out++ = rgba[1]; *out++ = rgba[2]; break;
        case OFS_PIXELS_BGRX8888: *out++ = rgba[2]; *out++ = rgba[1]; *out++ = rgba[0]; *out++ = 0; break;
        case OFS_PIXELS_RGB565: {
            const unsigned v = ((rgba[0] >> 3) << 11) | ((rgba[1] >> 2) << 5) | (rgba[2] >> 3);
            *out++ = (uint8_t)v; *out++ = (uint8_t)(v >> 8);   /* little-endian */
            break;
        }
        }
    }
}

int main(int argc, char **argv)
{
    int fps = 60, seconds = 20, pixels = OFS_PIXELS_RGBA8888, always = 0, i, p;
    static uint8_t rgba[OFS_WIDTH * OFS_HEIGHT * 4], other[OFS_WIDTH * OFS_HEIGHT * 4];
    ofs_client scope;
    int64_t start, last_print = 0;
    double next;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--always")) always = 1;
        else if (!strcmp(argv[i], "--format") && i + 1 < argc) {
            const char *f = argv[++i];
            pixels = !strcmp(f, "rgb888") ? OFS_PIXELS_RGB888 : !strcmp(f, "rgb565") ? OFS_PIXELS_RGB565
                   : !strcmp(f, "bgrx") ? OFS_PIXELS_BGRX8888 : OFS_PIXELS_RGBA8888;
        } else { printf("usage: %s [--fps n] [--seconds n] [--format rgb888|rgb565|bgrx|rgba] [--always]\n", argv[0]); return 1; }
    }
    if (fps < 1) fps = 1;

    if (ofs_open(&scope, 0) != 0) { printf("cannot open a UDP socket\n"); return 1; }
    scope.always_send = always;

    start = ofs_now_ms();
    next = (double)start;
    for (;;) {
        const int64_t now = ofs_now_ms();
        const double t = (now - start) / 1000.0;
        const int paused = t >= 10.0 && t < 13.0;   /* "pause menu" from 10 s to 13 s */
        if (t >= seconds) break;

        ofs_update(&scope);   /* discovery + pause packets, never blocks */
        for (p = 1; p <= OFS_MAX_PLAYERS; p++) {
            if (!ofs_has_scope(&scope, p)) continue;
            if (paused) { ofs_set_paused(&scope, p, 1); continue; }   /* the scope keeps the last image */
            render_scope_view(p, t, rgba);
            if (pixels == OFS_PIXELS_RGBA8888) ofs_send_frame(&scope, p, OFS_PIXELS_RGBA8888, rgba, 0, 0);
            else { convert(rgba, pixels, other); ofs_send_frame(&scope, p, pixels, other, 0, 0); }
        }

        if (now - last_print >= 1000) {
            last_print = now;
            printf("%5.1f s  daemon %s  scopes:", t, scope.daemon_seen ? "yes" : "no ");
            for (p = 1; p <= OFS_MAX_PLAYERS; p++) printf(" P%d=%s", p, scope.port[p] ? "yes" : "-");
            printf("%s\n", paused ? "  (paused)" : "");
            fflush(stdout);
        }

        /* fixed frame rate; a real game just calls this from its own loop */
        next += 1000.0 / fps;
        if (next > (double)ofs_now_ms()) sleep_ms((int)(next - (double)ofs_now_ms()));
        else next = (double)ofs_now_ms();
    }
    ofs_close(&scope);
    return 0;
}
