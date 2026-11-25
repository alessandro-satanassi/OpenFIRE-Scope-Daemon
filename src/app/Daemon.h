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

// EN: State of the scope of one player, as the window and the tray icon show it.
// IT: Stato del mirino di un giocatore, come lo mostrano la finestra e l'icona.
enum class ScopeState {
    NoDongle,    // no dongle with this player / nessun dongle con questo giocatore
    Paired,      // dongle OF_SCOPE:Pn / dongle OF_SCOPE:Pn
    Duplicate    // a second dongle with the same player / un secondo dongle con lo stesso giocatore
};

enum class SourceKind {
    None,        // no source / nessuna sorgente
    Udp,         // frames from the game / frame dal gioco
    UdpPaused,   // game paused (presence packets) / gioco in pausa (pacchetti di presenza)
    Dxgi         // screen capture (Windows) / cattura dello schermo (Windows)
};

struct SlotStatus {
    int player = 0;                 // 1..4
    ScopeState scope = ScopeState::NoDongle;
    QString port;                   // COM5, /dev/ttyACM0...
    int channel = 0;
    QString firmware;
    quint16 udpPort = 0;            // effective port, 0 = not open / porta effettiva, 0 = non aperta
    SourceKind source = SourceKind::None;
    double fpsIn = 0.0;
    double fpsOut = 0.0;
    double kbPerFrame = 0.0;
    double q = 0.0;
    double capacityKBs = 0.0;       // C
    double dropPerSec = 0.0;
    double qfullPerSec = 0.0;
    bool aimMissing = false;        // DXGI with several players and no absolute aim / DXGI con piu' giocatori senza mira assoluta
};

// EN: Serial ports that are not (or not yet) assigned to a player. / IT: porte seriali non (ancora) assegnate a un giocatore.
enum class PortState {
    Waiting,       // new, waits 1 s / nuova, aspetta 1 s
    Probing,       // WHOAMI sent / WHOAMI inviato
    Boot,          // OF_SCOPE:BOOT
    Free,          // OF_SCOPE:UNPAIRED
    NoAnswer,      // retried after 5 s and 10 s / riprovata dopo 5 s e 10 s
    Discarded,     // not a dongle until unplugged / non e' un dongle finche' non si scollega
    Busy,          // opened by another program / aperta da un altro programma
    AccessDenied,  // permissions / permessi
    Lost           // read or write error / errore di lettura o scrittura
};

struct DeviceStatus {
    QString port;
    QString serialNumber;
    PortState state = PortState::Waiting;
    int channel = 0;
    QString firmware;
    QString detail;
};

enum class IconState {
    Idle,        // grey: no dongle / grigio: nessun dongle
    Dongles,     // blue: dongles, no scope paired / blu: dongle, nessun mirino abbinato
    Active,      // green: at least one scope paired / verde: almeno un mirino abbinato
    Warning      // yellow: something needs attention, or suspended / giallo: qualcosa da guardare, o sospeso
};

// EN: The daemon core: owns every part and the state the window shows. It lives in the main thread and
//     never uses widgets, so it also runs headless. Phase 1: state, settings and suspend only; the
//     dongles, the UDP, the encoder and the capture arrive in the next phases.
// IT: Il cuore del demone: possiede tutte le parti e lo stato che la finestra mostra. Vive nel thread
//     principale e non usa mai i widget, quindi funziona anche senza finestra. Fase 1: solo stato,
//     impostazioni e sospensione; dongle, UDP, encoder e cattura arrivano nelle fasi successive.
class Daemon : public QObject
{
    Q_OBJECT

public:
    explicit Daemon(Settings *settings, QObject *parent = nullptr);
    ~Daemon() override;

    Settings *settings() const { return m_settings; }

    void start();
    void stop();

    QVector<SlotStatus> slotStatus() const { return m_slots; }
    QVector<DeviceStatus> devices() const { return m_devices; }
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
    QVector<DeviceStatus> m_devices;
    QStringList m_warnings;
    bool m_suspended = false;
    bool m_running = false;
};

#endif // OFD_DAEMON_H
