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
#include "dongle/EncodedFrame.h"
#include "udp/SlotInput.h"

class AimInput;
class ClipPlayer;
class DongleLink;
class SlotEncoder;
class Discovery;
class QTimer;
class ScreenCapture;
class DongleManager;
class UdpInput;

// EN: The daemon core: owns every part and the state the window shows. It lives in the main thread and
//     never uses widgets, so it also runs headless: dongles, test clip, UDP from the games, discovery,
//     preview, encoders and, on Windows, the screen capture with the aim points.
// IT: Il cuore del demone: possiede tutte le parti e lo stato che la finestra mostra. Vive nel thread
//     principale e non usa mai i widget, quindi funziona anche senza finestra: dongle, clip di prova, UDP
//     dai giochi, discovery, anteprima, encoder e, su Windows, la cattura dello schermo con i punti di mira.
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
    InputFramePtr latestFrame(int player) const;   // EN: for the preview / IT: per l'anteprima
    EncodedFramePtr sentFrame(int player) const;   // EN: preview "Output" / IT: anteprima "Uscita"

    bool isSuspended() const { return m_suspended; }

    // EN: from the OpenFIRE App through the discovery port, before it reboots a board: "SUSPEND;T=n" releases the
    //     serial ports for n seconds (1-600, "SUSPEND" alone 120), "RESUME" ends it before; anything else is
    //     ignored. A suspension chosen by the user is never ended by a command; the user can end a suspension of
    //     the App from the menu.
    // IT: dall'App OpenFIRE tramite la porta di discovery, prima che riavvii una scheda: "SUSPEND;T=n" rilascia le
    //     seriali per n secondi (1-600, "SUSPEND" da solo 120), "RESUME" la chiude prima; tutto il resto si
    //     ignora. Una sospensione scelta dall'utente non la chiude mai un comando; l'utente puo' chiudere dal
    //     menu una sospensione dell'App.
    void handleCommand(const QByteArray &command);

    // EN: from the settings dialog, before saving: applies the ports all together or none. false: nothing changed,
    //     message says why. The dialog saves the INI only after true, so the INI holds only ports that worked.
    // IT: dalla finestra delle impostazioni, prima di salvare: applica le porte tutte insieme o nessuna. false: non
    //     cambia nulla, message dice perche'. La finestra salva l'INI solo dopo true, cosi' l'INI ha solo porte che hanno funzionato.
    bool tryPorts(const QVector<quint16> &video, quint16 discovery, QString *message);

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
    void applySuspended(bool suspended, const QString &logText);
    void endCommandSuspend(const QString &logText);
    bool applyPorts(const QVector<quint16> &video, quint16 discovery, QString *failure);
    void checkCapture();
    QByteArray discoveryAnswer() const;

    Settings *m_settings = nullptr;
    QVector<SlotStatus> m_slots;
    DongleManager *m_dongles = nullptr;
    ClipPlayer *m_clip = nullptr;
    UdpInput *m_udp = nullptr;
    Discovery *m_discovery = nullptr;
    SlotEncoder *m_encoders[Settings::Players] = {};
    AimInput *m_aim = nullptr;
    ScreenCapture *m_capture = nullptr;
    QTimer *m_captureTimer = nullptr;
    quint32 m_captureFlags = 0;   // EN: aim missing and duplicate lightguns, last seen / IT: mira mancante e lightgun doppie, ultime viste
    QStringList m_warnings;
    QString m_portWarning;   // EN: last port change not applied / IT: ultimo cambio di porte non applicato
    bool m_suspended = false;
    bool m_commandSuspend = false;   // EN: suspended by "SUSPEND", ends by itself / IT: sospeso da "SUSPEND", finisce da solo
    QTimer *m_resumeTimer = nullptr;
    bool m_running = false;
};

#endif // OFD_DAEMON_H
