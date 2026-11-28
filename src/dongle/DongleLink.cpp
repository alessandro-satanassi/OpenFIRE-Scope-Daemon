#include "dongle/DongleLink.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QtMath>

#include "OpenFIRE_Scope_Protocol.h"
#include "dongle/UsbFrame.h"

namespace {
constexpr int kWhoamiEveryMs = 200;
constexpr int kWhoamiTries = 3;
constexpr int kBootWhoamiEveryMs = 1000;   // EN: during BOOT the dongle does not announce itself / IT: durante BOOT il dongle non si annuncia da solo
constexpr int kTestContent = 1400;     // EN: 'T' + 1399 bytes / IT: 'T' + 1399 byte
constexpr int kTestDeadlineMs = 3000;
constexpr int kIdleWaitMs = 250;
constexpr int kReadBuffer = 8192;
constexpr double kCreditShare = 0.8;       // EN: credit grows by 0.8 * C per second / IT: il credito cresce di 0,8 * C al secondo
constexpr double kCreditCapBytes = 30000;  // EN: 2 frames of the 30 fps profile / IT: 2 frame del profilo a 30 fps
constexpr int kResendAfterMs = 100;
constexpr int kPauseEveryMs = 200;
constexpr double kMinCapacityKBs = 200.0;
constexpr int kDecreaseEveryMs = 500;      // EN: C goes down at most once every 500 ms / IT: C scende al massimo una volta ogni 500 ms
constexpr int kRaiseAfterMs = 5000;        // EN: 5 s without congestion, then +5% per second / IT: 5 s senza congestione, poi +5% al secondo
// EN: Above the value of the test: C goes on up by 10% per second while the USB is busy less than 60% of the time, then
//     by 3%, at most to 1000 KB/s (USB Full Speed), only while the frames fill their USB budget and the USB is busy
//     less than 85% of the time. A step of one second never jumps over the band 85-95%. Busy at least 95% of the time is
//     congestion, like DROP and QFULL. Between 85% and 95% C stays as it is (hysteresis). Every congestion above
//     the test doubles the wait before going up again (5 s, 10 s ... 60 s); 60 s without congestion bring it back to 5 s.
// IT: Sopra il valore della prova: C continua a salire del 10% al secondo finche' la USB e' occupata meno del 60% del
//     tempo, poi del 3%, al massimo fino a 1000 KB/s (USB Full Speed), solo mentre i frame riempiono il loro budget USB
//     e la USB e' occupata meno dell'85% del tempo. Un passo di un secondo non salta mai la fascia 85-95%. Occupata almeno
//     il 95% del tempo e' congestione, come DROP e QFULL. Tra 85% e 95% C resta com'e' (isteresi). Ogni congestione
//     sopra la prova raddoppia l'attesa prima di risalire (5 s, 10 s ... 60 s); 60 s senza congestione la riportano a 5 s.
constexpr double kMaxCapacityKBs = 1000.0;
constexpr double kProbeFastRate = 0.10;    // EN: per second / IT: al secondo
constexpr double kProbeRate = 0.03;        // EN: per second / IT: al secondo
constexpr double kProbeFastBusy = 0.60;
constexpr double kProbeBusyBelow = 0.85;
constexpr double kSaturatedBusy = 0.95;
constexpr double kFullFrame = 0.85;        // EN: share of the USB budget of its frame / IT: quota del budget USB del suo frame
constexpr int kBusyWindowMs = 1000;
constexpr int kBusyWindowFrames = 5;       // EN: fewer frames in the window: no judgement / IT: meno frame nella finestra: nessun giudizio
constexpr int kProbeWaitMaxMs = 60000;
constexpr int kProbeForgetMs = 60000;
} // namespace

DongleLink::DongleLink(const QString &location, QThread::Priority priority, QObject *parent)
    : QThread(parent)
    , m_location(location)
    , m_priority(priority)
{
    m_testFrame = UsbFrame::test(kTestContent);
}

DongleLink::~DongleLink()
{
    requestStop();
    wait();
}

EncodedFramePtr DongleLink::shownFrame() const
{
    QMutexLocker lock(&m_shownMutex);
    return m_shown;
}

