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

#include "app/Daemon.h"

#include <QDir>
#include <QTimer>

#include "OpenFIRE_Scope_Protocol.h"
#include "app/Log.h"
#include "capture/AimInput.h"
#include "capture/ScreenCapture.h"
#include "clip/ClipPlayer.h"
#include "dongle/DongleManager.h"
#include "udp/Discovery.h"
#include "udp/UdpInput.h"
#include "video/SlotEncoder.h"

Daemon::Daemon(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_slots.resize(Settings::Players);
    for (int i = 0; i < Settings::Players; ++i) m_slots[i].player = i + 1;

    m_dongles = new DongleManager(m_settings, this);
    connect(m_dongles, &DongleManager::changed, this, &Daemon::statusChanged);
    m_udp = new UdpInput(m_settings, this);
    connect(m_udp, &UdpInput::changed, this, &Daemon::statusChanged);
    m_discovery = new Discovery(m_settings, [this]() { return discoveryAnswer(); }, this);
    connect(m_discovery, &Discovery::changed, this, &Daemon::statusChanged);
    m_discovery->setCommandHandler([this](const QByteArray &command) { handleCommand(command); });
    m_resumeTimer = new QTimer(this);
    m_resumeTimer->setSingleShot(true);
    connect(m_resumeTimer, &QTimer::timeout, this, [this]() {
        endCommandSuspend(tr("Resumed: the time asked for by the OpenFIRE App is over"));
    });

    // EN: one encoder per slot, running while the slot has a paired scope; with the test clip they stay off.
    // IT: un encoder per slot, attivo mentre lo slot ha un mirino abbinato; con la clip di prova restano spenti.
    for (int p = 1; p <= Settings::Players; ++p) m_encoders[p - 1] = new SlotEncoder(m_udp->slot(p), this);
    m_dongles->setSlotLinkHandler([this](int player, DongleLink *link) {
        if (player < 1 || player > Settings::Players) return;
        m_encoders[player - 1]->setLink(m_clip ? nullptr : link);
        // EN: the capture serves only slots with a paired scope, never with the test clip.
        // IT: la cattura serve solo slot con un mirino abbinato, mai con la clip di prova.
        m_udp->slot(player)->setCaptureAllowed(link && !m_clip);
    });

    // EN: screen capture and aim points (Windows only). / IT: cattura dello schermo e punti di mira (solo Windows).
    SlotInput *inputs[Settings::Players];
    for (int p = 1; p <= Settings::Players; ++p) inputs[p - 1] = m_udp->slot(p);
    m_aim = new AimInput(this);
    m_capture = new ScreenCapture(inputs, m_aim, this);
    m_captureTimer = new QTimer(this);
    m_captureTimer->setInterval(500);
    connect(m_captureTimer, &QTimer::timeout, this, &Daemon::checkCapture);
    connect(m_settings, &Settings::changed, this, &Daemon::applySettings);
}

bool Daemon::setClipFile(const QString &path)
{
    auto *clip = new ClipPlayer(m_dongles, this);
    QString error;
    if (!clip->load(path, &error)) {
        Log::error(tr("Test clip: %1").arg(error));
        delete clip;
        return false;
    }
    delete m_clip;
    m_clip = clip;
    Log::info(tr("Test clip %1: %2 frames at %3 fps, to every paired scope")
                  .arg(QDir::toNativeSeparators(path)).arg(m_clip->frameCount()).arg(m_clip->fps()));
    return true;
}

Daemon::~Daemon()
{
    stop();
}

void Daemon::start()
{
    if (m_running) return;
    m_running = true;
    applySettings();
    Log::info(tr("OpenFIRE Scope Daemon %1 started").arg(QStringLiteral(OFD_VERSION)));
    Log::info(tr("Settings: %1").arg(QDir::toNativeSeparators(m_settings->filePath())));
    if (m_settings->hasOverrides()) Log::info(tr("Command line values are used for this run only"));
    m_udp->start();
    m_discovery->start();
    m_dongles->start();
    if (m_clip) m_clip->start();
    if (ScreenCapture::supported()) {
        m_aim->start();
        m_capture->start();
        m_captureTimer->start();
    }
    emit statusChanged();
}

