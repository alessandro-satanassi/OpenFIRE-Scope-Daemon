#include "dongle/DongleManager.h"

#include <QSerialPortInfo>
#include <QSet>
#include <algorithm>

#include "OpenFIRE_Scope_Protocol.h"
#include "app/Log.h"
#include "dongle/DongleLink.h"
#include "serial/NativeSerial.h"

namespace {
constexpr int kScanEveryMs = 1000;
constexpr int kNewPortWaitMs = 1000;    // EN: lets esptool open it first / IT: lascia che esptool la apra per primo
constexpr int kBusyRetryMs = 5000;
constexpr int kRescanTimeoutMs = 10000;
constexpr double kDefaultCapacityKBs = 450.0;

const int kChannels[OF_SCOPE_N_CHANNELS] = OF_SCOPE_CHANNELS;

#ifdef Q_OS_LINUX
// EN: udev rule for the dongles: access without the dialout group, and ModemManager kept away.
// IT: regola udev per i dongle: accesso senza il gruppo dialout, e ModemManager tenuto lontano.
QString udevHint(quint16 vid, quint16 pid)
{
    const QString v = QStringLiteral("%1").arg(vid, 4, 16, QLatin1Char('0'));
    const QString p = QStringLiteral("%1").arg(pid, 4, 16, QLatin1Char('0'));
    return QStringLiteral("printf '%s\\n' "
                          "'SUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\", ENV{ID_MM_DEVICE_IGNORE}=\"1\"' "
                          "'SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\", TAG+=\"uaccess\"' "
                          "| sudo tee /etc/udev/rules.d/70-openfire-scope.rules && sudo udevadm control --reload-rules && sudo udevadm trigger")
        .arg(v, p);
}
#endif
} // namespace

DongleManager::DongleManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_clock.start();
    m_scanTimer.setInterval(kScanEveryMs);
    connect(&m_scanTimer, &QTimer::timeout, this, &DongleManager::scan);
    m_rateTimer.setInterval(1000);
    connect(&m_rateTimer, &QTimer::timeout, this, &DongleManager::sampleRates);
}

DongleManager::~DongleManager()
{
    stop();
}

void DongleManager::start()
{
    if (m_running) return;
    m_running = true;
    m_scanTimer.start();
    m_rateTimer.start();
    scan();
}

void DongleManager::stop()
{
    if (!m_running) return;
    m_running = false;
    m_scanTimer.stop();
    m_rateTimer.stop();
    for (auto &kv : m_entries) closeLink(kv.second);
    m_entries.clear();
    m_channelWarnings.clear();
    for (QString &o : m_owner) o.clear();
    emit changed();
}

void DongleManager::setSuspended(bool suspended)
{
    if (m_suspended == suspended) return;
    m_suspended = suspended;
    if (suspended) {
        // EN: every port is closed; on Resume every port is new again (and gets START before the video).
        // IT: tutte le porte si chiudono; con Riprendi ogni porta torna nuova (e riceve START prima del video).
        for (auto &kv : m_entries) closeLink(kv.second);
        m_entries.clear();
        m_channelWarnings.clear();
        for (QString &o : m_owner) o.clear();
        emit changed();
    } else {
        scan();
    }
}

void DongleManager::retryPort(const QString &port)
{
    for (auto &kv : m_entries) {
        Entry &e = kv.second;
        if (e.port != port) continue;
        if (e.state == State::NoAnswer || e.state == State::Discarded || e.state == State::Busy
            || e.state == State::AccessDenied || e.state == State::Lost) {
            closeLink(e);
            e.state = State::Waiting;
            e.noAnswerCount = 0;
            e.retryAt = now();
            e.detail.clear();
            Log::info(tr("%1: tried again").arg(e.port));
        }
    }
    scan();
}

