// OpenFIREScope.cs - send a game's video to the OpenFIRE scopes through the OpenFIRE Scope Daemon.
//
// One file, no dependencies, .NET Standard 2.0: .NET Framework 4.6.1+, .NET 5+, Mono, Unity, MonoGame, Stride, Godot C#.
// Copy it into your project.
//
//     var scope = new OpenFIRE.OpenFIREScope();          // opens the socket, starts the discovery
//     // every game frame:
//     scope.Update();                                     // never blocks
//     for (int p = 1; p <= OpenFIREScope.MaxPlayers; p++)
//         if (scope.HasScope(p)) {
//             RenderScopeView(p, pixels);                 // 240x240
//             scope.SendFrame(p, pixels, ScopePixels.RGBA8888);
//         }
//     scope.Dispose();                                    // at game end
//
// See README.md next to this file.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE

using System;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;

namespace OpenFIRE
{
    /// <summary>How the image is laid out in memory.</summary>
    public enum ScopePixels
    {
        /// <summary>3 bytes: R, G, B.</summary>
        RGB888 = 0,
        /// <summary>2 bytes: 16-bit little-endian, R in the 5 high bits.</summary>
        RGB565 = 1,
        /// <summary>4 bytes: B, G, R, X. Direct3D B8G8R8A8, Unity TextureFormat.BGRA32, System.Drawing Format32bppRgb.</summary>
        BGRX8888 = 2,
        /// <summary>4 bytes: R, G, B, A. OpenGL/Vulkan R8G8B8A8, Unity TextureFormat.RGBA32. Sent as RGB888.</summary>
        RGBA8888 = 3,
    }

    /// <summary>Result of <see cref="OpenFIREScope.SendFrame(int, byte[], ScopePixels, int, bool, int)"/>.</summary>
    public enum SendResult
    {
        Ok = 0,
        /// <summary>That player has no scope right now: nothing sent. Not an error.</summary>
        NoScope = 1,
        /// <summary>Bad arguments or a closed client.</summary>
        Error = -1,
    }

    /// <summary>
    /// The connection of a game to the OpenFIRE Scope Daemon: discovery of the scopes, video, pause.
    /// Not thread-safe: use it from one thread (normally the game loop), or one instance per thread.
    /// </summary>
    public sealed class OpenFIREScope : IDisposable
    {
        public const int Width = 240, Height = 240;         // the scope image is always 240 x 240
        public const int MaxPlayers = 4;                    // players 1..4
        public const int DefaultDiscoveryPort = 47810;      // the user can change it in the daemon settings

        const int Strips = 15, StripRows = 16, HeaderSize = 16;
        const int WhoamiEveryMs = 1000;                     // discovery request
        const int AnswerTimeoutMs = 3000;                   // no answer for so long: daemon gone, no scopes
        const int PresenceEveryMs = 200;                    // pause packet; the daemon forgets a source after 500 ms

        readonly Socket _sock;
        readonly IPEndPoint _discovery;
        readonly Stopwatch _clock = Stopwatch.StartNew();
        readonly Random _random = new Random();
        readonly int[] _port = new int[MaxPlayers + 1];
        readonly IPEndPoint[] _endPoint = new IPEndPoint[MaxPlayers + 1];
        readonly uint[] _frame = new uint[MaxPlayers + 1];
        readonly byte[] _format = new byte[MaxPlayers + 1];
        readonly bool[] _paused = new bool[MaxPlayers + 1];
        readonly long[] _nextPresenceMs = new long[MaxPlayers + 1];
        readonly byte[] _packet = new byte[HeaderSize + Width * StripRows * 4];
        readonly byte[] _receive = new byte[2048];
        byte[] _rows;                                       // only for SendFrame(IntPtr)
        uint _session;
        long _nextWhoamiMs, _lastAnswerMs;
        bool _disposed;

        /// <summary>1 = send to the default ports 47811-47814 even without a scope. For testing only
        /// (daemon's Preview tab, Tools/fake_daemon.py).</summary>
        public bool AlwaysSend { get; set; }

        /// <summary>True while the daemon answers the discovery.</summary>
        public bool DaemonSeen { get; private set; }

        /// <summary>Opens the UDP socket and sends the first discovery request. Throws SocketException if no
        /// socket can be opened.</summary>
        public OpenFIREScope(int discoveryPort = DefaultDiscoveryPort)
        {
            _discovery = new IPEndPoint(IPAddress.Loopback, discoveryPort > 0 ? discoveryPort : DefaultDiscoveryPort);
            _sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            _sock.Bind(new IPEndPoint(IPAddress.Loopback, 0));   // any free port: the answers come back here
            _sock.Blocking = false;
            _sock.SendBufferSize = 4 * 1024 * 1024;             // 15 datagrams in a row; on macOS also > 9 KB
            try
            {
                // Windows: without this, a datagram sent while the daemon is closed makes the next receive fail.
                const int SIO_UDP_CONNRESET = -1744830452;
                _sock.IOControl(SIO_UDP_CONNRESET, new byte[] { 0, 0, 0, 0 }, null);
            }
            catch (Exception) { /* not Windows */ }
            for (int p = 0; p <= MaxPlayers; p++) _format[p] = (byte)ScopePixels.RGB888;
            NewSession();
            Update();   // first discovery request at once
        }