void DongleLink::clearShown()
{
    // EN: another player or no pairing: the preview must not show the frame of the previous one.
    // IT: un altro giocatore o nessun abbinamento: l'anteprima non deve mostrare il frame del precedente.
    QMutexLocker lock(&m_shownMutex);
    m_shown.reset();
}

void DongleLink::requestStop()
{
    m_stop.store(true);
    m_serial.wake();
}

void DongleLink::requestRescan(const QByteArray &skipList)
{
    {
        QMutexLocker lock(&m_cmdMutex);
        m_cmdTexts.append(QByteArray(OF_CMD_RESCAN) + ";SKIP=" + skipList);
    }
    m_serial.wake();
}

void DongleLink::setVideoEnabled(bool on)
{
    {
        QMutexLocker lock(&m_cmdMutex);
        m_cmdVideoEnabled = on;
        m_cmdVideoChanged = true;
    }
    m_serial.wake();
}

void DongleLink::setSource(Source source, quint32 generation, int player)
{
    {
        QMutexLocker lock(&m_cmdMutex);
        m_cmdSource = source;
        m_cmdGeneration = generation;
        m_cmdPlayer = player;
        m_cmdSourceChanged = true;
    }
    m_serial.wake();
}

void DongleLink::submitFrame(const EncodedFramePtr &frame)
{
    {
        QMutexLocker lock(&m_cmdMutex);
        m_cmdFrame = frame;   // EN: an older frame not yet taken is replaced / IT: un frame piu' vecchio non ancora preso si sostituisce
    }
    m_serial.wake();
}

