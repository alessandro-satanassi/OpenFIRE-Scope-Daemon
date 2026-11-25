// EN: of_test_sender: fake game for the bench. Sends animated test images in the OF UDP format to the
//     ports of the daemon (from the discovery on 127.0.0.1:47810, or --ports), with pauses (presence
//     packets) and session changes on request. No game is needed to test UDP, preview and, from phase 4,
//     the video on the scope.
// IT: of_test_sender: finto gioco per il banco. Manda immagini di prova animate nel formato UDP OF alle
//     porte del demone (dalla discovery su 127.0.0.1:47810, o --ports), con pause (pacchetti di presenza)
//     e cambi di sessione a richiesta. Non serve un gioco per provare UDP, anteprima e, dalla fase 4, il
//     video sul mirino.

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QStringList>
#include <QUdpSocket>
#include <QVariant>
#include <QVector>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include "OpenFIRE_Scope_Protocol.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

constexpr int W = OF_SCOPE_W;
constexpr int H = OF_SCOPE_H;
constexpr int StripPixels = W * OF_SCOPE_STRIP_ROWS;
constexpr double Pi = 3.14159265358979323846;

struct Player {
    int number = 0;
    quint16 port = 0;
    quint32 session = 0;
    quint32 frame = 0;
    int sent = 0;
    int errors = 0;
    qint64 lastPresenceMs = -1000;
};

quint32 newSession()
{
    quint32 s = 0;
    while (s == 0) s = QRandomGenerator::global()->generate();
    return s;
}

void hsv(double h, double s, double v, uint8_t *rgb)
{
    h = std::fmod(h, 1.0) * 6.0;
    const int i = int(h);
    const double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r, g, b;
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    rgb[0] = uint8_t(r * 255);
    rgb[1] = uint8_t(g * 255);
    rgb[2] = uint8_t(b * 255);
}

void fillRect(uint8_t *img, int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = qMax(0, y0); y < qMin(H, y0 + h); ++y)
        for (int x = qMax(0, x0); x < qMin(W, x0 + w); ++x) {
            uint8_t *p = img + (y * W + x) * 3;
            p[0] = r; p[1] = g; p[2] = b;
        }
}

// EN: 7-segment digit, s = segment length. / IT: cifra a 7 segmenti, s = lunghezza del segmento.
void digit(uint8_t *img, int d, int x, int y, int s, int t)
{
    static const uint8_t seg[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    const uint8_t m = seg[d % 10];
    if (m & 0x01) fillRect(img, x + t, y, s, t, 255, 255, 255);                 // a
    if (m & 0x02) fillRect(img, x + t + s, y + t, t, s, 255, 255, 255);         // b
    if (m & 0x04) fillRect(img, x + t + s, y + 2 * t + s, t, s, 255, 255, 255); // c
    if (m & 0x08) fillRect(img, x + t, y + 2 * t + 2 * s, s, t, 255, 255, 255); // d
    if (m & 0x10) fillRect(img, x, y + 2 * t + s, t, s, 255, 255, 255);         // e
    if (m & 0x20) fillRect(img, x, y + t, t, s, 255, 255, 255);                 // f
    if (m & 0x40) fillRect(img, x + t, y + t + s, s, t, 255, 255, 255);         // g
}

// EN: tables computed once: hue of every angle and shade of every pixel (darker towards the edge).
// IT: tabelle calcolate una volta: tinta di ogni angolo e ombra di ogni pixel (piu' scuro verso il bordo).
struct Tables {
    uint8_t hue[1024][3];
    std::vector<quint16> angle;   // 0..1023
    std::vector<uint8_t> shade;   // 0..255
    Tables() : angle(size_t(W) * H), shade(size_t(W) * H)
    {
        for (int i = 0; i < 1024; ++i) hsv(i / 1024.0, 0.75, 1.0, hue[i]);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const double dx = x - 119.5, dy = y - 119.5;
                const double a = std::atan2(dy, dx) / (2 * Pi) + 0.5;
                const double r = std::sqrt(dx * dx + dy * dy) / 170.0;
                angle[size_t(y) * W + x] = quint16(int(a * 1024) & 1023);
                shade[size_t(y) * W + x] = uint8_t(qBound(0.0, 255 * (0.95 - 0.5 * r), 255.0));
            }
    }
};

