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

#include "capture/ScreenCapture.h"

// EN: Linux and macOS: no screen capture for now; the window shows it as not available.
// IT: Linux e macOS: per ora niente cattura dello schermo; la finestra la mostra come non disponibile.

ScreenCapture::ScreenCapture(SlotInput *const inputs[Settings::Players], AimInput *aim, QObject *parent)
    : QThread(parent)
    , m_aim(aim)
{
    for (int i = 0; i < Settings::Players; ++i) m_slots[i] = inputs[i];
}

ScreenCapture::~ScreenCapture()
{
    stopCapture();
}

bool ScreenCapture::supported()
{
    return false;
}

QVector<MonitorInfo> ScreenCapture::monitors()
{
    return {};
}

void ScreenCapture::configure(bool enabled, const QString &monitor, int zoom)
{
    QMutexLocker lock(&m_mutex);
    m_enabled = enabled;
    m_monitor = monitor;
    m_zoom = zoom;
}

void ScreenCapture::stopCapture()
{
    m_stop.store(true);
    wait();
}

bool ScreenCapture::aimMissing(int) const
{
    return false;
}

void ScreenCapture::run()
{
}
