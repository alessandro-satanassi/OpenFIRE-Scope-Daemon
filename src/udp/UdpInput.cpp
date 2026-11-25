#include "udp/UdpInput.h"

#include "app/Log.h"
#include "app/Settings.h"
#include "udp/NativeUdp.h"
#include "udp/UdpReceiver.h"

namespace {
constexpr quint16 kSparePortFirst = 47815;
constexpr quint16 kSparePortLast = 47830;
constexpr int kTickMs = 250;
} // namespace

UdpInput::UdpInput(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_slots.resize(Settings::Players);
    for (int i = 0; i < Settings::Players; ++i) m_slots[i].input = std::make_unique<SlotInput>(i + 1);
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &UdpInput::tick);
}

UdpInput::~UdpInput()
{
    stop();
}

void UdpInput::start()
{
    if (m_running) return;
    m_running = true;
    for (Slot &s : m_slots) open(s);
    m_lastRateMs = monotonicMs();
    m_timer.start();
    emit changed();
}

void UdpInput::stop()
{
    if (!m_running) return;
    m_running = false;
    m_timer.stop();
    // EN: every receiver stops together: at most one receive timeout (250 ms). / IT: tutti i ricevitori si fermano insieme: al massimo un timeout di ricezione (250 ms).
    for (Slot &s : m_slots)
        if (s.receiver) s.receiver->requestStop();
    for (Slot &s : m_slots) s.receiver.reset();
}

bool UdpInput::startReceiver(Slot &s, quint16 port, QString *detail, bool *inUse)
{
    auto socket = std::make_unique<NativeUdp>();
    const NativeUdp::BindResult r = socket->bind(port, detail);
    if (inUse) *inUse = r == NativeUdp::BindResult::InUse;
    if (r != NativeUdp::BindResult::Ok) return false;
    startReceiver(s, std::move(socket));
    return true;
}

void UdpInput::startReceiver(Slot &s, std::unique_ptr<NativeUdp> socket)
{
    if (s.receiver) {
        s.receiver->requestStop();
        s.receiver.reset();
    }
    const QThread::Priority prio = m_settings->udpPriority() == ThreadPriority::AboveNormal ? QThread::HighPriority
                                                                                          : QThread::NormalPriority;
    s.receiver = std::make_unique<UdpReceiver>(std::move(socket), s.input.get(), prio);
    const int player = s.input->player();
    connect(s.receiver.get(), &UdpReceiver::failed, this, [player](const QString &d) {
        Log::error(tr("P%1: UDP receive error (%2), the port is opened again").arg(player).arg(d));
    });
    s.receiver->start();
}

void UdpInput::open(Slot &s)
{
    const int player = s.input->player();
    s.preferred = m_settings->preferredVideoPort(player);
    s.portWarning.clear();
    QString detail;
    if (startReceiver(s, s.preferred, &detail, nullptr)) {
        Log::info(tr("P%1: video on UDP port %2").arg(player).arg(s.receiver->port()));
        return;
    }
    // EN: preferred port busy: 47815-47830, then one chosen by the system. / IT: porta preferita occupata: 47815-47830, poi una scelta dal sistema.
    const QString firstDetail = detail;
    for (quint16 port = kSparePortFirst; port <= kSparePortLast; ++port) {
        if (port == m_settings->discoveryPort()) continue;
        if (startReceiver(s, port, &detail, nullptr)) break;
    }
    if (!s.receiver) startReceiver(s, 0, &detail, nullptr);
    if (s.receiver) {
        Log::warn(tr("P%1: UDP port %2 not available (%3), video on port %4")
                      .arg(player).arg(s.preferred).arg(firstDetail).arg(s.receiver->port()));
    } else {
        Log::error(tr("P%1: no UDP port can be opened (%2)").arg(player).arg(detail));
    }
}

void UdpInput::releaseOld(const QVector<quint16> &wantedPorts)
{
    for (Slot &s : m_slots) s.changing = s.adopt = false;
    if (!m_running) return;
    for (Slot &s : m_slots) {
        const quint16 wanted = wantedPorts.value(s.input->player() - 1, s.preferred);
        if (wanted == s.preferred) continue;
        if (s.receiver && s.receiver->port() == wanted) {
            // EN: already there (back to the active port): nothing to bind; the preference changes only at commit.
            // IT: gia' li' (ritorno alla porta attiva): niente da legare; la preferenza cambia solo al commit.
            s.adopt = true;
            s.target = wanted;
            continue;
        }
        s.changing = true;
        s.target = wanted;
        s.oldPort = s.receiver ? s.receiver->port() : 0;
        if (s.receiver) s.receiver->requestStop();
    }
    for (Slot &s : m_slots)
        if (s.changing) s.receiver.reset();   // EN: they stop together / IT: si fermano insieme
}

bool UdpInput::bindNew(QString *failure)
{
    for (Slot &s : m_slots) {
        if (!s.changing) continue;
        const quint16 wanted = s.target;
        auto socket = std::make_unique<NativeUdp>();
        QString detail;
        if (socket->bind(wanted, &detail) != NativeUdp::BindResult::Ok) {
            if (failure) *failure = tr("P%1: UDP port %2 is not available (%3)").arg(s.input->player()).arg(wanted).arg(detail);
            return false;
        }
        s.pending = std::move(socket);
    }
    return true;
}