// ----------------------------------------------------------------- port list / elenco delle porte
void DongleManager::scan()
{
    if (!m_running || m_suspended) return;
    const quint16 vid = m_settings->dongleVid();
    const quint16 pid = m_settings->donglePid();
    const qint64 t = now();

    QSet<QString> present;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (!info.hasVendorIdentifier() || !info.hasProductIdentifier()) continue;
        if (info.vendorIdentifier() != vid || info.productIdentifier() != pid) continue;
#ifdef Q_OS_MACOS
        // EN: only /dev/cu.*, never /dev/tty.* (that one waits for the carrier). / IT: solo /dev/cu.*, mai /dev/tty.* (quella aspetta la portante).
        if (!info.systemLocation().startsWith(QLatin1String("/dev/cu."))) continue;
#endif
        const QString key = info.portName() + QLatin1Char('|') + QString::number(vid, 16) + QLatin1Char(':')
                            + QString::number(pid, 16) + QLatin1Char('|') + info.serialNumber();
        present.insert(key);
        if (m_entries.count(key)) continue;
        Entry e;
        e.key = key;
        e.port = info.portName();
#ifdef Q_OS_WIN
        e.location = info.portName();
#else
        e.location = info.systemLocation();
#endif
        e.serialNumber = info.serialNumber();
        e.state = State::Waiting;
        e.retryAt = t + kNewPortWaitMs;
        e.order = m_nextOrder++;
        Log::info(tr("%1: dongle port connected (serial number %2)").arg(e.port, e.serialNumber.isEmpty() ? tr("none") : e.serialNumber));
        m_entries.emplace(key, std::move(e));
    }

    bool any = false;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!present.contains(it->first)) {
            // EN: the port left the list: its state is forgotten. / IT: la porta e' uscita dall'elenco: il suo stato si dimentica.
            Entry &e = it->second;
            Log::info(tr("%1: disconnected").arg(e.port));
            releaseSlot(e);
            closeLink(e);
            it = m_entries.erase(it);
            any = true;
            continue;
        }
        Entry &e = it->second;
        const bool due = t >= e.retryAt;
        if (due && !e.link
            && (e.state == State::Waiting || e.state == State::NoAnswer || e.state == State::Busy
                || e.state == State::AccessDenied || e.state == State::Lost)) {
            openLink(e);
            any = true;
        }
        if (e.rescanPending && t - e.rescanAt > kRescanTimeoutMs) {
            e.rescanPending = false;   // EN: no answer in time: the channels are checked again / IT: nessuna risposta in tempo: i canali si ricontrollano
            any = true;
        }
        ++it;
    }
    if (any) {
        updateVideoEnabled();
        checkChannels();
        emit changed();
    }
}

void DongleManager::openLink(Entry &e)
{
    const QThread::Priority prio = m_settings->serialPriority() == ThreadPriority::AboveNormal ? QThread::HighPriority
                                                                                             : QThread::NormalPriority;
    e.link = std::make_unique<DongleLink>(e.location, prio);
    e.linkId = m_nextLinkId++;
    // EN: Busy and AccessDenied (and their reason) stay until the port really opens: no repeated warning.
    // IT: Busy e AccessDenied (e il motivo) restano finche' la porta non si apre davvero: nessun avviso ripetuto.
    if (e.state != State::Busy && e.state != State::AccessDenied) {
        e.state = State::Probing;
        e.detail.clear();
    }
    e.player = 0;
    e.channel = 0;
    e.capacityKBs = 0.0;
    e.haveStat = false;
    e.rescanPending = false;
    e.lastFrames = e.lastBytes = e.lastQSum = 0;
    e.fpsOut = e.kbPerFrame = e.q = 0.0;
    e.dropPerSec = e.qfullPerSec = 0.0;

    const quint64 id = e.linkId;
    DongleLink *link = e.link.get();
    connect(link, &DongleLink::opened, this, [this, id](int r, const QString &d) { onOpened(id, r, d); });
    connect(link, &DongleLink::noAnswer, this, [this, id]() { onNoAnswer(id); });
    connect(link, &DongleLink::scopeState, this,
            [this, id](int k, int p, const QString &fw, int ch) { onScopeState(id, k, p, fw, ch); });
    connect(link, &DongleLink::stat, this,
            [this, id](quint32 rx, quint32 tx, quint32 d, quint32 q) { onStat(id, rx, tx, d, q); });
    connect(link, &DongleLink::bandwidthMeasured, this, [this, id](double k) { onBandwidth(id, k); });
    connect(link, &DongleLink::lost, this, [this, id](const QString &r) { onLost(id, r); });
    link->start();
}

