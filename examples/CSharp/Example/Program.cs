// Program.cs - a console "game" for the OpenFIRE scopes, in C#.
//
// It draws a 240x240 scope view for every player that has a scope (a zoomed landscape with a moving target and a
// reticle) and sends it at 60 fps. Press P to pause or resume (the scopes keep the last image), Q to quit.
// Without a keyboard (redirected input) it pauses by itself from 10 s to 13 s.
//
//     dotnet run                       60 fps, until Q (or 20 s without a keyboard)
//     dotnet run -- --always           also without scopes: watch the daemon's Preview tab
//     dotnet run -- --format rgb565    rgb888 | rgb565 | bgrx | rgba
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE

using System;
using System.Threading;
using OpenFIRE;

static class Program
{
    const int W = OpenFIREScope.Width, H = OpenFIREScope.Height;

    static int Main(string[] args)
    {
        var pixels = ScopePixels.RGB888;
        bool always = false;
        double seconds = 20;
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "--always") always = true;
            else if (args[i] == "--seconds" && i + 1 < args.Length) seconds = double.Parse(args[++i]);
            else if (args[i] == "--format" && i + 1 < args.Length)
            {
                string f = args[++i];
                pixels = f == "rgb565" ? ScopePixels.RGB565 : f == "bgrx" ? ScopePixels.BGRX8888
                       : f == "rgba" ? ScopePixels.RGBA8888 : ScopePixels.RGB888;
            }
            else { Console.WriteLine("usage: Example [--always] [--seconds n] [--format rgb888|rgb565|bgrx|rgba]"); return 1; }
        }
        bool keyboard = !Console.IsInputRedirected;

        using (var scope = new OpenFIREScope())
        {
            scope.AlwaysSend = always;
            var image = new byte[W * H * 4];
            bool paused = false;
            long start = scope.NowMs, lastPrint = -1000;
            double next = start;
            Console.WriteLine(keyboard ? "P = pause / resume, Q = quit" : "no keyboard: pause from 10 s to 13 s");

            while (true)
            {
                long now = scope.NowMs;
                double t = (now - start) / 1000.0;
                if (keyboard)
                {
                    if (Console.KeyAvailable)
                    {
                        var key = Console.ReadKey(true).Key;
                        if (key == ConsoleKey.Q || key == ConsoleKey.Escape) break;
                        if (key == ConsoleKey.P) paused = !paused;
                    }
                }
                else
                {
                    if (t >= seconds) break;
                    paused = t >= 10 && t < 13;
                }

                scope.Update();   // discovery + pause packets, never blocks
                if (paused)
                {
                    scope.SetPaused(0, true);   // all players: the scopes keep the last image
                }
                else
                {
                    for (int p = 1; p <= OpenFIREScope.MaxPlayers; p++)
                    {
                        if (!scope.HasScope(p)) continue;   // no scope: render nothing for that player
                        RenderScopeView(p, t, pixels, image);
                        scope.SendFrame(p, image, pixels);
                    }
                }

                if (now - lastPrint >= 1000)
                {
                    lastPrint = now;
                    Console.Write($"{t,5:0.0} s  daemon {(scope.DaemonSeen ? "yes" : "no ")}  scopes:");
                    for (int p = 1; p <= OpenFIREScope.MaxPlayers; p++) Console.Write($" P{p}={(scope.GetPort(p) != 0 ? "yes" : "-")}");
                    Console.WriteLine(paused ? "  (paused)" : "");
                }

                next += 1000.0 / 60;   // fixed 60 fps; a real game calls this from its own loop
                double wait = next - scope.NowMs;
                if (wait > 0) Thread.Sleep((int)wait); else next = scope.NowMs;
            }
        }
        // Dispose closed the socket: the scopes show NO SIGNAL about 0.5 s later
        return 0;
    }

    // A fake scope view: in a game this is a second camera with a narrow field of view (see ../README.md).
    static void RenderScopeView(int player, double t, ScopePixels pixels, byte[] o)
    {
        double pan = t * 30 + player * 250, tx = 120 + 60 * Math.Sin(t * 1.4 + player), ty = 125 + 25 * Math.Cos(t);
        int i = 0;
        for (int y = 0; y < H; y++)
        {
            for (int x = 0; x < W; x++)
            {
                double ridge = 110 + 30 * Math.Sin((x + pan) * 0.025) + 8 * Math.Sin((x + pan) * 0.13);
                int r, g, b;
                if (y < ridge) { r = 120 + y / 4; g = 170 + y / 5; b = 235; }                         // sky
                else { r = 90 - (y - 110) / 4; g = 110 + (int)(15 * Math.Sin((x + pan) * 0.3 + y * 0.2)); b = 60; }   // hills
                double dx = x - tx, dy = y - ty;
                if (dx * dx + dy * dy < 15 * 15) { r = 250; g = 200; b = 30; }                         // target
                int ax = Math.Abs(x - 120), ay = Math.Abs(y - 120);                                     // reticle
                if ((ax <= 1 && ay > 10 && ay < 70) || (ay <= 1 && ax > 10 && ax < 70)) r = g = b = 20;
                switch (pixels)
                {
                    case ScopePixels.RGB888: o[i++] = (byte)r; o[i++] = (byte)g; o[i++] = (byte)b; break;
                    case ScopePixels.RGBA8888: o[i++] = (byte)r; o[i++] = (byte)g; o[i++] = (byte)b; o[i++] = 255; break;
                    case ScopePixels.BGRX8888: o[i++] = (byte)b; o[i++] = (byte)g; o[i++] = (byte)r; o[i++] = 0; break;
                    case ScopePixels.RGB565:
                        int v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                        o[i++] = (byte)v; o[i++] = (byte)(v >> 8); break;   // little-endian
                }
            }
        }
    }
}