void UdpInput::commit()
{
    bool any = false;
    for (Slot &s : m_slots) {
        if (s.adopt) {
            s.adopt = false;
            s.preferred = s.target;
            s.portWarning.clear();
            any = true;
            continue;
        }
        if (!s.changing) continue;
        s.changing = false;
        s.preferred = s.target;
        s.portWarning.clear();
        startReceiver(s, std::move(s.pending));
        Log::info(tr("P%1: video on UDP port %2").arg(s.input->player()).arg(s.receiver->port()));
        any = true;
    }
    if (any) emit changed();
}

void UdpInput::dropNew()
{
    for (Slot &s : m_slots) s.pending.reset();
}

void UdpInput::restoreOld()
{
    bool any = false;
    for (Slot &s : m_slots) {
        s.adopt = false;   // EN: the preference stays the old one / IT: la preferenza resta la vecchia
        if (!s.changing) continue;
        s.changing = false;
        any = true;
        // EN: the applied port stays the old one: a later change of the settings tries the new one again.
        // IT: la porta applicata resta la vecchia: un cambio successivo delle impostazioni riprova la nuova.
        QString detail;
        if (s.oldPort && startReceiver(s, s.oldPort, &detail, nullptr)) continue;
        // EN: old port taken in the meantime by another program: spare ports, as at the start.
        // IT: porta vecchia presa nel frattempo da un altro programma: porte di riserva, come all'avvio.
        const quint16 applied = s.preferred;
        open(s);
        s.preferred = applied;
        if (s.receiver) {
            s.portWarning = tr("P%1: UDP port %2 not available (%3), video on port %4")
                                .arg(s.input->player()).arg(s.oldPort).arg(detail).arg(s.receiver->port());
        }
    }
    if (any) emit changed();
}

SlotInput *UdpInput::slot(int player) const
{
    if (player < 1 || player > int(m_slots.size())) return nullptr;
    return m_slots[player - 1].input.get();
}

InputFramePtr UdpInput::latestFrame(int player) const
{
    const SlotInput *in = slot(player);
    return in ? in->latest() : InputFramePtr();
}

void UdpInput::tick()
{
    const qint64 now = monotonicMs();
    bool any = false;
    for (Slot &s : m_slots) {
        // EN: a receiver ended by a socket error is opened again. / IT: un ricevitore finito per un errore del socket si riapre.
        if (s.receiver && s.receiver->isFinished()) {
            s.receiver.reset();
            open(s);
            any = true;
        }
        const SourceKind src = s.input->udpSource(now);
        if (src != s.source) {
            s.source = src;
            any = true;
        }
    }
    // EN: rates once a second. / IT: frequenze una volta al secondo.
    if (now - m_lastRateMs >= 1000) {
        const double dt = double(now - m_lastRateMs) / 1000.0;
        m_lastRateMs = now;
        for (Slot &s : m_slots) {
            const quint64 c = s.input->framesComplete.load();
            const quint64 inc = s.input->framesIncomplete.load();
            const quint64 inv = s.input->packetsInvalid.load();
            s.fpsIn = double(c - s.lastComplete) / dt;
            s.incompletePerSec = double(inc - s.lastIncomplete) / dt;
            s.invalidPerSec = double(inv - s.lastInvalid) / dt;
            s.lastComplete = c;
            s.lastIncomplete = inc;
            s.lastInvalid = inv;
        }
    }
    if (any) emit changed();
}

void UdpInput::fillSlots(QVector<SlotStatus> *slotList) const
{
    for (SlotStatus &st : *slotList) {
        if (st.player < 1 || st.player > int(m_slots.size())) continue;
        const Slot &s = m_slots[st.player - 1];
        st.udpPort = s.receiver ? s.receiver->port() : 0;
        st.source = s.source;
        st.fpsIn = s.source == SourceKind::None ? 0.0 : s.fpsIn;
        st.udpIncompletePerSec = s.incompletePerSec;
        st.udpInvalidPerSec = s.invalidPerSec;
    }
}

QStringList UdpInput::warnings() const
{
    QStringList out;
    int smallest = NativeUdp::WantedBufferBytes;
    for (const Slot &s : m_slots) {
        if (!s.portWarning.isEmpty()) out << s.portWarning;
        if (s.receiver && s.receiver->receiveBufferBytes() < smallest) smallest = s.receiver->receiveBufferBytes();
    }
    if (smallest < NativeUdp::WantedBufferBytes) {
        QString text = tr("UDP receive buffer of %1 KB instead of %2 KB: frames can lose strips")
                           .arg(smallest / 1024).arg(NativeUdp::WantedBufferBytes / 1024);
#ifdef Q_OS_LINUX
        text += QLatin1Char('\n')
              + tr("To raise it, run once in a terminal, then start the daemon again:\n%1")
                    .arg(QStringLiteral("echo net.core.rmem_max=%1 | sudo tee /etc/sysctl.d/60-openfire-scope.conf && sudo sysctl -w net.core.rmem_max=%1")
                             .arg(NativeUdp::WantedBufferBytes));
#endif
        out << text;
    }
    return out;
}
