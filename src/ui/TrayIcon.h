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

#ifndef OFD_TRAYICON_H
#define OFD_TRAYICON_H

#include <QIcon>
#include <QObject>

#include "app/Daemon.h"

class QAction;
class QActionGroup;
class QMenu;
class QSystemTrayIcon;

// EN: Icon in the notification area (menu bar on macOS): colour of the state, summary as tooltip, menu.
// IT: Icona nell'area di notifica (barra dei menu su macOS): colore dello stato, riassunto come suggerimento, menu.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(Daemon *daemon, QObject *parent = nullptr);
    ~TrayIcon() override;

signals:
    void showWindowRequested();
    void quitRequested();

private:
    void refresh();
    static QIcon makeIcon(IconState state);

    Daemon *m_daemon = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_captureAction = nullptr;
    QMenu *m_zoomMenu = nullptr;
    QActionGroup *m_zoomGroup = nullptr;
    QAction *m_suspendAction = nullptr;
    QIcon m_icons[4];
    int m_shownState = -1;
    QString m_shownTip;
};

#endif // OFD_TRAYICON_H