void Daemon::stop()
{
    if (!m_running) return;
    m_running = false;
    if (m_clip) m_clip->stop();
    m_captureTimer->stop();
    m_resumeTimer->stop();
    m_capture->stopCapture();
    m_aim->stopInput();
    m_dongles->stop();
    m_discovery->stop();
    m_udp->stop();
    Log::info(tr("Daemon stopped"));
}

void Daemon::applySettings()
{
    Log::instance().setFile(m_settings->logToFile() ? m_settings->logFilePath() : QString());
    // EN: the encoder constants are used from the next start of each encoder (next pairing).
    // IT: le costanti dell'encoder valgono dal prossimo avvio di ogni encoder (prossimo abbinamento).
    const ThreadPriority ep = m_settings->encoderPriority();
    const QThread::Priority encoderPriority = ep == ThreadPriority::AboveNormal ? QThread::HighPriority
                                            : ep == ThreadPriority::BelowNormal ? QThread::LowPriority
                                                                                 : QThread::NormalPriority;
    for (SlotEncoder *e : m_encoders) e->setConfig(m_settings->encoderConfig(), encoderPriority);
    m_capture->configure(m_settings->captureEnabled(), m_settings->captureMonitor(), m_settings->captureZoom());
    m_aim->setMap(m_settings->lightgunMap());
    // EN: normally nothing changes here: the dialog has already applied the ports before saving them.
    // IT: di norma qui non cambia nulla: la finestra ha gia' applicato le porte prima di salvarle.
    QVector<quint16> video;
    for (int p = 1; p <= Settings::Players; ++p) video << m_settings->preferredVideoPort(p);
    QString failure;
    if (applyPorts(video, m_settings->discoveryPort(), &failure)) {
        m_portWarning.clear();
    } else {
        m_portWarning = tr("The new ports were not applied: %1. The previous ports stay.").arg(failure);
        Log::warn(m_portWarning);
    }
    emit statusChanged();
}

bool Daemon::applyPorts(const QVector<quint16> &video, quint16 discovery, QString *failure)
{
    // EN: the ports change all together or not at all. Every port that changes (video and discovery) is
    //     released first, so ports can be swapped; then all the new ones are bound. If one is busy, the new ones
    //     are dropped and the old ones taken back.
    // IT: le porte cambiano tutte insieme o nessuna. Ogni porta che cambia (video e discovery) si rilascia prima,
    //     cosi' le porte si possono scambiare; poi si legano tutte le nuove. Se una e' occupata, le nuove si
    //     scartano e si riprendono le vecchie.
    if (!m_running) return true;
    m_udp->releaseOld(video);
    m_discovery->releaseOld(discovery);
    if (m_udp->bindNew(failure) && m_discovery->bindNew(failure)) {
        m_udp->commit();
        m_discovery->commit();
        return true;
    }
    m_udp->dropNew();
    m_discovery->dropNew();
    m_udp->restoreOld();
    m_discovery->restoreOld();
    return false;
}

bool Daemon::tryPorts(const QVector<quint16> &video, quint16 discovery, QString *message)
{
    QString failure;
    if (applyPorts(video, discovery, &failure)) {
        m_portWarning.clear();
        return true;
    }
    const QString text = tr("The new ports were not applied: %1. The previous ports stay.").arg(failure);
    Log::warn(text);
    if (message) *message = text;
    emit statusChanged();
    return false;
}

QVector<SlotStatus> Daemon::slotStatus() const
{
    QVector<SlotStatus> out = m_slots;
    m_udp->fillSlots(&out);
    for (SlotStatus &s : out) {
        if (s.player < 1 || s.player > Settings::Players) continue;
        s.encodeMs = m_encoders[s.player - 1]->encodeMs();
        s.aimMissing = s.source == SourceKind::Dxgi && m_capture->aimMissing(s.player);
    }
    m_dongles->fillSlots(&out);   // EN: after the UDP: the test clip wins as source / IT: dopo l'UDP: la clip di prova vince come sorgente
    return out;
}

QVector<DeviceStatus> Daemon::devices() const
{
    return m_dongles->devices();
}