        /// <summary>Monotonic milliseconds since this object was created.</summary>
        public long NowMs => _clock.ElapsedMilliseconds;

        /// <summary>The video port of a player, 0 if it has no scope.</summary>
        public int GetPort(int player) => player >= 1 && player <= MaxPlayers ? _port[player] : 0;

        /// <summary>True if player 1..4 has a paired scope (or AlwaysSend is on).</summary>
        public bool HasScope(int player) => player >= 1 && player <= MaxPlayers && (_port[player] != 0 || AlwaysSend);

        /// <summary>A new random session: call it when a brand-new match starts (optional).</summary>
        public void NewSession()
        {
            uint s;
            do s = (uint)_random.Next() ^ ((uint)_random.Next() << 16); while (s == 0 || s == _session);
            _session = s;
        }

        /// <summary>Call once per game frame. Never blocks: reads the daemon's answers, asks again once a second,
        /// sends the pause packet every 200 ms for the paused players.</summary>
        public void Update()
        {
            if (_disposed) return;
            long now = NowMs;

            // 1. answers of the daemon
            while (true)
            {
                int n;
                EndPoint from = new IPEndPoint(IPAddress.Any, 0);
                try
                {
                    if (_sock.Available == 0) break;
                    n = _sock.ReceiveFrom(_receive, ref from);
                }
                catch (SocketException) { break; }
                if (((IPEndPoint)from).Port != _discovery.Port) continue;   // only the daemon's answers
                ParseAnswer(Encoding.ASCII.GetString(_receive, 0, n));
                _lastAnswerMs = now;
                DaemonSeen = true;
            }
            if (DaemonSeen && now - _lastAnswerMs > AnswerTimeoutMs)   // daemon closed
            {
                Array.Clear(_port, 0, _port.Length);
                DaemonSeen = false;
            }

            // 2. discovery request once a second: scopes can be paired, switched off or moved at any time
            if (now >= _nextWhoamiMs)
            {
                Send(Encoding.ASCII.GetBytes("WHOAMI"), 6, _discovery);
                _nextWhoamiMs = now + WhoamiEveryMs;
            }

            // 3. pause packets: the 16-byte header alone, strip 0xFF
            for (int p = 1; p <= MaxPlayers; p++)
            {
                if (!_paused[p] || !HasScope(p) || now < _nextPresenceMs[p]) continue;
                WriteHeader(_packet, _format[p], _frame[p], 0xFF, p);
                Send(_packet, HeaderSize, EndPointOf(p));
                _nextPresenceMs[p] = now + PresenceEveryMs;
            }
        }

        /// <summary>Game paused (menu, pause screen) for one player 1..4, or for all with player 0: the scope keeps
        /// the last image instead of showing NO SIGNAL. The next SendFrame() of that player ends it.</summary>
        public void SetPaused(int player, bool paused)
        {
            for (int p = 1; p <= MaxPlayers; p++)
            {
                if (player != 0 && p != player) continue;
                if (paused && !_paused[p]) _nextPresenceMs[p] = 0;   // first packet at the next Update()
                _paused[p] = paused;
            }
        }

