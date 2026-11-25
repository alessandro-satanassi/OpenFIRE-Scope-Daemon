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

#ifndef OFD_MAINWINDOW_H
#define OFD_MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

#include "app/Daemon.h"
#include "app/Log.h"

class QAction;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;
class QTimer;
class PreviewWidget;

// EN: The window: Status (players and other devices), Preview, Log. It only reads the state of the
//     daemon; closing it hides it and the daemon keeps running.
// IT: La finestra: Stato (giocatori e altri dispositivi), Anteprima, Registro. Legge solo lo stato del
//     demone; chiuderla la nasconde e il demone resta attivo.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // EN: trayAvailable = false: closing minimizes instead of hiding. / IT: trayAvailable = false: chiudere riduce a icona invece di nascondere.
    MainWindow(Daemon *daemon, bool trayAvailable, QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void showAndRaise();

signals:
    void quitRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QWidget *buildStatusTab();
    QWidget *buildPreviewTab();
    QWidget *buildLogTab();
    void buildMenus();

    void refreshStatus();
    void refreshDevices();
    void refreshPreview();
    void updatePreviewTimer();
    void appendLog(const Log::Entry &entry);
    void copyLog();
    void openSettings();
    void showAbout();
    void storeGeometry();

    Daemon *m_daemon = nullptr;
    bool m_trayAvailable = false;

    QTabWidget *m_tabs = nullptr;
    QLabel *m_warnings = nullptr;
    QTableWidget *m_players = nullptr;
    QTableWidget *m_devices = nullptr;
    QVector<DeviceStatus> m_shownDevices;
    QCheckBox *m_showPreview = nullptr;
    QCheckBox *m_circle = nullptr;
    PreviewWidget *m_preview[Settings::Players] = {};
    QPlainTextEdit *m_log = nullptr;
    QAction *m_suspendAction = nullptr;
    QLabel *m_statusText = nullptr;

    QTimer *m_statusTimer = nullptr;    // 2 Hz
    QTimer *m_previewTimer = nullptr;   // 15 Hz
};

#endif // OFD_MAINWINDOW_H
