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

#include "capture/AimInput.h"

// EN: Linux and macOS: no screen capture for now, so no aim point. / IT: Linux e macOS: per ora niente cattura, quindi niente punto di mira.

AimInput::AimInput(QObject *parent)
    : QThread(parent)
{
}

AimInput::~AimInput()
{
    stopInput();
}

bool AimInput::supported()
{
    return false;
}

void AimInput::setMap(const QVector<LightgunMapEntry> &map)
{
    QMutexLocker lock(&m_mutex);
    m_map = map;
}

AimInput::Point AimInput::aim(int) const
{
    return Point();
}

bool AimInput::duplicate(int) const
{
    return false;
}

void AimInput::stopInput()
{
    m_stop.store(true);
    wait();
}

void AimInput::run()
{
}