// ----------------------------------------------------------------- thread / thread
void DongleLink::run()
{
    setPriority(m_priority);

    QString detail;
    const NativeSerial::OpenResult result = m_serial.open(m_location, &detail);
    emit opened(int(result), detail);
    if (result != NativeSerial::OpenResult::Ok) return;

    QElapsedTimer clock;
    clock.start();
    UsbFrame::Parser parser;
    static thread_local uint8_t readBuf[kReadBuffer];

    bool answered = false;
    int whoamiSent = 0;
    qint64 nextWhoami = 0;
    m_credit = kCreditCapBytes;   // EN: the first frame leaves at once / IT: il primo frame parte subito

    while (!m_stop.load()) {
        qint64 now = clock.elapsed();
        takeCommands(now);
        updateCredit(now);

        // EN: probing: WHOAMI every 200 ms, 3 times; no answer 200 ms after the third -> "no answer".
        // IT: prova: WHOAMI ogni 200 ms, 3 volte; nessuna risposta 200 ms dopo la terza -> "nessuna risposta".
        if (!answered && now >= nextWhoami) {
            if (whoamiSent >= kWhoamiTries) {
                emit noAnswer();
                break;
            }
            m_control.append(UsbFrame::control(OF_CMD_WHOAMI));
            ++whoamiSent;
            nextWhoami = now + kWhoamiEveryMs;
        }
        // EN: BOOT: WHOAMI every second until the dongle has its channel (the end of the search could be missed).
        // IT: BOOT: WHOAMI ogni secondo finche' il dongle non ha il canale (la fine della ricerca potrebbe sfuggire).
        if (answered && m_inBoot && now >= m_nextBootWhoami) {
            m_control.append(UsbFrame::control(OF_CMD_WHOAMI));
            m_nextBootWhoami = now + kBootWhoamiEveryMs;
        }
        if (m_bwActive && now >= m_bwDeadline) finishBandwidthTest(0.0);
        // EN: slot paused: PAUSE every 200 ms, between two whole frames; the first one waits for the last slice
        //     of the frame in transmission. / IT: slot in pausa: PAUSE ogni 200 ms, tra due trame intere; il primo
        //     aspetta l'ultima fascia del frame in trasmissione.
        // EN: not while the source of the new player is awaited. / IT: non mentre si aspetta la sorgente del nuovo giocatore.
        if (m_paired && m_videoEnabled && !m_waitNewSource && m_source == Source::Paused && now >= m_nextPauseMs && m_txIndex >= m_tx.size()) {
            m_control.append(UsbFrame::control(OF_CMD_PAUSE));
            m_nextPauseMs = now + kPauseEveryMs;
        }

        if (!m_serial.writeBusy()) pumpWrite(now);

        int timeout = kIdleWaitMs;
        if (!m_serial.writeBusy()) timeout = qMin(timeout, nextEventIn(now));
        if (!answered) timeout = int(qMax<qint64>(0, nextWhoami - now));
        if (answered && m_inBoot) timeout = int(qMin<qint64>(timeout, qMax<qint64>(0, m_nextBootWhoami - now)));
        if (m_bwActive) timeout = int(qMin<qint64>(timeout, qMax<qint64>(0, m_bwDeadline - now)));

        const NativeSerial::Events ev = m_serial.wait(timeout, readBuf, kReadBuffer);
        if (ev.error) {
            emit lost(ev.errorText);
            break;
        }
        now = clock.elapsed();
        if (ev.readCount > 0) {
            parser.feed(readBuf, ev.readCount, now, [&](const uint8_t *content, int len) {
                if (content[0] == OF_USB_TYPE_CONTROL) {
                    const QByteArray text(reinterpret_cast<const char *>(content + 1), len - 1);
                    // EN: only a valid state counts as an answer. / IT: vale come risposta solo uno stato valido.
                    if (text.startsWith(OF_MSG_SCOPE) && ScopeMessage::parse(text).kind != ScopeMessage::Invalid) answered = true;
                }
                handleContent(content, len, now);
            });
        } else {
            parser.idle(now);
        }
        if (ev.writeFinished) {
            if (m_writing != Writing::None) m_winBusyMs += double(now - m_writeStartMs);   // EN: USB busy / IT: USB occupata
            if (m_writing == Writing::Video) {
                // EN: congestion: one slice written in more than one frame time. / IT: congestione: una fascia scritta in piu' del tempo di un frame.
                const double fps = m_txFrame ? m_txFrame->fps : 30.0;
                if (double(now - m_writeStartMs) > 1000.0 / qMax(1.0, fps)) congestion(now);
            }
            if (m_writing == Writing::Video && m_lastSliceWriting) {
                ++m_framesSent;
                m_videoBytesSent += quint64(m_txBytes);
                // EN: frame that fills its USB budget (0.8 * C / fps, as the encoder computes it).
                // IT: frame che riempie il suo budget USB (0,8 * C / fps, come lo calcola l'encoder).
                ++m_winFrames;
                const double frameFps = m_txFrame ? qMax(1.0, m_txFrame->fps) : 30.0;
                if (double(m_txBytes) >= kFullFrame * kCreditShare * m_capacity.load() * 1000.0 / frameFps) ++m_winFull;
                m_qSum += quint64(m_txQSum);
                m_tx.clear();
                m_txIndex = 0;
                // EN: sent whole: it becomes the copy for the resend, only in the current generation.
                // IT: inviato intero: diventa la copia per il reinvio, solo nella generazione corrente.
                if (m_txFrame && m_txGeneration == m_generation) m_lastSent = m_txFrame;
                if (m_txFrame) {
                    QMutexLocker lock(&m_shownMutex);
                    m_shown = m_txFrame;
                }
                m_txFrame.reset();
            }
            m_writing = Writing::None;
            m_lastSliceWriting = false;
        }
    }

    // EN: the frame being written is never cut: wait for it (at most 500 ms) before closing.
    // IT: la trama in scrittura non si tronca mai: la si aspetta (al massimo 500 ms) prima di chiudere.
    QElapsedTimer drain;
    drain.start();
    while (m_serial.writeBusy() && drain.elapsed() < NativeSerial::WriteTimeoutMs) {
        const NativeSerial::Events ev = m_serial.wait(50, readBuf, kReadBuffer);
        if (ev.error) break;
    }
    m_serial.close();
}

