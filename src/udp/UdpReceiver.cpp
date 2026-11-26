#include "udp/UdpReceiver.h"

#include <cstring>

#include "udp/TcpReceiver.h"

namespace {
constexpr int kStripPixels = OF_SCOPE_W * OF_SCOPE_STRIP_ROWS;   // 3,840
constexpr quint16 kFullMask = (1u << OF_SCOPE_STRIPS) - 1;         // 0x7FFF
constexpr int kRestartMs = 500;   // EN: no strip for so long -> any frame number is accepted / IT: nessuna fascia per tanto -> si accetta qualunque numero
constexpr int kBuffer = 65536;

quint32 le32(const uint8_t *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

int bytesPerPixel(int format)
{
    switch (format) {
    case OF_UDP_FMT_RGB888:   return 3;
    case OF_UDP_FMT_RGB565:   return 2;
    case OF_UDP_FMT_BGRX8888: return 4;
    default:                  return 0;
    }
}

void convertStrip(int format, const uint8_t *src, uint8_t *dst)
{
    switch (format) {
    case OF_UDP_FMT_RGB888:
        std::memcpy(dst, src, size_t(kStripPixels) * 3);
        break;
    case OF_UDP_FMT_RGB565:
        for (int i = 0; i < kStripPixels; ++i, src += 2, dst += 3) {
            const unsigned v = unsigned(src[0]) | (unsigned(src[1]) << 8);
            const unsigned r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            dst[0] = uint8_t((r << 3) | (r >> 2));
            dst[1] = uint8_t((g << 2) | (g >> 4));
            dst[2] = uint8_t((b << 3) | (b >> 2));
        }
        break;
    case OF_UDP_FMT_BGRX8888:
        for (int i = 0; i < kStripPixels; ++i, src += 4, dst += 3) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        }
        break;
    default:
        break;
    }
}
} // namespace

UdpReceiver::UdpReceiver(std::unique_ptr<NativeUdp> socket, SlotInput *slot, QThread::Priority priority, QObject *parent)
    : QThread(parent)
    , m_socket(std::move(socket))
    , m_slot(slot)
    , m_priority(priority)
    , m_port(m_socket->port())
    , m_bufferBytes(m_socket->receiveBufferBytes())
{
}

UdpReceiver::~UdpReceiver()
{
    requestStop();
    m_tcp.reset();
    wait();   // EN: at most 250 ms, the receive timeout / IT: al massimo 250 ms, il timeout di ricezione
}

void UdpReceiver::requestStop()
{
    m_stop.store(true);
    if (m_tcp) m_tcp->requestStop();
}

void UdpReceiver::attachTcp(std::unique_ptr<NativeTcp> listener)
{
    m_tcp = std::make_unique<TcpReceiver>(std::move(listener), this, m_slot, m_priority);
    m_tcp->start();
}

void UdpReceiver::feed(const uint8_t *p, int n, qint64 now)
{
    std::lock_guard<std::mutex> lock(m_handleMutex);
    handle(p, n, now);
}

void UdpReceiver::run()
{
    setPriority(m_priority);
    static thread_local uint8_t buf[kBuffer];
    while (!m_stop.load()) {
        QString detail;
        const int n = m_socket->receive(buf, kBuffer, &detail);
        if (n < 0) {
            emit failed(detail);
            break;
        }
        if (n > 0) feed(buf, n, monotonicMs());
    }
    m_socket->close();
}

void UdpReceiver::handle(const uint8_t *p, int n, qint64 now)
{
    // EN: checks on every packet; a packet that fails one is dropped and counted.
    // IT: controlli su ogni pacchetto; un pacchetto che ne fallisce uno si scarta e si conta.
    if (n < OF_UDP_HEADER || p[0] != 'O' || p[1] != 'F' || p[2] != OF_UDP_VERSION) {
        ++m_slot->packetsInvalid;
        return;
    }
    const int format = p[3];
    const quint32 frame = le32(p + 4);
    const int strip = p[8];
    const int strips = p[9];
    const int player = p[10];
    const quint32 session = le32(p + 11);
    const int bpp = bytesPerPixel(format);
    const bool presence = strip == OF_UDP_STRIP_PRESENCE;
    if (bpp == 0 || strips != OF_SCOPE_STRIPS || player != m_slot->player() || (!presence && strip >= OF_SCOPE_STRIPS)
        || n != OF_UDP_HEADER + (presence ? 0 : kStripPixels * bpp)) {
        ++m_slot->packetsInvalid;
        return;
    }
    if (presence) {
        m_slot->notePresence();
        return;
    }

    // EN: new session, or a pause of 500 ms: start again from this frame, whatever its number.
    // IT: sessione nuova, o pausa di 500 ms: si riparte da questo frame, qualunque sia il numero.
    const bool restart = !m_haveCurrent || session != m_session || now - m_lastStripMs > kRestartMs;
    m_lastStripMs = now;
    const quint16 bit = quint16(1u << strip);
    if (restart) {
        if (m_haveCurrent && !m_completed && m_mask) ++m_slot->framesIncomplete;
        m_haveCurrent = true;
        m_session = session;
        m_current = frame;
        m_mask = 0;
        m_completed = false;
    } else {
        const qint32 diff = qint32(frame - m_current);
        if (diff < 0) return;                       // EN: older frame / IT: frame piu' vecchio
        if (diff > 0) {                             // EN: newer: the incomplete one is dropped / IT: piu' nuovo: quello incompleto si scarta
            if (!m_completed && m_mask) ++m_slot->framesIncomplete;
            m_current = frame;
            m_mask = 0;
            m_completed = false;
        } else if (m_completed || (m_mask & bit)) {
            return;                                 // EN: duplicate / IT: duplicato
        }
    }

    if (!m_building) {
        m_building = m_slot->acquire();
        // EN: reserve all in use (never in practice): the strip is lost and this frame stays incomplete.
        // IT: riserva tutta in uso (in pratica mai): la fascia si perde e questo frame resta incompleto.
        if (!m_building) return;
    }
    convertStrip(format, p + OF_UDP_HEADER, m_building->rgb + size_t(strip) * OF_SCOPE_STRIP_ROWS * InputFrame::Stride);
    m_mask |= bit;
    if (m_mask == kFullMask) {
        m_completed = true;
        m_slot->publish(m_building, SourceKind::Udp);
        m_building.reset();
        m_building = m_slot->acquire();
    }
}
