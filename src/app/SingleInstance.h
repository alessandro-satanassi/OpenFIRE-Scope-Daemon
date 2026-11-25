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

#ifndef OFD_SINGLEINSTANCE_H
#define OFD_SINGLEINSTANCE_H

#include <QLockFile>
#include <QObject>
#include <memory>

class QLocalServer;

// EN: One daemon per user. The QLockFile gives the exclusivity (on Windows two QLocalServer can listen
//     on the same name, so the server alone is not enough). The QLocalServer lets a second start ask
//     the first one to open its window.
// IT: Un solo demone per utente. Il QLockFile da' l'esclusivita' (su Windows due QLocalServer possono
//     ascoltare sullo stesso nome, quindi il server da solo non basta). Il QLocalServer permette a un
//     secondo avvio di chiedere al primo di aprire la sua finestra.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    // EN: true = this is the only instance; false = another one runs and has been asked to show its window.
    // IT: true = questa e' l'unica istanza; false = ne gira gia' un'altra, a cui si e' chiesto di mostrare la finestra.
    bool acquire();

signals:
    void showRequested();

private:
    static QString serverName();
    void onNewConnection();

    std::unique_ptr<QLockFile> m_lock;
    QLocalServer *m_server = nullptr;
};

#endif // OFD_SINGLEINSTANCE_H