void DongleLink::takeCommands(qint64 now)
{
    QList<QByteArray> texts;
    EncodedFramePtr frame;
    bool videoChanged = false;
    bool videoEnabled = false;
    bool sourceChanged = false;
    Source source = Source::None;
    quint32 generation = 0;
    int player = 0;
    {
        QMutexLocker lock(&m_cmdMutex);
        texts.swap(m_cmdTexts);
        frame.swap(m_cmdFrame);
        videoChanged = m_cmdVideoChanged;
        videoEnabled = m_cmdVideoEnabled;
        m_cmdVideoChanged = false;
        sourceChanged = m_cmdSourceChanged;
        source = m_cmdSource;
        generation = m_cmdGeneration;
        player = m_cmdPlayer;
        m_cmdSourceChanged = false;
    }
    for (const QByteArray &t : texts) m_control.append(UsbFrame::control(t));
    if (videoChanged && videoEnabled != m_videoEnabled) {
        m_videoEnabled = videoEnabled;
        if (!m_videoEnabled) {
            dropVideo();
            m_lastSent.reset();
        }
    }
    // EN: the source first, then the frame: a frame of an older generation is dropped.
    // IT: prima la sorgente, poi il frame: un frame di una generazione piu' vecchia si scarta.
    if (sourceChanged) {
        if (generation != m_generation) newGeneration(generation);
        // EN: after a pairing change only the source of the new owner of the slot opens the video: a new
        //     generation from its encoder, or 0 from the test clip.
        // IT: dopo un cambio di abbinamento solo la sorgente del nuovo proprietario dello slot riapre il video:
        //     una generazione nuova dal suo encoder, o 0 dalla clip di prova.
        // EN: the encoder of the player paired before can still make a new generation until the manager stops it.
        // IT: l'encoder del giocatore abbinato prima puo' ancora creare una generazione nuova finche' il gestore non lo ferma.
        if (m_waitNewSource && (generation == 0 || (generation != m_pairingGeneration && player == m_player))) m_waitNewSource = false;
        if (source == Source::Paused && m_source != Source::Paused) m_nextPauseMs = now;   // EN: the first PAUSE at once / IT: il primo PAUSE subito
        m_source = source;
    }
    if (frame && frame->generation == m_generation && !m_waitNewSource) m_pending = frame;
}

void DongleLink::newGeneration(quint32 generation)
{
    // EN: the frame in transmission ends whole; the one waiting and the copy for the resend belong to the old one.
    // IT: il frame in trasmissione finisce intero; quello in attesa e la copia per il reinvio sono della vecchia.
    m_generation = generation;
    m_pending.reset();
    m_lastSent.reset();
}

void DongleLink::dropVideo()
{
    // EN: the slice being written ends by itself; the others and the frame waiting are dropped.
    // IT: la fascia in scrittura finisce da sola; le altre e il frame in attesa si scartano.
    if (m_writing == Writing::Video) {
        while (m_tx.size() > m_txIndex) m_tx.removeLast();
        m_lastSliceWriting = false;
    } else {
        m_tx.clear();
        m_txIndex = 0;
    }
    m_txFrame.reset();
    m_pending.reset();
}

void DongleLink::writeFrame(const QByteArray &frame, Writing kind, qint64 now)
{
    if (m_serial.startWrite(reinterpret_cast<const uint8_t *>(frame.constData()), int(frame.size()))) {
        m_writing = kind;
        m_writeStartMs = now;
    }
}

