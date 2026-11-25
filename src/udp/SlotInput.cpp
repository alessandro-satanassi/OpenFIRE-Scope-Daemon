#include "udp/SlotInput.h"

#include <QMutexLocker>
#include <chrono>

qint64 monotonicMs()
{
    using namespace std::chrono;
    return qint64(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

SlotInput::SlotInput(int player)
    : m_player(player)
{
    for (auto &f : m_reserve) f = std::make_shared<InputFrame>();
}

std::shared_ptr<InputFrame> SlotInput::acquire()
{
    QMutexLocker lock(&m_mutex);
    for (auto &f : m_reserve) {
        // EN: only the reserve holds it: nobody can take a new reference without this lock.
        // IT: lo tiene solo la riserva: nessuno puo' prendere un riferimento nuovo senza questo lock.
        if (f.use_count() == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);   // EN: the last reader has finished / IT: l'ultimo lettore ha finito
            return f;
        }
    }
    return nullptr;
}

void SlotInput::publish(const std::shared_ptr<InputFrame> &frame, SourceKind source)
{
    const qint64 now = monotonicMs();
    frame->source = source;
    frame->timeMs = now;
    {
        QMutexLocker lock(&m_mutex);
        // EN: under the lock, so that a capture frame published right after sees the UDP source.
        // IT: sotto il lock, cosi' un frame della cattura pubblicato subito dopo vede la sorgente UDP.
        if (source == SourceKind::Udp) m_lastEvent.store(now << 1);
        frame->seq = ++m_seq;
        m_latest = frame;
    }
    ++framesComplete;
}

void SlotInput::setCaptureAllowed(bool on)
{
    m_captureAllowed.store(on);
    if (!on) dropCaptureFrame();
}

void SlotInput::setCaptureLive(bool on)
{
    m_captureLive.store(on);
    if (!on) dropCaptureFrame();
}

void SlotInput::dropCaptureFrame()
{
    // EN: capture lost, monitor changed, capture off or scope no longer paired: the latest DXGI frame is old and
    //     must not leave when the capture comes back; the slot starts again from the first new crop.
    // IT: cattura persa, monitor cambiato, cattura spenta o mirino non piu' abbinato: l'ultimo frame DXGI e'
    //     vecchio e non deve partire quando la cattura torna; lo slot riparte dal primo ritaglio nuovo.
    QMutexLocker lock(&m_mutex);
    ++m_captureEpoch;
    if (m_latest && m_latest->source == SourceKind::Dxgi) m_latest.reset();
}

bool SlotInput::captureWanted(qint64 nowMs) const
{
    return m_captureAllowed.load() && udpSource(nowMs) == SourceKind::None;
}

bool SlotInput::publishCapture(const std::shared_ptr<InputFrame> &frame)
{
    const qint64 now = monotonicMs();
    frame->source = SourceKind::Dxgi;
    frame->timeMs = now;
    {
        QMutexLocker lock(&m_mutex);
        if (source(now) != SourceKind::Dxgi) return false;
        frame->seq = ++m_seq;
        m_latest = frame;
    }
    ++framesComplete;
    return true;
}

void SlotInput::notePresence()
{
    m_lastEvent.store((monotonicMs() << 1) | 1);
}

InputFramePtr SlotInput::latest() const
{
    QMutexLocker lock(&m_mutex);
    return m_latest;
}

SourceKind SlotInput::udpSource(qint64 nowMs) const
{
    // EN: the most recent valid event decides: a complete frame or a presence.
    // IT: decide l'evento valido piu' recente: un frame completo o una presenza.
    const qint64 event = m_lastEvent.load();
    if (event < 0 || nowMs - (event >> 1) >= SourceTimeoutMs) return SourceKind::None;
    return (event & 1) ? SourceKind::UdpPaused : SourceKind::Udp;
}

SourceKind SlotInput::source(qint64 nowMs) const
{
    const SourceKind udp = udpSource(nowMs);
    if (udp != SourceKind::None) return udp;
    return (m_captureAllowed.load() && m_captureLive.load()) ? SourceKind::Dxgi : SourceKind::None;
}