QStringList Daemon::warnings() const
{
    QStringList out = m_warnings;
    if (!m_portWarning.isEmpty()) out << m_portWarning;
    out << m_dongles->warnings();
    out << m_discovery->warnings();
    out << m_udp->warnings();
    for (int p = 1; p <= Settings::Players; ++p)
        if (m_aim->duplicate(p)) out << tr("P%1: two lightguns with the same VID:PID, only the first one aims").arg(p);
    if (m_suspended) out.prepend(tr("Suspended: the serial ports are released"));
    return out;
}

IconState Daemon::iconState() const
{
    if (!warnings().isEmpty()) return IconState::Warning;
    bool paired = false;
    for (const SlotStatus &s : slotStatus())
        if (s.scope != ScopeState::NoDongle) paired = true;
    if (paired) return IconState::Active;
    return devices().isEmpty() ? IconState::Idle : IconState::Dongles;
}

QString Daemon::summary() const
{
    QStringList parts;
    const QVector<SlotStatus> slotList = slotStatus();
    for (const SlotStatus &s : slotList) {
        if (s.scope == ScopeState::NoDongle) continue;
        QString src;
        switch (s.source) {
        case SourceKind::Udp:       src = QStringLiteral("UDP"); break;
        case SourceKind::UdpPaused: src = tr("pause"); break;
        case SourceKind::Dxgi:      src = QStringLiteral("DXGI"); break;
        case SourceKind::Clip:      src = tr("clip"); break;
        case SourceKind::None:      src = tr("no source"); break;
        }
        parts << tr("P%1 %2 %3 fps").arg(s.player).arg(src).arg(qRound(s.fpsOut));
    }
    QString text = QStringLiteral("OpenFIRE Scope Daemon");
    if (!parts.isEmpty()) text += QLatin1Char('\n') + parts.join(QStringLiteral(" · "));
    else text += QLatin1Char('\n') + (devices().isEmpty() ? tr("No dongle connected") : tr("No scope paired"));
    const QStringList warn = warnings();
    if (!warn.isEmpty()) text += QLatin1Char('\n') + warn.join(QLatin1Char('\n'));
    return text;
}

void Daemon::setSuspended(bool suspended)
{
    // EN: a choice of the user ends the suspension of the App (and its timer). / IT: una scelta dell'utente chiude la sospensione dell'App (e il suo timer).
    if (m_commandSuspend) {
        m_commandSuspend = false;
        m_resumeTimer->stop();
    }
    applySuspended(suspended, suspended ? tr("Suspended: serial ports released") : tr("Resumed"));
}

void Daemon::handleCommand(const QByteArray &command)
{
    if (command == "RESUME") {
        if (m_commandSuspend) endCommandSuspend(tr("Resumed by the OpenFIRE App"));
        return;
    }
    // EN: "SUSPEND" or "SUSPEND;T=n", n from 1 to 600 s; another or missing value = 120 s.
    // IT: "SUSPEND" o "SUSPEND;T=n", n da 1 a 600 s; un valore diverso o assente = 120 s.
    int seconds = 120;
    if (command.startsWith("SUSPEND;T=")) {
        bool ok = false;
        const int value = command.mid(10).toInt(&ok);
        if (ok && value >= 1 && value <= 600) seconds = value;
    }
    if (m_suspended && !m_commandSuspend) {
        Log::info(tr("The OpenFIRE App asks to suspend: already suspended by the user"));
        return;
    }
    m_commandSuspend = true;
    m_resumeTimer->start(seconds * 1000);
    if (m_suspended) Log::info(tr("The OpenFIRE App extends the suspension to %1 s").arg(seconds));
    else applySuspended(true, tr("Suspended by the OpenFIRE App for at most %1 s: serial ports released").arg(seconds));
}

void Daemon::endCommandSuspend(const QString &logText)
{
    m_commandSuspend = false;
    m_resumeTimer->stop();
    applySuspended(false, logText);
}

void Daemon::applySuspended(bool suspended, const QString &logText)
{
    if (m_suspended == suspended) return;
    m_suspended = suspended;
    // EN: closes every port and stops the probes / starts again. / IT: chiude tutte le porte e ferma le prove / riparte.
    m_dongles->setSuspended(suspended);
    Log::info(logText);
    emit suspendedChanged(suspended);
    emit statusChanged();
}

