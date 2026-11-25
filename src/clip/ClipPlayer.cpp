#include "clip/ClipPlayer.h"

#include <QFile>
#include <cstring>

#include "dongle/DongleManager.h"

namespace {
const char kMagic[8] = { 'O', 'F', 'C', 'L', 'I', 'P', '1', '\0' };
} // namespace

ClipPlayer::ClipPlayer(DongleManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ClipPlayer::tick);
}

bool ClipPlayer::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = tr("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray all = file.readAll();
    const auto *p = reinterpret_cast<const uint8_t *>(all.constData());
    const qsizetype size = all.size();
    if (size < 12 || std::memcmp(p, kMagic, 8) != 0) {
        *error = tr("%1 is not an OpenFIRE clip").arg(path);
        return false;
    }
    const int frames = p[8] | (p[9] << 8);
    const int fps = p[10] | (p[11] << 8);
    qsizetype pos = 12;
    QVector<EncodedFramePtr> list;
    for (int f = 0; f < frames; ++f) {
        auto frame = std::make_shared<EncodedFrame>();
        for (int s = 0; s < OF_SCOPE_STRIPS; ++s) {
            if (pos + 3 > size) {
                *error = tr("%1 is truncated").arg(path);
                return false;
            }
            const uint8_t q = p[pos];
            const int len = p[pos + 1] | (p[pos + 2] << 8);
            pos += 3;
            if (q < 1 || q > 100 || len < 1 || len > OF_VIDEO_MAX_DATA || pos + len > size) {
                *error = tr("%1: frame %2, slice %3 not valid").arg(path).arg(f).arg(s);
                return false;
            }
            frame->slices[s].q = q;
            frame->slices[s].data = all.mid(pos, len);
            pos += len;
        }
        list.append(frame);
    }
    if (list.isEmpty()) {
        *error = tr("%1 has no frames").arg(path);
        return false;
    }
    m_frames = list;
    m_fps = (fps >= 1 && fps <= 60) ? fps : 30;
    return true;
}

void ClipPlayer::start()
{
    if (m_frames.isEmpty()) return;
    m_next = 0;
    m_manager->setVideoSource(SourceKind::Clip);
    m_timer.start(qMax(1, qRound(1000.0 / m_fps)));
}

void ClipPlayer::stop()
{
    m_timer.stop();
    m_manager->setVideoSource(SourceKind::None);
}

void ClipPlayer::tick()
{
    m_manager->submitFrameToAll(m_frames[m_next]);
    m_next = (m_next + 1) % int(m_frames.size());
}