void DongleLink::updateCredit(qint64 now)
{
    // EN: every second: how long the USB was busy, and how many frames filled their budget.
    // IT: ogni secondo: quanto e' stata occupata la USB, e quanti frame hanno riempito il loro budget.
    if (now - m_winStartMs >= kBusyWindowMs) {
        const double busy = m_winBusyMs / double(now - m_winStartMs);
        const bool video = m_winFrames >= kBusyWindowFrames;
        m_probeOk = video && busy < kProbeBusyBelow && 2 * m_winFull >= m_winFrames;
        m_probeFast = busy < kProbeFastBusy;
        // EN: USB full above the value of the test, or without the test: congestion. At or below a measured value
        //     nothing changes from before.
        // IT: USB piena sopra il valore della prova, o senza la prova: congestione. Al valore misurato o sotto non
        //     cambia niente da prima.
        // EN: Without the test the new value also becomes the ceiling, so that it is not climbed back to at once.
        // IT: Senza la prova il nuovo valore diventa anche il tetto, cosi' non ci si risale subito.
        if (video && busy >= kSaturatedBusy && (m_capacity.load() > m_capacityTop || !m_capacityMeasured)) {
            congestion(now);
            if (!m_capacityMeasured) m_capacityTop = qMin(m_capacityTop, m_capacity.load());
        }
        m_winStartMs = now;
        m_winBusyMs = 0.0;
        m_winFrames = 0;
        m_winFull = 0;
    }
    if (now - m_lastCongestionMs >= m_probeWaitMs + kProbeForgetMs) m_probeWaitMs = kRaiseAfterMs;

    const double c = m_capacity.load();
    const qint64 dt = now - m_creditMs;
    m_creditMs = now;
    if (dt > 0) m_credit = qMin(kCreditCapBytes, m_credit + kCreditShare * c * double(dt));   // EN: KB/s = bytes/ms / IT: KB/s = byte/ms

    // EN: 5 s without congestion: C goes up by 5% per second, up to the value of the test.
    // IT: 5 s senza congestione: C risale del 5% al secondo, fino al valore della prova.
    const qint64 step = qMin<qint64>(now - m_lastRaiseMs, 1000);
    if (now - m_lastCongestionMs >= kRaiseAfterMs && c < m_capacityTop) {
        if (step > 0) m_capacity.store(qMin(m_capacityTop, c * (1.0 + 0.05 * double(step) / 1000.0)));
    } else if (c >= m_capacityTop && c < kMaxCapacityKBs && m_probeOk && now - m_lastCongestionMs >= m_probeWaitMs) {
        // EN: above the test, see kMaxCapacityKBs. / IT: sopra la prova, vedi kMaxCapacityKBs.
        const double rate = m_probeFast ? kProbeFastRate : kProbeRate;
        if (step > 0) m_capacity.store(qMin(kMaxCapacityKBs, c * (1.0 + rate * double(step) / 1000.0)));
    }
    m_lastRaiseMs = now;
}

void DongleLink::congestion(qint64 now)
{
    m_lastCongestionMs = now;
    m_probeOk = false;
    if (now - m_lastDecreaseMs < kDecreaseEveryMs) return;
    m_lastDecreaseMs = now;
    // EN: hysteresis: a congestion above the test waits longer before the next try. / IT: isteresi: una congestione sopra la prova aspetta di piu' prima del prossimo tentativo.
    if (m_capacity.load() > m_capacityTop) m_probeWaitMs = qMin<qint64>(kProbeWaitMaxMs, 2 * m_probeWaitMs);
    // EN: floor: half of the ceiling, at most 200 KB/s. / IT: minimo: meta' del tetto, al massimo 200 KB/s.
    m_capacity.store(qMax(qMin(kMinCapacityKBs, 0.5 * m_capacityTop), m_capacity.load() * 0.8));
}

bool DongleLink::startFrame(const EncodedFramePtr &frame, qint64 now)
{
    // EN: it leaves only if the credit covers it, and consumes it. / IT: parte solo se il credito lo copre, e lo consuma.
    const int need = frame->usbBytes();
    if (m_credit < double(need)) return false;
    m_credit -= double(need);

    // EN: the frame number is written now: dropped frames leave no gaps. / IT: il numero di frame si scrive ora: i frame scartati non lasciano buchi.
    const uint16_t number = m_frameNumber++;
    m_tx.clear();
    m_txIndex = 0;
    m_txBytes = 0;
    m_txQSum = 0;
    int slices = 0;
    for (int i = 0; i < OF_SCOPE_STRIPS; ++i) {
        const EncodedSlice &s = frame->slices[i];
        if (s.data.isEmpty()) continue;
        QByteArray f = UsbFrame::video(number, uint8_t(i), s.q, uint8_t(m_player),
                                       reinterpret_cast<const uint8_t *>(s.data.constData()), int(s.data.size()));
        m_txBytes += int(f.size());
        m_txQSum += s.q;
        ++slices;
        m_tx.append(f);
    }
    if (slices > 0) m_txQSum /= slices;
    m_txFrame = frame;
    m_txGeneration = m_generation;
    m_lastFrameStartMs = now;
    if (!m_tx.isEmpty()) {
        m_lastSliceWriting = (m_tx.size() == 1);
        writeFrame(m_tx[m_txIndex++], Writing::Video, now);
    }
    return true;
}

