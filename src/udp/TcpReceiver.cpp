#include "udp/TcpReceiver.h"

#include <cstring>

#include "app/Log.h"
#include "udp/SlotInput.h"
#include "udp/UdpReceiver.h"

namespace {
constexpr int kStripPixels = OF_SCOPE_W * OF_SCOPE_STRIP_ROWS;   // 3,840
constexpr int kMaxStripBytes = kStripPixels * 4;                  // EN: BGRX / IT: BGRX

int bytesPerPixel(int format)
{
    switch (format) {
    case OF_UDP_FMT_RGB888:   return 3;
    case OF_UDP_FMT_RGB565:   return 2;
    case OF_UDP_FMT_BGRX8888: return 4;
    default:                  return 0;
    }
}
} // namespace

TcpReceiver::TcpReceiver(std::unique_ptr<NativeTcp> listener, UdpReceiver *target, SlotInput *slot, QThread::Priority priority,
                         QObject *parent)
    : QThread(parent)
    , m_listener(std::move(listener))
    , m_target(target)
    , m_slot(slot)
    , m_priority(priority)
    , m_port(m_listener->port())
    , m_packets(new uint8_t[size_t(OF_SCOPE_STRIPS) * (OF_UDP_HEADER + kMaxStripBytes)])
{
}

TcpReceiver::~TcpReceiver()
{
    requestStop();
    wait();   // EN: at most 250 ms, the wait of the listener / IT: al massimo 250 ms, l'attesa dell'ascolto
}

void TcpReceiver::resetStream()
{
    m_headerHave = 0;
    m_stripBytes = 0;
    m_payloadHave = 0;
}

void TcpReceiver::run()
{
    setPriority(m_priority);
    quint32 serial = m_listener->clientSerial();
    while (!m_stop.load()) {
        uint8_t *dst;
        int want;
        if (m_stripBytes == 0) {
            dst = m_header + m_headerHave;
            want = OF_UDP_HEADER - m_headerHave;
        } else {
            const int strip = m_payloadHave / m_stripBytes;
            const int offset = m_payloadHave % m_stripBytes;
            dst = m_packets.get() + size_t(strip) * (OF_UDP_HEADER + m_stripBytes) + OF_UDP_HEADER + offset;
            want = m_stripBytes - offset;
        }
        QString detail;
        const int n = m_listener->receive(dst, want, &detail);
        if (m_listener->clientSerial() != serial) {   // EN: new connection: new stream / IT: connessione nuova: flusso nuovo
            serial = m_listener->clientSerial();
            resetStream();
        }
        if (n < 0) {
            Log::error(tr("P%1: TCP error (%2), video only over UDP until the port is opened again").arg(m_slot->player()).arg(detail));
            break;
        }
        if (n == 0) continue;

        const qint64 now = monotonicMs();
        if (m_stripBytes == 0) {
            m_headerHave += n;
            if (m_headerHave < OF_UDP_HEADER) continue;
            m_headerHave = 0;
            if (!headerDone(now)) {
                ++m_slot->packetsInvalid;
                m_listener->dropClient();
                resetStream();
            }
        } else {
            m_payloadHave += n;
            if (m_payloadHave < OF_SCOPE_STRIPS * m_stripBytes) continue;
            frameDone(now);
            resetStream();
        }
    }
    m_listener->close();
}

bool TcpReceiver::headerDone(qint64 now)
{
    // EN: only what is needed to know the length; the UDP receiver checks the rest of every strip.
    // IT: solo cio' che serve per sapere la lunghezza; il ricevitore UDP controlla il resto di ogni fascia.
    const uint8_t *p = m_header;
    if (p[0] != 'O' || p[1] != 'F' || p[2] != OF_UDP_VERSION) return false;
    const int bpp = bytesPerPixel(p[3]);
    if (bpp == 0) return false;
    if (p[8] == OF_UDP_STRIP_PRESENCE) {
        m_target->feed(p, OF_UDP_HEADER, now);
        return true;
    }
    m_stripBytes = kStripPixels * bpp;
    m_payloadHave = 0;
    return true;
}

void TcpReceiver::frameDone(qint64 now)
{
    for (int strip = 0; strip < OF_SCOPE_STRIPS; ++strip) {
        uint8_t *packet = m_packets.get() + size_t(strip) * (OF_UDP_HEADER + m_stripBytes);
        std::memcpy(packet, m_header, OF_UDP_HEADER);
        packet[8] = uint8_t(strip);
        m_target->feed(packet, OF_UDP_HEADER + m_stripBytes, now);
    }
    m_slot->tcpFrameMs.store(now);   // EN: SRC=TCP in the discovery / IT: SRC=TCP nel discovery
}