void Daemon::setCaptureEnabled(bool on)
{
    if (!Settings::captureSupported()) return;
    m_settings->setCaptureEnabled(on);
    Log::info(on ? tr("Screen capture on") : tr("Screen capture off"));
    m_settings->notifyChanged();
}

void Daemon::setCaptureZoom(int zoom)
{
    m_settings->setCaptureZoom(zoom);
    Log::info(tr("Capture zoom %1x").arg(m_settings->captureZoom()));
    m_settings->notifyChanged();
}

void Daemon::retryPort(const QString &port)
{
    // EN: forgets the result of this port and probes it again. / IT: dimentica l'esito di questa porta e la riprova.
    m_dongles->retryPort(port);
}

void Daemon::checkCapture()
{
    // EN: these flags change in the capture threads: the icon and the window are told here.
    // IT: questi indicatori cambiano nei thread della cattura: l'icona e la finestra lo sanno da qui.
    quint32 flags = 0;
    for (int p = 1; p <= Settings::Players; ++p) {
        if (m_capture->aimMissing(p)) flags |= 1u << (p - 1);
        if (m_aim->duplicate(p)) flags |= 1u << (p + 3);
    }
    if (flags == m_captureFlags) return;
    m_captureFlags = flags;
    emit statusChanged();
}

InputFramePtr Daemon::latestFrame(int player) const
{
    return m_udp->latestFrame(player);
}

EncodedFramePtr Daemon::sentFrame(int player) const
{
    return m_dongles->sentFrame(player);
}

QByteArray Daemon::discoveryAnswer() const
{
    // EN: one line per dongle: "OF_SCOPE:Pn;PORT=..." for the paired ones (with two dongles on the same player
    //     only the first), "OF_SCOPE:UNPAIRED" for the free ones and those still in BOOT, "OF_SCOPE:NONE"
    //     without dongles. Extra fields are ignored by the games.
    // IT: una riga per dongle: "OF_SCOPE:Pn;PORT=..." per quelli abbinati (con due dongle sullo stesso giocatore
    //     solo il primo), "OF_SCOPE:UNPAIRED" per quelli liberi e quelli ancora in BOOT, "OF_SCOPE:NONE"
    //     senza dongle. I campi in piu' i giochi li ignorano.
    QList<QByteArray> lines;
    for (const SlotStatus &s : slotStatus()) {
        if (s.scope == ScopeState::NoDongle) continue;
        QByteArray src;
        switch (s.source) {
        case SourceKind::None:      src = "NONE"; break;
        case SourceKind::Udp:       src = "UDP"; break;
        case SourceKind::UdpPaused: src = "PAUSE"; break;
        case SourceKind::Dxgi:      src = "DXGI"; break;
        case SourceKind::Clip:      src = "CLIP"; break;
        }
        QByteArray line = QByteArray(OF_MSG_SCOPE) + 'P' + QByteArray::number(s.player) + ";PORT=" + QByteArray::number(s.udpPort);
        if (!s.firmware.isEmpty()) line += ";FW=" + s.firmware.toLatin1();
        if (s.channel) line += ";CH=" + QByteArray::number(s.channel);
        line += ";SRC=" + src;
        line += ";FPS=" + QByteArray::number(qRound(s.fpsOut));
        line += ";Q=" + QByteArray::number(qRound(s.q));
        line += ";C=" + QByteArray::number(qRound(s.capacityKBs));
        line += ";DROP=" + QByteArray::number(qRound(s.dropPerSec));
        line += ";QFULL=" + QByteArray::number(qRound(s.qfullPerSec));
        lines << line;
    }
    for (const DeviceStatus &d : devices()) {
        if (d.state != PortState::Free && d.state != PortState::Boot) continue;
        QByteArray line = QByteArray(OF_MSG_SCOPE) + OF_MSG_UNPAIRED;
        if (!d.firmware.isEmpty()) line += ";FW=" + d.firmware.toLatin1();
        if (d.channel) line += ";CH=" + QByteArray::number(d.channel);
        lines << line;
    }
    if (lines.isEmpty()) lines << QByteArray(OF_MSG_SCOPE) + OF_MSG_NONE;
    return lines.join('\n');
}
