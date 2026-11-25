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

#ifndef OFD_LOG_H
#define OFD_LOG_H

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

// EN: Event log: the last 500 events in memory, optionally also in a file. Thread-safe: any thread
//     calls Log::info/warn/error; the window receives entryAdded in its own thread (queued signal).
// IT: Registro degli eventi: gli ultimi 500 in memoria, a scelta anche su file. Sicuro tra i thread:
//     qualsiasi thread chiama Log::info/warn/error; la finestra riceve entryAdded nel suo thread
//     (segnale accodato).
class Log : public QObject
{
    Q_OBJECT

public:
    enum class Level { Info, Warning, Error };

    struct Entry {
        QDateTime time;
        Level level = Level::Info;
        QString text;
    };

    static constexpr int Capacity = 500;

    static Log &instance();

    static void info(const QString &text) { instance().add(Level::Info, text); }
    static void warn(const QString &text) { instance().add(Level::Warning, text); }
    static void error(const QString &text) { instance().add(Level::Error, text); }

    void add(Level level, const QString &text);
    QVector<Entry> entries() const;
    static QString format(const Entry &entry);

    // EN: empty path = no file. / IT: percorso vuoto = nessun file.
    void setFile(const QString &path);
    // EN: also on stderr (headless mode). / IT: anche su stderr (modalita' senza finestra).
    void setEcho(bool on);

signals:
    void entryAdded(const Log::Entry &entry);

private:
    Log() = default;

    mutable QMutex m_mutex;
    QVector<Entry> m_ring;
    int m_head = 0;
    QFile m_file;
    bool m_echo = false;
};

Q_DECLARE_METATYPE(Log::Entry)

#endif // OFD_LOG_H