void DongleLink::pumpWrite(qint64 now)
{
    // EN: 1. control messages first, always between two whole frames. / IT: 1. prima i messaggi di controllo, sempre tra due trame intere.
    if (!m_control.isEmpty()) {
        writeFrame(m_control.takeFirst(), Writing::Control, now);
        return;
    }
    // EN: 2. bandwidth test, one frame at a time. / IT: 2. prova di banda, una trama alla volta.
    if (m_bwActive) {
        writeFrame(m_testFrame, Writing::Test, now);
        return;
    }
    if (!m_paired || !m_videoEnabled) return;
    // EN: 3. next slice of the frame in transmission: it always ends whole. / IT: 3. fascia successiva del frame in trasmissione: finisce sempre intero.
    if (m_txIndex < m_tx.size()) {
        m_lastSliceWriting = (m_txIndex == m_tx.size() - 1);
        writeFrame(m_tx[m_txIndex++], Writing::Video, now);
        return;
    }
    if (m_source != Source::Live) return;
    // EN: 4. a new frame, when the credit covers it. While it waits, no resend can use the credit.
    // IT: 4. un frame nuovo, quando il credito lo copre. Mentre aspetta, nessun reinvio puo' usare il credito.
    if (m_pending) {
        if (startFrame(m_pending, now)) m_pending.reset();
        return;
    }
    // EN: 5. resend of the last frame sent, after 100 ms without new frames, with a new number.
    // IT: 5. reinvio dell'ultimo frame inviato, dopo 100 ms senza frame nuovi, con un numero nuovo.
    if (m_lastSent && now - m_lastFrameStartMs >= kResendAfterMs) {
        if (startFrame(m_lastSent, now)) ++m_resends;
    }
}

int DongleLink::nextEventIn(qint64 now) const
{
    int wait = kIdleWaitMs;
    auto creditIn = [&](const EncodedFramePtr &f) {
        const double missing = double(f->usbBytes()) - m_credit;
        const double rate = kCreditShare * m_capacity.load();
        if (missing <= 0.0 || rate <= 0.0) return 0;
        return int(qCeil(missing / rate));
    };
    // EN: as in run(): nothing to wait for while the source of the new player is awaited. / IT: come in run(): niente da attendere mentre si aspetta la sorgente del nuovo giocatore.
    if (m_paired && m_videoEnabled && !m_waitNewSource) {
        if (m_source == Source::Paused) wait = qMin<qint64>(wait, qMax<qint64>(0, m_nextPauseMs - now));
        if (m_source == Source::Live && m_txIndex >= m_tx.size()) {
            if (m_pending) wait = qMin(wait, creditIn(m_pending));
            else if (m_lastSent) wait = qMin(wait, qMax(int(qMax<qint64>(0, m_lastFrameStartMs + kResendAfterMs - now)), creditIn(m_lastSent)));
        }
    }
    return qMax(0, wait);
}