void DongleManager::closeLink(Entry &e)
{
    if (!e.link) return;
    e.link->disconnect(this);
    e.link.reset();   // EN: stops and waits for the thread (the frame being written ends) / IT: ferma e aspetta il thread (la trama in scrittura finisce)
    e.linkId = 0;
}

DongleManager::Entry *DongleManager::findByLink(quint64 linkId)
{
    if (linkId == 0) return nullptr;
    for (auto &kv : m_entries)
        if (kv.second.linkId == linkId) return &kv.second;
    return nullptr;
}

// ----------------------------------------------------------------- events of the links / eventi dei collegamenti
void DongleManager::onOpened(quint64 linkId, int result, const QString &detail)
{
    Entry *e = findByLink(linkId);
    if (!e) return;
    const auto r = NativeSerial::OpenResult(result);
    if (r == NativeSerial::OpenResult::Ok) {
        e->state = State::Probing;
        e->detail.clear();
        Log::info(tr("%1: opened, WHOAMI").arg(e->port));
        if (!detail.isEmpty()) Log::warn(tr("%1: RTS/DTR could not be turned off (%2)").arg(e->port, detail));
        emit changed();
        return;
    }
    closeLink(*e);
    e->detail = detail;
    e->retryAt = now() + kBusyRetryMs;
    switch (r) {
    case NativeSerial::OpenResult::Busy:
        if (e->state != State::Busy) Log::warn(tr("%1: busy, opened by another program").arg(e->port));
        e->state = State::Busy;
        break;
    case NativeSerial::OpenResult::AccessDenied:
        if (e->state != State::AccessDenied) Log::warn(tr("%1: access denied (%2)").arg(e->port, detail));
        e->state = State::AccessDenied;
        break;
    default:
        Log::warn(tr("%1: cannot be opened (%2)").arg(e->port, detail));
        e->state = State::Lost;
        break;
    }
    emit changed();
}

void DongleManager::onNoAnswer(quint64 linkId)
{
    Entry *e = findByLink(linkId);
    if (!e) return;
    closeLink(*e);
    ++e->noAnswerCount;
    // EN: no answer: tried again after 5 s and after 10 s, then discarded until unplugged (or "Retry").
    // IT: nessuna risposta: riprovata dopo 5 s e dopo 10 s, poi scartata finche' non si scollega (o "Riprova").
    if (e->noAnswerCount >= 3) {
        e->state = State::Discarded;
        Log::warn(tr("%1: no answer to WHOAMI, not a scope dongle").arg(e->port));
    } else {
        e->state = State::NoAnswer;
        e->retryAt = now() + (e->noAnswerCount == 1 ? 5000 : 10000);
        Log::info(tr("%1: no answer to WHOAMI, trying again in %2 s").arg(e->port).arg(e->noAnswerCount == 1 ? 5 : 10));
    }
    emit changed();
}

void DongleManager::onScopeState(quint64 linkId, int kind, int player, const QString &firmware, int channel)
{
    Entry *e = findByLink(linkId);
    if (!e) return;
    e->noAnswerCount = 0;
    if (!firmware.isEmpty()) e->firmware = firmware;
    const int oldChannel = e->channel;
    if (channel) e->channel = channel;
    if (kind != DongleLink::ScopeBoot && e->rescanPending) e->rescanPending = false;

    const State old = e->state;
    if (kind == DongleLink::ScopePaired) {
        // EN: same player again: no new slot and no repeated warning; a new channel is checked.
        // IT: stesso giocatore di nuovo: nessuno slot nuovo e nessun avviso ripetuto; un canale nuovo si controlla.
        if ((e->state == State::Paired || e->state == State::Duplicate) && e->player == player) {
            if (e->channel != oldChannel) {
                checkChannels();
                emit changed();
            }
            return;
        }
        releaseSlot(*e);
        e->player = player;
        assignSlot(*e);
    } else {
        releaseSlot(*e);
        e->player = 0;
        e->state = kind == DongleLink::ScopeBoot ? State::Boot : State::Free;
        if (old != e->state) {
            if (e->state == State::Boot) Log::info(tr("%1: dongle starting (channel search)").arg(e->port));
            else Log::info(tr("%1: dongle free, channel %2, firmware %3").arg(e->port).arg(e->channel).arg(e->firmware));
        }
    }
    updateVideoEnabled();
    checkChannels();
    emit changed();
}

