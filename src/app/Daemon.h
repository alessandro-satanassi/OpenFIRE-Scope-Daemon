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

#ifndef OFD_DAEMON_H
#define OFD_DAEMON_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "app/Settings.h"
#include "app/Status.h"

class ClipPlayer;
class DongleManager;

// EN: The daemon core: owns every part and the state the window shows. It lives in the main thread and
//     never uses widgets, so it also runs headless. Phase 2: dongles and test clip; UDP, encoder and
//     capture arrive in the next phases.
// IT: Il cuore del demone: possiede tutte le parti e lo stato che la finestra mostra. Vive nel thread
//     principale e non usa mai i widget, quindi funziona anche senza finestra. Fase 2: dongle e clip di
//     prova; UDP, encoder e cattura arrivano nelle fasi successive.
class Daemon : public QObject
{
    Q_OBJECT

public:
    explicit Daemon(Settings *settings, QObject *parent = nullptr);
    ~Daemon() override;

    Settings *settings() const { return m_settings; }

    // EN: before start(): test clip sent in a loop to every paired scope. / IT: prima di start(): clip di prova mandata in ciclo a ogni mirino abbinato.
    bool setClipFile(const QString &path);

    void start();
    void stop();

    QVector<SlotStatus> slotStatus() const;
    QVector<DeviceStatus> devices() const;
    QStringList warnings() const;
    IconState iconState() const;
    QString summary() const;   // tooltip of the tray icon / suggerimento dell'icona

    bool isSuspended() const { return m_suspended; }

public slots:
    void setSuspended(bool suspended);
    void setCaptureEnabled(bool on);
    void setCaptureZoom(int zoom);
    void retryPort(const QString &port);

signals:
    void statusChanged();
    void suspendedChanged(bool suspended);

private:
    void applySettings();

    Settings *m_settings = nullptr;
    QVector<SlotStatus> m_slots;
    DongleManager *m_dongles = nullptr;
    ClipPlayer *m_clip = nullptr;
    QStringList m_warnings;
    bool m_suspended = false;
    bool m_running = false;
};

#endif // OFD_DAEMON_H