// EN: hue wheel turning, a ball going round, the player number and a frame counter.
// IT: ruota dei colori che gira, una palla che gira, il numero del giocatore e un contatore dei frame.
void render(const Tables &tb, uint8_t *img, int player, quint32 frame, double t)
{
    const int offset = int(((player - 1) * 0.25 + t * 0.1) * 1024) & 1023;
    for (int i = 0; i < W * H; ++i) {
        const uint8_t *c = tb.hue[(tb.angle[size_t(i)] + offset) & 1023];
        const unsigned s = tb.shade[size_t(i)];
        uint8_t *p = img + size_t(i) * 3;
        p[0] = uint8_t(c[0] * s >> 8);
        p[1] = uint8_t(c[1] * s >> 8);
        p[2] = uint8_t(c[2] * s >> 8);
    }
    const int bx = int(120 + 80 * std::cos(t * 2.0)), by = int(120 + 80 * std::sin(t * 2.0));
    for (int y = by - 14; y <= by + 14; ++y)
        for (int x = bx - 14; x <= bx + 14; ++x)
            if ((x - bx) * (x - bx) + (y - by) * (y - by) <= 196) fillRect(img, x, y, 1, 1, 255, 255, 255);
    fillRect(img, 104, 40, 32, 50, 0, 0, 0);
    digit(img, player, 108, 42, 20, 4);
    fillRect(img, 82, 150, 76, 44, 0, 0, 0);
    digit(img, int(frame / 100 % 10), 86, 154, 14, 3);
    digit(img, int(frame / 10 % 10), 110, 154, 14, 3);
    digit(img, int(frame % 10), 134, 154, 14, 3);
}