void DongleManager::onStat(quint64 linkId, quint32, quint32, quint32 drop, quint32 qfull)
{
    Entry *e = findByLink(linkId);
    if (!e) return;
    // EN: counters from power on: only differences between two messages. / IT: contatori dall'accensione: solo differenze tra due messaggi.
    const qint64 t = now();
    if (e->haveStat && t > e->lastStatMs) {
        const double dt = double(t - e->lastStatMs) / 1000.0;
        e->dropPerSec = double(quint32(drop - e->lastDrop)) / dt;
        e->qfullPerSec = double(quint32(qfull - e->lastQfull)) / dt;
    }
    e->haveStat = true;
    e->lastDrop = drop;
    e->lastQfull = qfull;
    e->lastStatMs = t;
}

void DongleManager::onBandwidth(quint64 linkId, double kBps)
{
    Entry *e = findByLink(linkId);
    if (!e) return;
    if (kBps > 0.0) {
        e->capacityKBs = kBps;
        Log::info(tr("%1: USB bandwidth %2 KB/s").arg(e->port).arg(qRound(kBps)));
    } else {
        e->capacityKBs = kDefaultCapacityKBs;
        Log::info(tr("%1: bandwidth test stopped, %2 KB/s used").arg(e->port).arg(qRound(kDefaultCapacityKBs)));
    }
    emit changed();
}

void DongleManager::onLost(quint64 linkId, const QString &reason)
{
    Entry *e = findByLink(linkId);
    if (!e) return;
    Log::warn(tr("%1: lost (%2)").arg(e->port, reason));
    releaseSlot(*e);
    closeLink(*e);
    // EN: treated as a new port if it is still in the list. / IT: trattata come nuova se e' ancora nell'elenco.
    e->state = State::Lost;
    e->detail = reason;
    e->retryAt = now() + kNewPortWaitMs;
    updateVideoEnabled();
    emit changed();
}

// ----------------------------------------------------------------- slots / slot
DongleManager::Entry *DongleManager::slotOwner(int player)
{
    if (player < 1 || player > Settings::Players || m_owner[player - 1].isEmpty()) return nullptr;
    auto it = m_entries.find(m_owner[player - 1]);
    return it == m_entries.end() ? nullptr : &it->second;
}

const DongleManager::Entry *DongleManager::slotOwner(int player) const
{
    if (player < 1 || player > Settings::Players || m_owner[player - 1].isEmpty()) return nullptr;
    auto it = m_entries.find(m_owner[player - 1]);
    return it == m_entries.end() ? nullptr : &it->second;
}

void DongleManager::assignSlot(Entry &e)
{
    const Entry *owner = slotOwner(e.player);
    if (!owner) {
        m_owner[e.player - 1] = e.key;
        e.state = State::Paired;
        Log::info(tr("%1: scope P%2 paired, channel %3, firmware %4").arg(e.port).arg(e.player).arg(e.channel).arg(e.firmware));
    } else {
        // EN: the slot stays with the first dongle; this one keeps its port without video.
        // IT: lo slot resta al primo dongle; questo tiene la porta senza video.
        e.state = State::Duplicate;
        Log::warn(tr("%1: P%2 is already used by %3").arg(e.port).arg(e.player).arg(owner->port));
    }
}

void DongleManager::releaseSlot(Entry &e)
{
    if (e.player < 1 || e.player > Settings::Players) return;
    if (m_owner[e.player - 1] != e.key) return;
    m_owner[e.player - 1].clear();
    // EN: a duplicate of the same player takes the free slot (the oldest one). / IT: un doppione dello stesso giocatore prende lo slot libero (il piu' vecchio).
    Entry *next = nullptr;
    for (auto &kv : m_entries) {
        Entry &o = kv.second;
        if (o.state == State::Duplicate && o.player == e.player && (!next || o.order < next->order)) next = &o;
    }
    if (next) {
        m_owner[e.player - 1] = next->key;
        next->state = State::Paired;
        Log::info(tr("%1: scope P%2 takes the slot").arg(next->port).arg(next->player));
    }
}

void DongleManager::updateVideoEnabled()
{
    for (auto &kv : m_entries) {
        Entry &e = kv.second;
        if (!e.link) continue;
        e.link->setVideoEnabled(e.state == State::Paired && m_source != SourceKind::None);
    }
}

