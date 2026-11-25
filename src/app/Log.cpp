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

#include "app/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <cstdio>

Log &Log::instance()
{
    static Log log;
    return log;
}

QString Log::format(const Entry &entry)
{
    const char *tag = entry.level == Level::Error ? "ERR " : entry.level == Level::Warning ? "WARN" : "INFO";
    return QStringLiteral("%1 %2 %3")
        .arg(entry.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), QLatin1String(tag), entry.text);
}

void Log::add(Level level, const QString &text)
{
    Entry entry{ QDateTime::currentDateTime(), level, text };
    {
        QMutexLocker lock(&m_mutex);
        if (m_ring.size() < Capacity) {
            m_ring.append(entry);
        } else {
            m_ring[m_head] = entry;
            m_head = (m_head + 1) % Capacity;
        }
        if (m_file.isOpen() || m_echo) {
            const QByteArray line = format(entry).toUtf8() + '\n';
            if (m_file.isOpen()) {
                m_file.write(line);
                m_file.flush();
            }
            if (m_echo) {
                std::fwrite(line.constData(), 1, size_t(line.size()), stderr);
                std::fflush(stderr);
            }
        }
    }
    emit entryAdded(entry);
}

QVector<Log::Entry> Log::entries() const
{
    QMutexLocker lock(&m_mutex);
    QVector<Entry> out;
    out.reserve(m_ring.size());
    for (int i = 0; i < m_ring.size(); ++i)
        out.append(m_ring.at((m_head + i) % m_ring.size()));
    return out;
}

void Log::setFile(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen() && m_file.fileName() == path) return;
    m_file.close();
    if (path.isEmpty()) return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);
    // EN: a log over 4 MB starts again. / IT: un registro oltre 4 MB riparte da zero.
    const QIODevice::OpenMode mode = (QFileInfo(path).size() > 4 * 1024 * 1024)
                                         ? (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
                                         : (QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (!m_file.open(mode)) {
        lock.unlock();
        add(Level::Error, QCoreApplication::translate("Log", "Cannot open the log file %1").arg(QDir::toNativeSeparators(path)));
    }
}

void Log::setEcho(bool on)
{
    QMutexLocker lock(&m_mutex);
    m_echo = on;
}