        /// <summary>Sends one 240x240 image to the scope of a player.</summary>
        /// <param name="player">1 to 4.</param>
        /// <param name="image">The pixels, first row at the top (or at the bottom with flipY).</param>
        /// <param name="pixels">How the pixels are laid out.</param>
        /// <param name="stride">Bytes from one row to the next; 0 = tightly packed.</param>
        /// <param name="flipY">True when the first row in memory is the bottom of the image (OpenGL, Unity).</param>
        /// <param name="offset">Where the image starts in the array.</param>
        public SendResult SendFrame(int player, byte[] image, ScopePixels pixels, int stride = 0, bool flipY = false, int offset = 0)
        {
            if (_disposed || image == null || player < 1 || player > MaxPlayers || pixels < ScopePixels.RGB888 || pixels > ScopePixels.RGBA8888)
                return SendResult.Error;
            int bppIn = BytesPerPixel(pixels);
            if (stride == 0) stride = Width * bppIn;
            if (stride < Width * bppIn || offset < 0 || (long)offset + (long)stride * (Height - 1) + Width * bppIn > image.Length)
                return SendResult.Error;
            if (!HasScope(player)) return SendResult.NoScope;

            ScopePixels wire = pixels == ScopePixels.RGBA8888 ? ScopePixels.RGB888 : pixels;   // format in the header
            int bppWire = BytesPerPixel(wire);
            IPEndPoint to = EndPointOf(player);
            _paused[player] = false;
            _format[player] = (byte)wire;
            _frame[player]++;   // one number per frame and per player; it may wrap

            for (int strip = 0; strip < Strips; strip++)
            {
                WriteHeader(_packet, (byte)wire, _frame[player], strip, player);
                int o = HeaderSize;
                for (int row = 0; row < StripRows; row++)
                {
                    int y = strip * StripRows + row;
                    int line = offset + (flipY ? Height - 1 - y : y) * stride;
                    if (pixels == ScopePixels.RGBA8888)
                    {
                        for (int x = 0; x < Width; x++, line += 4)
                        {
                            _packet[o++] = image[line];
                            _packet[o++] = image[line + 1];
                            _packet[o++] = image[line + 2];
                        }
                    }
                    else
                    {
                        Buffer.BlockCopy(image, line, _packet, o, Width * bppWire);
                        o += Width * bppWire;
                    }
                }
                Send(_packet, o, to);
            }
            return SendResult.Ok;
        }

        /// <summary>The same, from unmanaged memory (a mapped Direct3D texture, a native buffer). stride must be
        /// given if the rows are padded.</summary>
        public SendResult SendFrame(int player, IntPtr image, ScopePixels pixels, int stride = 0, bool flipY = false)
        {
            if (image == IntPtr.Zero || pixels < ScopePixels.RGB888 || pixels > ScopePixels.RGBA8888) return SendResult.Error;
            if (!HasScope(player)) return player >= 1 && player <= MaxPlayers ? SendResult.NoScope : SendResult.Error;
            int rowBytes = Width * BytesPerPixel(pixels);
            if (stride == 0) stride = rowBytes;
            if (_rows == null) _rows = new byte[Width * Height * 4];
            for (int y = 0; y < Height; y++)   // copy only the pixels, without the padding
                Marshal.Copy(new IntPtr(image.ToInt64() + (long)y * stride), _rows, y * rowBytes, rowBytes);
            return SendFrame(player, _rows, pixels, rowBytes, flipY);
        }

        /// <summary>Closes the socket. The scopes show NO SIGNAL (or the daemon's screen capture) about 0.5 s later.</summary>
        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            _sock.Close();
            Array.Clear(_port, 0, _port.Length);
            DaemonSeen = false;
        }

        // ------------------------------------------------------------------------------------------- private

        static int BytesPerPixel(ScopePixels p) => p == ScopePixels.RGB888 ? 3 : p == ScopePixels.RGB565 ? 2 : 4;

        // "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> _port[]
        void ParseAnswer(string text)
        {
            var ports = new int[MaxPlayers + 1];
            foreach (string line in text.Split('\n'))
            {
                if (!line.StartsWith("OF_SCOPE:P", StringComparison.Ordinal) || line.Length < 11) continue;
                int player = line[10] - '0';
                if (player < 1 || player > MaxPlayers || (line.Length > 11 && line[11] != ';')) continue;
                int i = line.IndexOf(";PORT=", StringComparison.Ordinal);
                if (i < 0) continue;
                int end = i + 6, port = 0;
                while (end < line.Length && line[end] >= '0' && line[end] <= '9' && port < 65536) port = port * 10 + (line[end++] - '0');
                if (port > 0 && port < 65536) ports[player] = port;
            }
            Array.Copy(ports, _port, ports.Length);
        }

        IPEndPoint EndPointOf(int player)
        {
            int port = _port[player] != 0 ? _port[player] : DefaultDiscoveryPort + player;
            if (_endPoint[player] == null || _endPoint[player].Port != port) _endPoint[player] = new IPEndPoint(IPAddress.Loopback, port);
            return _endPoint[player];
        }

        void WriteHeader(byte[] h, byte format, uint frame, int strip, int player)
        {
            h[0] = (byte)'O'; h[1] = (byte)'F'; h[2] = 1; h[3] = format;
            h[4] = (byte)frame; h[5] = (byte)(frame >> 8); h[6] = (byte)(frame >> 16); h[7] = (byte)(frame >> 24);
            h[8] = (byte)strip; h[9] = Strips; h[10] = (byte)player;
            h[11] = (byte)_session; h[12] = (byte)(_session >> 8); h[13] = (byte)(_session >> 16); h[14] = (byte)(_session >> 24);
            h[15] = 0;
        }

        void Send(byte[] data, int length, IPEndPoint to)
        {
            // errors are ignored on purpose: a lost strip only makes that frame incomplete
            try { _sock.SendTo(data, 0, length, SocketFlags.None, to); }
            catch (SocketException) { }
        }
    }
}