void DongleManager::submitFrameToAll(const EncodedFramePtr &frame)
{
    for (int p = 1; p <= Settings::Players; ++p) {
        Entry *e = slotOwner(p);
        if (e && e->link && e->state == State::Paired) e->link->submitFrame(frame);
    }
}

void DongleManager::setVideoSource(SourceKind source)
{
    if (m_source == source) return;
    m_source = source;
    updateVideoEnabled();
    emit changed();
}

// ----------------------------------------------------------------- channels / canali
void DongleManager::checkChannels()
{
    // EN: two dongles on the same channel: RESCAN;SKIP=<channels of all the other dongles> to the newer of the
    //     two that is still free, one request at a time, only if at least one channel stays not skipped.
    //     Never to a paired dongle: if both are paired, only a warning.
    // IT: due dongle sullo stesso canale: RESCAN;SKIP=<canali di tutti gli altri dongle> al piu' recente dei
    //     due che e' ancora libero, una richiesta alla volta, solo se resta almeno un canale non escluso.
    //     Mai a un dongle abbinato: se sono abbinati entrambi, solo un avviso.
    m_channelWarnings.clear();
    // EN: one RESCAN at a time among all the dongles: the next one is decided after the answer (or the timeout).
    // IT: un solo RESCAN alla volta fra tutti i dongle: il successivo si decide dopo la risposta (o la scadenza).
    bool rescanBusy = false;
    for (const auto &kv : m_entries)
        if (kv.second.link && kv.second.rescanPending) rescanBusy = true;
    QVector<Entry *> withChannel;
    for (auto &kv : m_entries) {
        Entry &e = kv.second;
        if (e.link && e.channel > 0 && (e.state == State::Free || e.state == State::Paired || e.state == State::Duplicate))
            withChannel.append(&e);
    }
    for (int i = 0; i < withChannel.size(); ++i) {
        for (int j = i + 1; j < withChannel.size(); ++j) {
            Entry *a = withChannel[i];
            Entry *b = withChannel[j];
            if (a->channel != b->channel) continue;
            Entry *target = nullptr;
            Entry *newer = a->order > b->order ? a : b;
            Entry *older = newer == a ? b : a;
            if (newer->state == State::Free) target = newer;
            else if (older->state == State::Free) target = older;
            if (!target) {
                m_channelWarnings << tr("%1 and %2 are on the same channel %3").arg(a->port, b->port).arg(a->channel);
                continue;
            }
            if (rescanBusy) continue;
            QSet<int> skip;
            for (const Entry *o : withChannel)
                if (o != target) skip.insert(o->channel);
            int left = 0;
            for (int ch : kChannels)
                if (!skip.contains(ch)) ++left;
            if (left == 0) {
                m_channelWarnings << tr("%1 and %2 are on the same channel %3, no free channel left").arg(a->port, b->port).arg(a->channel);
                continue;
            }
            QList<int> sorted(skip.begin(), skip.end());
            std::sort(sorted.begin(), sorted.end());
            QStringList list;
            for (int ch : sorted) list << QString::number(ch);
            const QByteArray skipList = list.join(QLatin1Char(',')).toLatin1();
            target->link->requestRescan(skipList);
            target->rescanPending = true;
            target->rescanAt = now();
            rescanBusy = true;
            Log::info(tr("%1: same channel as another dongle, RESCAN;SKIP=%2").arg(target->port, QString::fromLatin1(skipList)));
        }
    }
}

// ----------------------------------------------------------------- statistics / statistiche
void DongleManager::sampleRates()
{
    for (auto &kv : m_entries) {
        Entry &e = kv.second;
        if (!e.link) continue;
        const quint64 frames = e.link->framesSent();
        const quint64 bytes = e.link->videoBytesSent();
        const quint64 qsum = e.link->qSum();
        const quint64 df = frames - e.lastFrames;
        e.fpsOut = double(df);
        e.kbPerFrame = df ? double(bytes - e.lastBytes) / double(df) / 1000.0 : 0.0;
        e.q = df ? double(qsum - e.lastQSum) / double(df) : 0.0;
        e.lastFrames = frames;
        e.lastBytes = bytes;
        e.lastQSum = qsum;
    }
    emit changed();
}

