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

#include "app/Log.h"

Daemon::Daemon(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_slots.resize(Settings::Players);
    for (int i = 0; i < Settings::Players; ++i) m_slots[i].player = i + 1;

    connect(m_settings, &Settings::changed, this, &Daemon::applySettings);
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
    emit statusChanged();
}

void Daemon::stop()
{
    if (!m_running) return;
    m_running = false;
    Log::info(tr("Daemon stopped"));
}

void Daemon::applySettings()
{
    Log::instance().setFile(m_settings->logToFile() ? m_settings->logFilePath() : QString());
    emit statusChanged();
}

QStringList Daemon::warnings() const
{
    QStringList out = m_warnings;
    if (m_suspended) out.prepend(tr("Suspended: the serial ports are released"));
    return out;
}

IconState Daemon::iconState() const
{
    if (!warnings().isEmpty()) return IconState::Warning;
    bool paired = false;
    for (const SlotStatus &s : m_slots)
        if (s.scope != ScopeState::NoDongle) paired = true;
    if (paired) return IconState::Active;
    return m_devices.isEmpty() ? IconState::Idle : IconState::Dongles;
}

QString Daemon::summary() const
{
    QStringList parts;
    for (const SlotStatus &s : m_slots) {
        if (s.scope == ScopeState::NoDongle) continue;
        QString src;
        switch (s.source) {
        case SourceKind::Udp:       src = QStringLiteral("UDP"); break;
        case SourceKind::UdpPaused: src = tr("pause"); break;
        case SourceKind::Dxgi:      src = QStringLiteral("DXGI"); break;
        case SourceKind::None:      src = tr("no source"); break;
        }
        parts << tr("P%1 %2 %3 fps").arg(s.player).arg(src).arg(qRound(s.fpsOut));
    }
    QString text = QStringLiteral("OpenFIRE Scope Daemon");
    if (!parts.isEmpty()) text += QLatin1Char('\n') + parts.join(QStringLiteral(" · "));
    else text += QLatin1Char('\n') + (m_devices.isEmpty() ? tr("No dongle connected") : tr("No scope paired"));
    const QStringList warn = warnings();
    if (!warn.isEmpty()) text += QLatin1Char('\n') + warn.join(QLatin1Char('\n'));
    return text;
}

void Daemon::setSuspended(bool suspended)
{
    if (m_suspended == suspended) return;
    m_suspended = suspended;
    // EN: phase 2: close every port and stop the probes / start again. / IT: fase 2: chiude tutte le porte e ferma le prove / riparte.
    Log::info(suspended ? tr("Suspended: serial ports released") : tr("Resumed"));
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
    // EN: phase 2: forget the result of this port and probe it again. / IT: fase 2: dimentica l'esito di questa porta e la riprova.
    Log::info(tr("Retry %1").arg(port));
}
