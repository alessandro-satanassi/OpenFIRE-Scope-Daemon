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

#ifndef OFD_SETTINGS_H
#define OFD_SETTINGS_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVector>

// EN: Lightgun USB identity -> player, for the aim point of the screen capture (Raw Input).
// IT: Identita' USB della lightgun -> giocatore, per il punto di mira della cattura dello schermo (Raw Input).
struct LightgunMapEntry {
    quint16 vid = 0;
    quint16 pid = 0;
    int player = 0;   // 1..4
};

// EN: Thread priority of a group of threads. / IT: priorita' di un gruppo di thread.
enum class ThreadPriority { Normal, AboveNormal };

// EN: Settings in an INI file (QSettings). Read and written only from the main thread; the other
//     threads receive copies of the values they need. Command line values override the file for this
//     run only and are never saved.
// IT: Impostazioni in un file INI (QSettings). Lette e scritte solo dal thread principale; gli altri
//     thread ricevono copie dei valori che servono. I valori della riga di comando valgono solo per
//     questo avvio e non si salvano mai.
class Settings : public QObject
{
    Q_OBJECT

public:
    static constexpr int Players = 4;

    explicit Settings(QObject *parent = nullptr);

    QString filePath() const;

    // EN: network / IT: rete
    quint16 discoveryPort() const;              // 47810
    void setDiscoveryPort(quint16 port);
    quint16 preferredVideoPort(int player) const;   // player 1..4: 47811..47814
    void setPreferredVideoPort(int player, quint16 port);

    // EN: screen capture (Windows only) / IT: cattura dello schermo (solo Windows)
    static bool captureSupported();
    bool captureEnabled() const;
    void setCaptureEnabled(bool on);
    QString captureMonitor() const;             // empty = primary / vuoto = principale
    void setCaptureMonitor(const QString &name);
    int captureZoom() const;                    // 1..4, default 2
    void setCaptureZoom(int zoom);
    QVector<LightgunMapEntry> lightgunMap() const;
    void setLightgunMap(const QVector<LightgunMapEntry> &map);
    static QString lightgunMapToText(const QVector<LightgunMapEntry> &map);
    static bool lightgunMapFromText(const QString &text, QVector<LightgunMapEntry> *map);

    // EN: dongles / IT: dongle
    quint16 dongleVid() const;                  // 0x303A
    quint16 donglePid() const;                  // 0x1001
    void setDongleVidPid(quint16 vid, quint16 pid);

    // EN: threads / IT: thread
    ThreadPriority serialPriority() const;      // above normal / sopra il normale
    ThreadPriority udpPriority() const;         // normal / normale

    // EN: log / IT: registro
    bool logToFile() const;
    void setLogToFile(bool on);
    QString logFilePath() const;

    // EN: window / IT: finestra
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    // EN: command line overrides, this run only. A setter called from the window cancels its override.
    // IT: valori dalla riga di comando, solo per questo avvio. Un setter chiamato dalla finestra annulla il suo valore.
    void overrideDiscoveryPort(quint16 port);
    void overrideCaptureOff();
    void overrideLogFile(const QString &path);
    bool hasOverrides() const;

    // EN: emitted after any change made from the window / IT: emesso dopo ogni modifica fatta dalla finestra
    void notifyChanged();

signals:
    void changed();

private:
    QString defaultLogFilePath() const;

    mutable QSettings m_ini;
    quint16 m_discoveryOverride = 0;
    bool m_captureOffOverride = false;
    QString m_logFileOverride;
};

#endif // OFD_SETTINGS_H