// ----------------------------------------------------------------- state for the window / stato per la finestra
void DongleManager::fillSlots(QVector<SlotStatus> *slotList) const
{
    for (SlotStatus &s : *slotList) {
        const int player = s.player;
        s.scope = ScopeState::NoDongle;
        s.port.clear();
        s.channel = 0;
        s.firmware.clear();
        s.fpsOut = s.kbPerFrame = s.q = s.capacityKBs = s.dropPerSec = s.qfullPerSec = 0.0;
        const Entry *e = slotOwner(player);
        if (!e) continue;
        bool duplicate = false;
        for (const auto &kv : m_entries)
            if (kv.second.state == State::Duplicate && kv.second.player == player) duplicate = true;
        s.scope = duplicate ? ScopeState::Duplicate : ScopeState::Paired;
        s.port = e->port;
        s.channel = e->channel;
        s.firmware = e->firmware;
        s.fpsOut = e->fpsOut;
        s.kbPerFrame = e->kbPerFrame;
        s.q = e->q;
        s.capacityKBs = e->capacityKBs > 0.0 ? e->capacityKBs : kDefaultCapacityKBs;
        s.dropPerSec = e->dropPerSec;
        s.qfullPerSec = e->qfullPerSec;
        if (m_source != SourceKind::None) s.source = m_source;
    }
}

QVector<DeviceStatus> DongleManager::devices() const
{
    QVector<const Entry *> list;
    for (const auto &kv : m_entries)
        if (kv.second.state != State::Paired) list.append(&kv.second);
    std::sort(list.begin(), list.end(), [](const Entry *a, const Entry *b) { return a->order < b->order; });

    QVector<DeviceStatus> out;
    for (const Entry *e : list) {
        DeviceStatus d;
        d.port = e->port;
        d.serialNumber = e->serialNumber;
        d.channel = e->channel;
        d.firmware = e->firmware;
        switch (e->state) {
        case State::Waiting:      d.state = PortState::Waiting; break;
        case State::Probing:      d.state = PortState::Probing; break;
        case State::Boot:         d.state = PortState::Boot; break;
        case State::Free:         d.state = PortState::Free; break;
        case State::Paired:       d.state = PortState::Free; break;
        case State::Duplicate:    d.state = PortState::Duplicate; break;
        case State::NoAnswer:     d.state = PortState::NoAnswer; break;
        case State::Discarded:    d.state = PortState::Discarded; break;
        case State::Busy:         d.state = PortState::Busy; break;
        case State::AccessDenied: d.state = PortState::AccessDenied; break;
        case State::Lost:         d.state = PortState::Lost; break;
        }
        if (e->state == State::Duplicate) {
            const Entry *owner = slotOwner(e->player);
            d.detail = tr("P%1 already used by %2").arg(e->player).arg(owner ? owner->port : QString());
        } else if (e->state == State::Free && e->capacityKBs > 0.0) {
            d.detail = tr("USB %1 KB/s").arg(qRound(e->capacityKBs));
        } else if (e->state == State::Discarded) {
            d.detail = tr("no answer to WHOAMI: not a scope dongle");
        } else {
            d.detail = e->detail;
        }
        out.append(d);
    }
    return out;
}

QStringList DongleManager::warnings() const
{
    QStringList out;
    for (int p = 1; p <= Settings::Players; ++p) {
        const Entry *owner = slotOwner(p);
        if (!owner) continue;
        for (const auto &kv : m_entries)
            if (kv.second.state == State::Duplicate && kv.second.player == p)
                out << tr("Two dongles with P%1: %2 is used, %3 is not").arg(p).arg(owner->port, kv.second.port);
    }
    out << m_channelWarnings;
    for (const auto &kv : m_entries) {
        const Entry &e = kv.second;
        if (e.state == State::Busy) out << tr("%1 is busy: another program has it open").arg(e.port);
        else if (e.state == State::AccessDenied) out << tr("%1: access denied").arg(e.port);
    }
#ifdef Q_OS_LINUX
    for (const auto &kv : m_entries) {
        if (kv.second.state == State::AccessDenied || kv.second.state == State::Busy) {
            out << tr("To fix it, run once in a terminal:\n%1").arg(udevHint(m_settings->dongleVid(), m_settings->donglePid()));
            break;
        }
    }
#endif
    return out;
}
