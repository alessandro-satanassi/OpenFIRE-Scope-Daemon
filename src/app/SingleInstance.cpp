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

#include "app/SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

namespace {
const QByteArray kShowCommand = QByteArrayLiteral("SHOW\n");
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    if (m_server) m_server->close();
    if (m_lock) m_lock->unlock();
}

QString SingleInstance::serverName()
{
    // EN: per user: the name contains a hash of the home folder. / IT: per utente: il nome contiene un hash della cartella home.
    const QByteArray home = QDir::homePath().toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(home, QCryptographicHash::Sha1).toHex().left(12));
    return QStringLiteral("OpenFIRE-Scope-Daemon-") + hash;
}

bool SingleInstance::acquire()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    QDir().mkpath(dir);

    m_lock = std::make_unique<QLockFile>(dir + QStringLiteral("/OpenFIRE-Scope-Daemon.lock"));
    m_lock->setStaleLockTime(0);   // EN: stale only if its process is gone / IT: scaduto solo se il suo processo non c'e' piu'

    if (!m_lock->tryLock(100)) {
        // EN: another daemon runs: ask it to show its window. / IT: gira gia' un altro demone: gli si chiede di mostrare la finestra.
        QLocalSocket socket;
        socket.connectToServer(serverName());
        if (socket.waitForConnected(500)) {
            socket.write(kShowCommand);
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
        }
        m_lock.reset();
        return false;
    }

    // EN: we own the lock: a socket left by a crash can be removed. / IT: il lock e' nostro: un socket lasciato da un crash si puo' togliere.
    QLocalServer::removeServer(serverName());
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    m_server->listen(serverName());   // EN: failure only loses "show the window" / IT: se fallisce si perde solo "mostra la finestra"
    return true;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            // EN: wait for the whole line; anything long without '\n' is not ours. / IT: aspetta la riga intera; un testo lungo senza '\n' non e' nostro.
            if (!socket->canReadLine()) {
                if (socket->bytesAvailable() > 64) socket->disconnectFromServer();
                return;
            }
            if (socket->readLine(64) == kShowCommand) emit showRequested();
            socket->disconnectFromServer();
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}