// ----------------------------------------------------------------- messages / messaggi
void DongleLink::handleContent(const uint8_t *content, int len, qint64 now)
{
    if (content[0] != OF_USB_TYPE_CONTROL || len < 2) return;
    const QByteArray text(reinterpret_cast<const char *>(content + 1), len - 1);

    if (text.startsWith(OF_MSG_STAT)) {
        const StatMessage s = StatMessage::parse(text);
        if (!s.valid) return;
        // EN: congestion: DROP or QFULL increased between two OF_STAT (not during the bandwidth test).
        // IT: congestione: DROP o QFULL aumentati tra due OF_STAT (non durante la prova di banda).
        if (m_haveStat && !m_bwActive && (s.drop != m_lastDrop || s.qfull != m_lastQfull)) congestion(now);
        m_haveStat = true;
        m_lastDrop = s.drop;
        m_lastQfull = s.qfull;
        if (m_bwActive) {
            // EN: the 1st OF_STAT is discarded (maybe computed before the start); C from the 2nd to the 4th.
            // IT: il 1° OF_STAT si scarta (forse calcolato prima dell'inizio); C dal 2° al 4°.
            ++m_bwStats;
            if (m_bwStats == 2) {
                m_bwRx2 = s.rx;
                m_bwT2 = now;
            } else if (m_bwStats == 4 && now > m_bwT2) {
                const double bytes = double(quint32(s.rx - m_bwRx2));
                finishBandwidthTest(bytes / double(now - m_bwT2));   // EN: bytes/ms = kB/s / IT: byte/ms = kB/s
            }
        }
        emit stat(s.rx, s.tx, s.drop, s.qfull);
        return;
    }

    if (text.startsWith(OF_MSG_SCOPE)) {
        const ScopeMessage m = ScopeMessage::parse(text);
        if (m.kind == ScopeMessage::Invalid) return;
        if (m.kind == ScopeMessage::Boot) {
            if (!m_inBoot) m_nextBootWhoami = now + kBootWhoamiEveryMs;
            m_inBoot = true;
        } else {
            m_inBoot = false;
        }
        if (m.kind == ScopeMessage::Paired) {
            const bool newPairing = !m_paired || m.player != m_player;
            m_paired = true;
            m_player = m.player;
            if (newPairing) {
                // EN: the bandwidth test stops at the frame boundary; START is a barrier before any new video.
                // IT: la prova di banda si ferma al confine della trama; START e' una barriera prima di ogni video nuovo.
                if (m_bwActive) finishBandwidthTest(0.0);
                dropVideo();
                m_control.removeAll(UsbFrame::control(OF_CMD_PAUSE));   // EN: PAUSE of the previous player still queued / IT: PAUSE del giocatore precedente ancora in coda
                m_lastSent.reset();   // EN: START starts a new session / IT: START apre una sessione nuova
                clearShown();
                // EN: a frame of the previous player still on its way must not leave as this one.
                // IT: un frame del giocatore precedente ancora in arrivo non deve partire come questo.
                m_waitNewSource = true;
                m_pairingGeneration = m_generation;
                m_control.append(UsbFrame::control(OF_CMD_START));
            }
        } else {
            m_paired = false;
            dropVideo();
            m_control.removeAll(UsbFrame::control(OF_CMD_PAUSE));   // EN: PAUSE of the previous player still queued / IT: PAUSE del giocatore precedente ancora in coda
            m_lastSent.reset();
            clearShown();
            m_waitNewSource = true;
            m_pairingGeneration = m_generation;
            if (m.kind == ScopeMessage::Unpaired && !m_bwDone) startBandwidthTest(now);
        }
        const int kind = m.kind == ScopeMessage::Boot ? ScopeBoot : m.kind == ScopeMessage::Unpaired ? ScopeUnpaired : ScopePaired;
        emit scopeState(kind, m.player, m.firmware, m.channel);
    }
}

void DongleLink::startBandwidthTest(qint64 now)
{
    m_bwDone = true;   // EN: once per opening of the port / IT: una volta per apertura della porta
    m_bwActive = true;
    m_bwStats = 0;
    m_bwDeadline = now + kTestDeadlineMs;
}

void DongleLink::finishBandwidthTest(double kBps)
{
    if (!m_bwActive) return;
    m_bwActive = false;   // EN: the frame being written ends by itself / IT: la trama in scrittura finisce da sola
    // EN: C starts from the measured value, or stays at DefaultCapacityKBs. / IT: C parte dal valore misurato, o resta a DefaultCapacityKBs.
    if (kBps > 0.0) {
        m_capacityMeasured = true;
        m_capacityTop = kBps;   // EN: also below 200: it is the measured ceiling / IT: anche sotto 200: e' il tetto misurato
        m_capacity.store(m_capacityTop);
    }
    emit bandwidthMeasured(kBps);
}
