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

#ifndef OFD_AIMINPUT_H
#define OFD_AIMINPUT_H

#include <QMutex>
#include <QThread>
#include <QVector>
#include <atomic>

#include "app/Settings.h"

// EN: Aim point of every player for the screen capture (Windows only): Raw Input with RIDEV_INPUTSINK on a
//     hidden window, in its own thread, so the movements arrive also when the daemon is not in front. Every
//     device is recognised by its name (VID_xxxx&PID_yyyy) and the VID:PID -> player table of the settings.
//     Only absolute movements count. The lightgun is calibrated on the screen it aims at, the game monitor,
//     as the emulators that read Raw Input assume: its coordinates 0-65535 are given as 0-1 and the capture
//     puts them on the game monitor, whatever the flags (MOUSE_VIRTUAL_DESKTOP or primary monitor). The flags
//     of every lightgun are written in the log at its first absolute movement. Two lightguns on the same
//     player: only the first counts. On Linux and macOS it does nothing.
// IT: Punto di mira di ogni giocatore per la cattura dello schermo (solo Windows): Raw Input con
//     RIDEV_INPUTSINK su una finestra nascosta, nel suo thread, cosi' i movimenti arrivano anche quando il
//     demone non e' in primo piano. Ogni dispositivo si riconosce dal nome (VID_xxxx&PID_yyyy) e dalla tabella
//     VID:PID -> giocatore delle impostazioni. Contano solo i movimenti assoluti. La lightgun e' tarata sullo
//     schermo che inquadra, il monitor del gioco, come assumono gli emulatori che leggono Raw Input: le sue
//     coordinate 0-65535 si danno come 0-1 e la cattura le porta sul monitor del gioco, qualunque siano i flag
//     (MOUSE_VIRTUAL_DESKTOP o monitor principale). I flag di ogni lightgun si scrivono nel registro al suo
//     primo movimento assoluto. Due lightgun sullo stesso giocatore: conta solo la prima. Su Linux e macOS non
//     fa nulla.
class AimInput : public QThread
{
    Q_OBJECT

public:
    struct Point {
        bool valid = false;   // EN: an absolute position has arrived / IT: e' arrivata una posizione assoluta
        double x = 0.0;       // EN: 0-1 on the game monitor / IT: 0-1 sul monitor del gioco
        double y = 0.0;
    };

    explicit AimInput(QObject *parent = nullptr);
    ~AimInput() override;

    static bool supported();
    void setMap(const QVector<LightgunMapEntry> &map);   // EN: from the main thread / IT: dal thread principale
    Point aim(int player) const;                         // player 1..4
    bool duplicate(int player) const;                    // EN: a second lightgun on this player / IT: una seconda lightgun su questo giocatore
    void stopInput();                                    // EN: stops and waits / IT: ferma e aspetta

protected:
    void run() override;

private:
#ifdef Q_OS_WIN
    friend struct AimInputWindow;
#endif
    mutable QMutex m_mutex;
    QVector<LightgunMapEntry> m_map;
    bool m_mapChanged = true;
    Point m_points[Settings::Players];
    bool m_duplicate[Settings::Players] = {};
    std::atomic<void *> m_window{ nullptr };
    std::atomic<bool> m_stop{ false };
};

#endif // OFD_AIMINPUT_H
