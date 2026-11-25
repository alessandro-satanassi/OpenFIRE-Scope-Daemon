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

#ifndef OFD_SCREENCAPTURE_H
#define OFD_SCREENCAPTURE_H

#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <atomic>

#include "app/Settings.h"

class AimInput;
class SlotInput;

// EN: A monitor for the settings: Windows name (\\.\DISPLAY1), saved in the INI, and the text of the list.
// IT: Un monitor per le impostazioni: nome di Windows (\\.\DISPLAY1), salvato nell'INI, e il testo dell'elenco.
struct MonitorInfo {
    QString device;
    QString label;
    bool primary = false;
};

// EN: Screen capture (Windows only, "Cattura DXGI e punto di mira" of the plan). One Desktop Duplication of
//     the game monitor serves every slot in capture. It is on only while at least one slot with a paired
//     scope has no UDP, and it is released 2 s after it is no longer needed, so it costs nothing to the
//     native games. D3D11 device on the graphics card of that monitor; DuplicateOutput1 in BGRA 8 bit, or
//     DuplicateOutput. AcquireNextFrame with an 8 ms timeout: a new desktop is copied on the GPU into the
//     "latest desktop" texture and the frame is released at once (LastPresentTime = 0: only the pointer,
//     nothing is copied). Every slot in capture is cropped when a new desktop arrives or its aim point moves
//     by at least 1 pixel, at most once every 16 ms: square of side 240 / zoom around the aim point, copied
//     on the GPU into a small staging texture (only the part inside the screen, the rest is black), read by
//     the CPU, rotated as the monitor and enlarged to 240x240. DXGI_ERROR_ACCESS_LOST or another error: all
//     is released and created again every 500 ms; meanwhile the capture is not live. Aim point: the lightgun
//     (Raw Input, put on the game monitor); with one player in capture and no absolute aim GetCursorPos;
//     otherwise the centre of the monitor, reported in the window. On Linux and macOS it does nothing.
// IT: Cattura dello schermo (solo Windows, "Cattura DXGI e punto di mira" del piano). Una sola Desktop
//     Duplication del monitor del gioco serve tutti gli slot in cattura. E' accesa solo mentre almeno uno
//     slot con mirino abbinato non ha UDP, e si rilascia 2 s dopo che non serve piu', cosi' non costa nulla ai
//     giochi nativi. Dispositivo D3D11 sulla scheda video di quel monitor; DuplicateOutput1 in BGRA a 8 bit,
//     o DuplicateOutput. AcquireNextFrame con timeout di 8 ms: un desktop nuovo si copia sulla GPU nella
//     texture "ultimo desktop" e il frame si rilascia subito (LastPresentTime = 0: solo il puntatore, non si
//     copia nulla). Ogni slot in cattura si ritaglia quando arriva un desktop nuovo o il suo punto di mira si
//     sposta di almeno 1 pixel, al massimo una volta ogni 16 ms: quadrato di lato 240 / zoom attorno al punto
//     di mira, copiato sulla GPU in una piccola texture di appoggio (solo la parte dentro lo schermo, il resto
//     e' nero), letto dalla CPU, ruotato come il monitor e ingrandito a 240x240. DXGI_ERROR_ACCESS_LOST o un
//     altro errore: si rilascia tutto e si ricrea ogni 500 ms; intanto la cattura non e' viva. Punto di mira:
//     la lightgun (Raw Input, portata sul monitor del gioco); con un solo giocatore in cattura e senza mira
//     assoluta GetCursorPos; altrimenti il centro del monitor, segnalato in finestra. Su Linux e macOS non fa
//     nulla.
class ScreenCapture : public QThread
{
    Q_OBJECT

public:
    ScreenCapture(SlotInput *const inputs[Settings::Players], AimInput *aim, QObject *parent = nullptr);
    ~ScreenCapture() override;

    static bool supported();
    static QVector<MonitorInfo> monitors();

    // EN: from the main thread; used from the next round of the capture. monitor: empty = primary.
    // IT: dal thread principale; vale dal giro successivo della cattura. monitor: vuoto = principale.
    void configure(bool enabled, const QString &monitor, int zoom);
    void stopCapture();                 // EN: stops and waits / IT: ferma e aspetta
    bool aimMissing(int player) const;  // EN: in capture without an aim of its own / IT: in cattura senza una mira sua

protected:
    void run() override;

private:
    SlotInput *m_slots[Settings::Players] = {};
    AimInput *m_aim = nullptr;
    mutable QMutex m_mutex;
    bool m_enabled = false;
    QString m_monitor;
    int m_zoom = 2;
    quint64 m_configSerial = 0;   // EN: grows at every change of monitor / IT: cresce a ogni cambio di monitor
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_aimMissing[Settings::Players]{};
};

#endif // OFD_SCREENCAPTURE_H
