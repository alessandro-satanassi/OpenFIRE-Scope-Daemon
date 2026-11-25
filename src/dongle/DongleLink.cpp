#include "dongle/DongleLink.h"

#include <QElapsedTimer>
#include <QMutexLocker>

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

    while (!m_stop.load()) {
        qint64 now = clock.elapsed();
        takeCommands();

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

        if (!m_serial.writeBusy()) pumpWrite();

        int timeout = kIdleWaitMs;
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
            if (m_writing == Writing::Video && m_lastSliceWriting) {
                ++m_framesSent;
                m_videoBytesSent += quint64(m_txBytes);
                m_qSum += quint64(m_txQSum);
                m_tx.clear();
                m_txIndex = 0;
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

void DongleLink::takeCommands()
{
    QList<QByteArray> texts;
    EncodedFramePtr frame;
    bool videoChanged = false;
    bool videoEnabled = false;
    {
        QMutexLocker lock(&m_cmdMutex);
        texts.swap(m_cmdTexts);
        frame.swap(m_cmdFrame);
        videoChanged = m_cmdVideoChanged;
        videoEnabled = m_cmdVideoEnabled;
        m_cmdVideoChanged = false;
    }
    for (const QByteArray &t : texts) m_control.append(UsbFrame::control(t));
    if (frame) m_pending = frame;
    if (videoChanged && videoEnabled != m_videoEnabled) {
        m_videoEnabled = videoEnabled;
        if (!m_videoEnabled) dropVideo();
    }
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
    m_pending.reset();
}

void DongleLink::writeFrame(const QByteArray &frame, Writing kind)
{
    if (m_serial.startWrite(reinterpret_cast<const uint8_t *>(frame.constData()), int(frame.size()))) m_writing = kind;
}

void DongleLink::pumpWrite()
{
    // EN: 1. control messages first, always between two whole frames. / IT: 1. prima i messaggi di controllo, sempre tra due trame intere.
    if (!m_control.isEmpty()) {
        writeFrame(m_control.takeFirst(), Writing::Control);
        return;
    }
    // EN: 2. bandwidth test, one frame at a time. / IT: 2. prova di banda, una trama alla volta.
    if (m_bwActive) {
        writeFrame(m_testFrame, Writing::Test);
        return;
    }
    if (!m_paired || !m_videoEnabled) return;
    // EN: 3. next slice of the frame in transmission. / IT: 3. fascia successiva del frame in trasmissione.
    if (m_txIndex < m_tx.size()) {
        m_lastSliceWriting = (m_txIndex == m_tx.size() - 1);
        writeFrame(m_tx[m_txIndex++], Writing::Video);
        return;
    }
    // EN: 4. a new frame goes into transmission: its number is written now. / IT: 4. un frame nuovo va in trasmissione: il suo numero si scrive ora.
    if (m_pending) {
        const EncodedFramePtr frame = m_pending;
        m_pending.reset();
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
        if (!m_tx.isEmpty()) {
            m_lastSliceWriting = (m_tx.size() == 1);
            writeFrame(m_tx[m_txIndex++], Writing::Video);
        }
    }
}

// ----------------------------------------------------------------- messages / messaggi
void DongleLink::handleContent(const uint8_t *content, int len, qint64 now)
{
    if (content[0] != OF_USB_TYPE_CONTROL || len < 2) return;
    const QByteArray text(reinterpret_cast<const char *>(content + 1), len - 1);

    if (text.startsWith(OF_MSG_STAT)) {
        const StatMessage s = StatMessage::parse(text);
        if (!s.valid) return;
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
                m_control.append(UsbFrame::control(OF_CMD_START));
            }
        } else {
            m_paired = false;
            dropVideo();
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
    emit bandwidthMeasured(kBps);
}
