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

#ifndef OFD_STATUS_H
#define OFD_STATUS_H

#include <QString>
#include <QtGlobal>

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
    Dxgi,        // screen capture (Windows) / cattura dello schermo (Windows)
    Clip         // test clip (--play-clip) / clip di prova (--play-clip)
};

struct SlotStatus {
    int player = 0;                 // 1..4
    ScopeState scope = ScopeState::NoDongle;
    QString port;                   // COM5, /dev/ttyACM0...
    int channel = 0;
    QString firmware;
    quint16 udpPort = 0;            // effective port, 0 = not open / porta effettiva, 0 = non aperta
    SourceKind source = SourceKind::None;
    bool tcp = false;               // EN: Udp source with the frames over TCP / IT: sorgente Udp con i frame via TCP
    double fpsIn = 0.0;
    double fpsOut = 0.0;
    double kbPerFrame = 0.0;
    double q = 0.0;
    double encodeMs = 0.0;          // EN: mean encoding time of a frame on the PC / IT: tempo medio di codifica di un frame sul PC
    double capacityKBs = 0.0;       // C
    double dropPerSec = 0.0;
    double qfullPerSec = 0.0;
    double udpIncompletePerSec = 0.0;   // EN: UDP frames replaced before complete / IT: frame UDP sostituiti prima di essere completi
    double udpInvalidPerSec = 0.0;      // EN: UDP packets that fail the checks / IT: pacchetti UDP che non passano i controlli
    bool aimMissing = false;        // DXGI with several players and no absolute aim / DXGI con piu' giocatori senza mira assoluta
};

// EN: Serial ports that are not (or not yet) assigned to a player. / IT: porte seriali non (ancora) assegnate a un giocatore.
enum class PortState {
    Waiting,       // new, waits 1 s / nuova, aspetta 1 s
    Probing,       // WHOAMI sent / WHOAMI inviato
    Boot,          // OF_SCOPE:BOOT
    Free,          // OF_SCOPE:UNPAIRED
    Duplicate,     // Pn already used by another dongle / Pn gia' usato da un altro dongle
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

#endif // OFD_STATUS_H