void packStrip(const uint8_t *img, int strip, int format, uint8_t *out)
{
    const uint8_t *src = img + size_t(strip) * StripPixels * 3;
    for (int i = 0; i < StripPixels; ++i, src += 3) {
        switch (format) {
        case OF_UDP_FMT_RGB888:
            *out++ = src[0]; *out++ = src[1]; *out++ = src[2];
            break;
        case OF_UDP_FMT_RGB565: {
            const unsigned v = ((src[0] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[2] >> 3);
            *out++ = uint8_t(v & 0xFF); *out++ = uint8_t(v >> 8);
            break;
        }
        default:
            *out++ = src[2]; *out++ = src[1]; *out++ = src[0]; *out++ = 0;
            break;
        }
    }
}

void header(uint8_t *p, int format, quint32 frame, int strip, int player, quint32 session)
{
    p[0] = 'O'; p[1] = 'F'; p[2] = OF_UDP_VERSION; p[3] = uint8_t(format);
    p[4] = uint8_t(frame); p[5] = uint8_t(frame >> 8); p[6] = uint8_t(frame >> 16); p[7] = uint8_t(frame >> 24);
    p[8] = uint8_t(strip); p[9] = OF_SCOPE_STRIPS; p[10] = uint8_t(player);
    p[11] = uint8_t(session); p[12] = uint8_t(session >> 8); p[13] = uint8_t(session >> 16); p[14] = uint8_t(session >> 24);
    p[15] = 0;
}

// EN: WHOAMI to the discovery: player -> port of the paired scopes. / IT: WHOAMI alla discovery: giocatore -> porta dei mirini abbinati.
QVector<quint16> discover(quint16 discoveryPort, QString *answer)
{
    QVector<quint16> ports(5, 0);
    QUdpSocket s;
    if (!s.bind(QHostAddress::LocalHost, 0)) return ports;
    s.writeDatagram(QByteArray(OF_CMD_WHOAMI), QHostAddress::LocalHost, discoveryPort);
    if (!s.waitForReadyRead(300)) return ports;
    const QByteArray data = s.receiveDatagram().data();
    *answer = QString::fromLatin1(data);
    for (const QByteArray &line : data.split('\n')) {
        if (!line.startsWith(OF_MSG_SCOPE "P")) continue;
        const QList<QByteArray> parts = line.mid(int(sizeof(OF_MSG_SCOPE) - 1)).split(';');
        const int player = parts.value(0).mid(1).toInt();
        for (const QByteArray &p : parts)
            if (p.startsWith("PORT=") && player >= 1 && player <= 4) ports[player] = quint16(p.mid(5).toUInt());
    }
    return ports;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("of_test_sender"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Fake game: animated test images to the OpenFIRE Scope Daemon (UDP on 127.0.0.1)."));
    parser.addHelpOption();
    const QCommandLineOption playersOpt(QStringLiteral("players"), QStringLiteral("Players to send, e.g. 1,2 (default 1,2,3,4)."), QStringLiteral("LIST"), QStringLiteral("1,2,3,4"));
    const QCommandLineOption portsOpt(QStringLiteral("ports"), QStringLiteral("UDP ports for P1..P4, e.g. 47811,47812 (default: from the discovery, else 47811-47814)."), QStringLiteral("LIST"));
    const QCommandLineOption fpsOpt(QStringLiteral("fps"), QStringLiteral("Frames per second (default 60)."), QStringLiteral("N"), QStringLiteral("60"));
    const QCommandLineOption formatOpt(QStringLiteral("format"), QStringLiteral("rgb888, rgb565 or bgrx (default bgrx)."), QStringLiteral("F"), QStringLiteral("bgrx"));
    const QCommandLineOption durationOpt(QStringLiteral("duration"), QStringLiteral("Seconds, 0 = until Ctrl+C (default 0)."), QStringLiteral("S"), QStringLiteral("0"));
    const QCommandLineOption pauseEveryOpt(QStringLiteral("pause-every"), QStringLiteral("A pause every S seconds (presence packets every 200 ms), 0 = never."), QStringLiteral("S"), QStringLiteral("0"));
    const QCommandLineOption pauseForOpt(QStringLiteral("pause-for"), QStringLiteral("Length of each pause in seconds (default 2)."), QStringLiteral("S"), QStringLiteral("2"));
    const QCommandLineOption sessionOpt(QStringLiteral("session-every"), QStringLiteral("New session (game restart) every S seconds, 0 = never."), QStringLiteral("S"), QStringLiteral("0"));
    const QCommandLineOption discoveryOpt(QStringLiteral("discovery-port"), QStringLiteral("Discovery port (default 47810)."), QStringLiteral("N"), QString::number(OF_UDP_DISCOVERY_PORT));
    parser.addOptions({ playersOpt, portsOpt, fpsOpt, formatOpt, durationOpt, pauseEveryOpt, pauseForOpt, sessionOpt, discoveryOpt });
    parser.process(app);

    const QString fmtName = parser.value(formatOpt).toLower();
    const int format = fmtName == QLatin1String("rgb888") ? OF_UDP_FMT_RGB888
                     : fmtName == QLatin1String("rgb565") ? OF_UDP_FMT_RGB565
                     : fmtName == QLatin1String("bgrx")   ? OF_UDP_FMT_BGRX8888 : -1;
    const int fps = parser.value(fpsOpt).toInt();
    if (format < 0 || fps < 1 || fps > 240) {
        std::fprintf(stderr, "Invalid --format or --fps\n");
        return 2;
    }
    const double duration = parser.value(durationOpt).toDouble();
    const double pauseEvery = parser.value(pauseEveryOpt).toDouble();
    const double pauseFor = parser.value(pauseForOpt).toDouble();
    const double sessionEvery = parser.value(sessionOpt).toDouble();

    // EN: ports: --ports, else the discovery, else the defaults. / IT: porte: --ports, altrimenti la discovery, altrimenti le predefinite.
    QVector<quint16> ports(5, 0);
    if (parser.isSet(portsOpt)) {
        const QStringList list = parser.value(portsOpt).split(QLatin1Char(','));
        for (int i = 0; i < list.size() && i < 4; ++i) ports[i + 1] = quint16(list[i].toUInt());
    } else {
        QString answer;
        ports = discover(quint16(parser.value(discoveryOpt).toUInt()), &answer);
        if (answer.isEmpty()) std::printf("No answer from the discovery.\n");
        else std::printf("Discovery:\n%s\n", qPrintable(answer));
        bool missing = false;
        for (int n = 1; n <= 4; ++n) missing = missing || ports[n] == 0;
        if (missing)
            std::printf("No port from the discovery for a player without a paired scope: default port 4781x.\n"
                        "If the daemon uses other ports (see its window), give them with --ports.\n");
    }

    std::vector<Player> players;
    for (const QString &p : parser.value(playersOpt).split(QLatin1Char(','))) {
        const int n = p.trimmed().toInt();
        if (n < 1 || n > 4) continue;
        Player pl;
        pl.number = n;
        pl.port = ports[n] ? ports[n] : quint16(OF_UDP_VIDEO_PORT_P1 + n - 1);
        pl.session = newSession();
        players.push_back(pl);
        std::printf("P%d -> 127.0.0.1:%u\n", n, unsigned(pl.port));
    }
    if (players.empty()) {
        std::fprintf(stderr, "No player\n");
        return 2;
    }
    std::printf("%d fps, %s, Ctrl+C to stop\n", fps, qPrintable(fmtName));
    std::fflush(stdout);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef _WIN32
    timeBeginPeriod(1);   // EN: 1 ms sleeps / IT: attese da 1 ms
#endif

    // EN: non blocking socket, errors are only counted: like a game, it never waits for the daemon.
    // IT: socket non bloccante, gli errori si contano soltanto: come un gioco, non aspetta mai il demone.
    QUdpSocket socket;
    socket.bind(QHostAddress::LocalHost, 0);
    socket.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);

    const int bpp = format == OF_UDP_FMT_RGB888 ? 3 : format == OF_UDP_FMT_RGB565 ? 2 : 4;
    const Tables tables;
    std::vector<uint8_t> image(size_t(W) * H * 3);
    std::vector<uint8_t> packet(OF_UDP_HEADER + size_t(StripPixels) * bpp);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto period = std::chrono::nanoseconds(1000000000LL / fps);
    auto next = start;
    QElapsedTimer statClock;
    statClock.start();
    double lastSession = 0.0;
    bool wasPaused = false;

    while (!g_stop) {
        const double t = std::chrono::duration<double>(Clock::now() - start).count();
        if (duration > 0 && t >= duration) break;

        if (sessionEvery > 0 && t - lastSession >= sessionEvery) {
            // EN: a restarted game: new session and a lower frame number. / IT: gioco riavviato: sessione nuova e numero di frame piu' basso.
            for (Player &p : players) { p.session = newSession(); p.frame = QRandomGenerator::global()->bounded(100u); }
            std::printf("New session\n");
            lastSession = t;
        }
        const bool paused = pauseEvery > 0 && std::fmod(t, pauseEvery + pauseFor) >= pauseEvery;
        if (paused != wasPaused) {
            std::printf(paused ? "Pause\n" : "Play\n");
            wasPaused = paused;
        }
        const qint64 ms = qint64(t * 1000);

        for (Player &p : players) {
            const QHostAddress to(QHostAddress::LocalHost);
            if (paused) {
                if (ms - p.lastPresenceMs >= 200) {
                    p.lastPresenceMs = ms;
                    header(packet.data(), format, p.frame, OF_UDP_STRIP_PRESENCE, p.number, p.session);
                    if (socket.writeDatagram(reinterpret_cast<const char *>(packet.data()), OF_UDP_HEADER, to, p.port) < 0) ++p.errors;
                }
                continue;
            }
            ++p.frame;
            render(tables, image.data(), p.number, p.frame, t);
            for (int strip = 0; strip < OF_SCOPE_STRIPS; ++strip) {
                header(packet.data(), format, p.frame, strip, p.number, p.session);
                packStrip(image.data(), strip, format, packet.data() + OF_UDP_HEADER);
                if (socket.writeDatagram(reinterpret_cast<const char *>(packet.data()), qint64(packet.size()), to, p.port) < 0) ++p.errors;
            }
            ++p.sent;
        }

        if (statClock.elapsed() >= 1000) {
            const double dt = double(statClock.restart()) / 1000.0;
            QStringList parts;
            for (Player &p : players) {
                parts << QStringLiteral("P%1 %2 fps, %3 send errors").arg(p.number).arg(qRound(p.sent / dt)).arg(p.errors);
                p.sent = 0;
                p.errors = 0;
            }
            std::printf("%s\n", qPrintable(parts.join(QStringLiteral(" | "))));
            std::fflush(stdout);
        }

        next += period;
        const auto now = Clock::now();
        if (next < now) next = now;   // EN: late: no burst to catch up / IT: in ritardo: nessuna raffica per recuperare
        std::this_thread::sleep_until(next);
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return 0;
}
