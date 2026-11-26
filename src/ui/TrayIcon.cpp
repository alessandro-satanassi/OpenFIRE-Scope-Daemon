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

#include "ui/TrayIcon.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

TrayIcon::TrayIcon(Daemon *daemon, QObject *parent)
    : QObject(parent)
    , m_daemon(daemon)
{
    for (int i = 0; i < 4; ++i) m_icons[i] = makeIcon(IconState(i));

    m_menu = new QMenu();

    QAction *openAction = m_menu->addAction(tr("Open window"));
    connect(openAction, &QAction::triggered, this, &TrayIcon::showWindowRequested);
    m_menu->addSeparator();

    if (Settings::captureSupported()) {
        m_captureAction = m_menu->addAction(tr("Allow screen capture"));
        m_captureAction->setCheckable(true);
        connect(m_captureAction, &QAction::triggered, m_daemon, &Daemon::setCaptureEnabled);
        // EN: on only while ticked (off at every start), and only if allowed. / IT: accesa solo mentre e' spuntata (spenta a ogni avvio), e solo se permessa.
        m_captureActiveAction = m_menu->addAction(tr("Screen capture on"));
        m_captureActiveAction->setCheckable(true);
        connect(m_captureActiveAction, &QAction::triggered, m_daemon, &Daemon::setCaptureActive);

        m_zoomMenu = m_menu->addMenu(tr("Zoom"));
        m_zoomGroup = new QActionGroup(this);
        m_zoomGroup->setExclusive(true);
        for (int z = 1; z <= 4; ++z) {
            QAction *a = m_zoomMenu->addAction(QStringLiteral("%1×").arg(z));
            a->setCheckable(true);
            a->setData(z);
            m_zoomGroup->addAction(a);
        }
        connect(m_zoomGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            m_daemon->setCaptureZoom(a->data().toInt());
        });
        m_menu->addSeparator();
    }

    m_suspendAction = m_menu->addAction(tr("Suspend (release the ports)"));
    connect(m_suspendAction, &QAction::triggered, this, [this]() {
        m_daemon->setSuspended(!m_daemon->isSuspended());
    });
    m_menu->addSeparator();

    QAction *quitAction = m_menu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    // EN: the menu shows the current values every time it opens. / IT: il menu mostra i valori attuali ogni volta che si apre.
    connect(m_menu, &QMenu::aboutToShow, this, &TrayIcon::refresh);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
#ifdef Q_OS_MACOS
        // EN: on macOS a click opens the menu. / IT: su macOS un clic apre il menu.
        if (reason == QSystemTrayIcon::DoubleClick) emit showWindowRequested();
#else
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) emit showWindowRequested();
#endif
    });

    connect(m_daemon, &Daemon::statusChanged, this, &TrayIcon::refresh);
    connect(m_daemon, &Daemon::suspendedChanged, this, &TrayIcon::refresh);
    connect(m_daemon->settings(), &Settings::changed, this, &TrayIcon::refresh);

    refresh();
    m_tray->show();
}

TrayIcon::~TrayIcon()
{
    m_tray->hide();
    delete m_menu;
}

void TrayIcon::refresh()
{
    const int state = int(m_daemon->iconState());
    if (state != m_shownState) {
        m_shownState = state;
        m_tray->setIcon(m_icons[state]);
    }
    const QString tip = m_daemon->summary();
    if (tip != m_shownTip) {
        m_shownTip = tip;
        m_tray->setToolTip(tip);
    }

    if (m_captureAction) {
        const Settings *s = m_daemon->settings();
        m_captureAction->setChecked(s->captureEnabled());
        m_captureActiveAction->setChecked(m_daemon->captureActive());
        m_captureActiveAction->setEnabled(s->captureEnabled() && !s->captureAuto());   // EN: not needed with the automatic start / IT: non serve con l'avvio automatico
        const int zoom = s->captureZoom();
        for (QAction *a : m_zoomGroup->actions()) a->setChecked(a->data().toInt() == zoom);
    }
    m_suspendAction->setText(m_daemon->isSuspended() ? tr("Resume") : tr("Suspend (release the ports)"));
}

QIcon TrayIcon::makeIcon(IconState state)
{
    QColor dot;
    switch (state) {
    case IconState::Idle:    dot = QColor(0x9E, 0x9E, 0x9E); break;
    case IconState::Dongles: dot = QColor(0x1E, 0x88, 0xE5); break;
    case IconState::Active:  dot = QColor(0x43, 0xA0, 0x47); break;
    case IconState::Warning: dot = QColor(0xFD, 0xD8, 0x35); break;
    }

    const QPixmap base(QStringLiteral(":/icons/openfire.png"));
    QIcon icon;
    for (int size : { 16, 22, 24, 32, 48, 64 }) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(pm.rect(), base);
        // EN: coloured dot in the lower right corner, with a dark ring so it shows on any panel.
        // IT: punto colorato nell'angolo in basso a destra, con un bordo scuro per vederlo su ogni pannello.
        const qreal d = size * 0.5;
        const QRectF r(size - d - 0.5, size - d - 0.5, d, d);
        p.setPen(QPen(QColor(0, 0, 0, 200), qMax(1.0, size / 16.0)));
        p.setBrush(dot);
        p.drawEllipse(r);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}
