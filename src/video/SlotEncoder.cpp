/*  OpenFIRE Scope Daemon: video from the PC to the OpenFIRE scopes.
    Copyright (C) 2026  Alessandro Satanassi, Team OpenFIRE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "video/SlotEncoder.h"

#include <QMutexLocker>
#include <memory>

#include "dongle/DongleLink.h"
#include "dongle/EncodedFrame.h"
#include "udp/SlotInput.h"
#include "video/HiResTimer.h"
#include "video/ScopeEncoder.h"

namespace {
constexpr int64_t kPollUs = 1000;          // EN: time reached, no new frame yet: look again after 1 ms / IT: tempo raggiunto, nessun frame nuovo: si riguarda dopo 1 ms
constexpr int64_t kIdleUs = 10000;         // EN: no source: the state is checked every 10 ms / IT: nessuna sorgente: lo stato si controlla ogni 10 ms
constexpr int64_t kStatsEveryUs = 1000000;

DongleLink::Source linkSource(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Udp:
    case SourceKind::Dxgi: return DongleLink::Source::Live;
    case SourceKind::UdpPaused: return DongleLink::Source::Paused;
    default: return DongleLink::Source::None;
    }
}
} // namespace

SlotEncoder::SlotEncoder(SlotInput *input, QObject *parent)
    : QThread(parent)
    , m_input(input)
{
}

SlotEncoder::~SlotEncoder()
{
    setLink(nullptr);
}

void SlotEncoder::setConfig(const EncoderConfig &config, QThread::Priority priority)
{
    QMutexLocker lock(&m_configMutex);
    m_config = config;
    m_priority = priority;
}

void SlotEncoder::setLink(DongleLink *link)
{
    if (link == m_link && (link == nullptr || isRunning())) return;
    if (isRunning()) {
        m_stop.store(true);
        wait();
    }
    m_link = link;
    m_encodeMs.store(0.0);
    m_targetFps.store(0.0);
    if (!m_link) return;
    m_stop.store(false);
    start();
}

void SlotEncoder::run()
{
    EncoderConfig config;
    {
        QMutexLocker lock(&m_configMutex);
        config = m_config;
        setPriority(m_priority);
    }
    DongleLink *const link = m_link;
    HiResTimer timer;
    ScopeEncoder encoder;
    RateControl rate(config);
    auto bufferA = std::make_unique<ScopeEncodeResult>();
    auto bufferB = std::make_unique<ScopeEncodeResult>();

    // EN: a new link starts from a new generation: nothing of the previous one can be sent again.
    // IT: un collegamento nuovo parte da una generazione nuova: nulla della precedente si puo' rimandare.
    static std::atomic<quint32> s_generation{ 1 };
    quint32 generation = s_generation.fetch_add(1);
    bool first = true;
    SourceKind state = SourceKind::None;
    quint64 lastSeq = 0;
    int64_t nextAt = 0;
    int64_t statsAt = HiResTimer::nowUs() + kStatsEveryUs;
    double statsMs = 0.0, statsFps = 0.0;
    int statsFrames = 0;

    while (!m_stop.load()) {
        int64_t now = HiResTimer::nowUs();
        const SourceKind kind = m_input->udpSource(monotonicMs());
        if (first || kind != state) {
            if (!first) generation = s_generation.fetch_add(1);
            first = false;
            state = kind;
            link->setSource(linkSource(state), generation);
        }

        if (now >= statsAt) {
            m_encodeMs.store(statsFrames ? statsMs / statsFrames : 0.0);
            m_targetFps.store(statsFrames ? statsFps / statsFrames : 0.0);
            statsMs = statsFps = 0.0;
            statsFrames = 0;
            statsAt = now + kStatsEveryUs;
        }

        if (linkSource(state) != DongleLink::Source::Live) {
            timer.sleepUntil(now + kIdleUs);
            continue;
        }
        if (now < nextAt) {
            timer.sleepUntil(nextAt);
            continue;
        }
        const InputFramePtr frame = m_input->latest();
        if (!frame || frame->seq == lastSeq) {
            timer.sleepUntil(now + kPollUs);
            continue;
        }

        // EN: the shared frame is only read. / IT: il frame condiviso si legge soltanto.
        const int64_t t0 = HiResTimer::nowUs();
        encoder.analyze(frame->rgb);
        const double fps = rate.chooseFps(encoder.motion(), monotonicMs());
        const int budget = rate.budget(fps, link->capacityKBs());
        const ScopeEncodeResult *r = rate.encode(encoder, budget, bufferA.get(), bufferB.get());
        rate.frameDone(*r, monotonicMs());
        lastSeq = frame->seq;
        nextAt = t0 + int64_t(1e6 / fps);

        // EN: the source may have changed during the encoding (pause, end of the game): the frame is dropped and
        //     the next round starts the new generation.
        // IT: la sorgente puo' essere cambiata durante la codifica (pausa, fine del gioco): il frame si scarta e il
        //     giro successivo avvia la nuova generazione.
        if (r->ok && m_input->udpSource(monotonicMs()) == state) {
            auto out = std::make_shared<EncodedFrame>();
            for (int i = 0; i < OF_SCOPE_STRIPS; ++i) {
                out->slices[i].q = r->sliceQ[i];
                out->slices[i].data = QByteArray(reinterpret_cast<const char *>(r->data[i]), r->sliceLen[i]);
            }
            out->generation = generation;
            out->fps = fps;
            link->submitFrame(out);
        }
        // EN: a slice over 1,400 bytes even at q = 1 drops the frame (it does not happen in practice).
        // IT: una fascia oltre 1.400 byte anche a q = 1 scarta il frame (in pratica non succede).

        statsMs += double(HiResTimer::nowUs() - t0) / 1000.0;
        statsFps += fps;
        ++statsFrames;
    }
}
