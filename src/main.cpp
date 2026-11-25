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

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "app/Daemon.h"
#include "app/Log.h"
#include "app/Settings.h"
#include "app/SingleInstance.h"
#include "ui/MainWindow.h"
#include "ui/TrayIcon.h"

namespace {

// EN: set by SIGINT/SIGTERM (headless); a timer in the main thread reads it. / IT: impostato da SIGINT/SIGTERM (senza finestra); lo legge un timer nel thread principale.
std::atomic<bool> g_quitRequested{ false };

void onQuitSignal(int)
{
    g_quitRequested.store(true);
}

#ifdef Q_OS_WIN
BOOL WINAPI onConsoleCtrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_quitRequested.store(true);
        return TRUE;
    }
    return FALSE;
}
#endif

#ifdef Q_OS_WIN
// EN: without a window Qt does not declare the DPI awareness: the aim points (Raw Input, GetCursorPos) and the
//     screen must be in real pixels also with the Windows zoom at 125% or 150%, and DuplicateOutput1 needs it.
//     Through GetProcAddress, so it does not depend on the version of the headers.
// IT: senza finestra Qt non dichiara la consapevolezza DPI: i punti di mira (Raw Input, GetCursorPos) e lo
//     schermo devono essere in pixel reali anche con lo zoom di Windows al 125% o al 150%, e DuplicateOutput1
//     ne ha bisogno. Tramite GetProcAddress, cosi' non dipende dalla versione delle intestazioni.
void setPerMonitorDpiAware()
{
    using SetContext = BOOL(WINAPI *)(HANDLE);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    const auto set = reinterpret_cast<SetContext>(reinterpret_cast<void *>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (set) set(reinterpret_cast<HANDLE>(INT_PTR(-4)));   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

bool hasStdHandle(DWORD which)
{
    const HANDLE h = GetStdHandle(which);
    return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}
#endif

// EN: Windows: the program has no console of its own. An output already given by the caller (a file,
//     a pipe) is kept; only an output without a handle goes to the console the program was started
//     from, if any. Other systems: nothing to do.
// IT: Windows: il programma non ha una console sua. Un'uscita gia' data da chi lo avvia (un file,
//     una pipe) resta quella; solo un'uscita senza handle va sulla console da cui e' stato avviato,
//     se c'e'. Altri sistemi: niente da fare.
void ensureConsoleOutput()
{
#ifdef Q_OS_WIN
    static bool done = false;
    if (done) return;
    done = true;
    const bool out = hasStdHandle(STD_OUTPUT_HANDLE);
    const bool err = hasStdHandle(STD_ERROR_HANDLE);
    // EN: attach even when both outputs are redirected: Ctrl+C / Ctrl+Break reach only the processes
    //     attached to the console.
    // IT: si collega anche con entrambe le uscite reindirizzate: Ctrl+C / Ctrl+Break arrivano solo ai
    //     processi collegati alla console.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE *f = nullptr;
    if (!out) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!err) freopen_s(&f, "CONOUT$", "w", stderr);
#endif
}

bool hasArgument(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

void loadTranslations(QCoreApplication *app)
{
    const QLocale locale = QLocale::system();

    auto *own = new QTranslator(app);
    if (own->load(locale, QStringLiteral("ScopeDaemon"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(own);
    else
        delete own;

    // EN: standard Qt texts (buttons of the dialogs). windeployqt makes a single qt_xx.qm; a normal Qt
    //     installation also has qtbase_xx.qm.
    // IT: testi standard di Qt (pulsanti delle finestre di dialogo). windeployqt crea un unico qt_xx.qm;
    //     un'installazione normale di Qt ha anche qtbase_xx.qm.
    const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    auto *qt = new QTranslator(app);
    if (qt->load(locale, QStringLiteral("qt"), QStringLiteral("_"), dir)
        || qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), dir))
        QCoreApplication::installTranslator(qt);
    else
        delete qt;
}

} // namespace

int main(int argc, char *argv[])
{
    // EN: the kind of application must be chosen before the parser runs. / IT: il tipo di applicazione va scelto prima del parser.
    const bool headless = hasArgument(argc, argv, "--headless");
    // EN: help and version also work without a display. / IT: aiuto e versione funzionano anche senza schermo.
    const bool textOnly = headless || hasArgument(argc, argv, "--help") || hasArgument(argc, argv, "-h")
                          || hasArgument(argc, argv, "--help-all") || hasArgument(argc, argv, "--version")
                          || hasArgument(argc, argv, "-v");

    QCoreApplication::setOrganizationName(QStringLiteral("OpenFIRE"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenFIRE-Scope-Daemon"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OFD_VERSION));

    std::unique_ptr<QCoreApplication> app;
    if (textOnly) {
        ensureConsoleOutput();
#ifdef Q_OS_WIN
        SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
        if (headless) setPerMonitorDpiAware();
#endif
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        app = std::make_unique<QApplication>(argc, argv);
#ifdef Q_OS_WIN
        QApplication::setStyle(QStringLiteral("fusion"));
#endif
        QApplication::setApplicationDisplayName(QStringLiteral("OpenFIRE Scope Daemon"));
        QApplication::setQuitOnLastWindowClosed(false);
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/openfire.png")));
    }

    loadTranslations(app.get());

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Video from the PC to the OpenFIRE scopes."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption headlessOption(QStringLiteral("headless"),
        QCoreApplication::translate("main", "No tray icon and no window."));
    const QCommandLineOption discoveryOption(QStringLiteral("discovery-port"),
        QCoreApplication::translate("main", "UDP discovery port for this run."), QStringLiteral("N"));
    const QCommandLineOption noCaptureOption(QStringLiteral("no-capture"),
        QCoreApplication::translate("main", "No screen capture for this run."));
    const QCommandLineOption logOption(QStringLiteral("log"),
        QCoreApplication::translate("main", "Log file for this run."), QStringLiteral("FILE"));
    const QCommandLineOption clipOption(QStringLiteral("play-clip"),
        QCoreApplication::translate("main", "Test: sends this clip (.ofclip) in a loop to every paired scope."), QStringLiteral("FILE"));
    parser.addOption(headlessOption);
    parser.addOption(discoveryOption);
    parser.addOption(noCaptureOption);
    parser.addOption(logOption);
    parser.addOption(clipOption);
    parser.process(*app);

    SingleInstance instance;
    if (!instance.acquire()) {
        ensureConsoleOutput();
        std::fprintf(stderr, "%s\n", qPrintable(QCoreApplication::translate("main", "OpenFIRE Scope Daemon is already running.")));
        return 0;
    }

    Settings settings;
    if (parser.isSet(discoveryOption)) {
        bool ok = false;
        const int port = parser.value(discoveryOption).toInt(&ok);
        if (!ok || port < 1024 || port > 65535) {
            ensureConsoleOutput();
            std::fprintf(stderr, "%s\n", qPrintable(QCoreApplication::translate("main", "Invalid discovery port: %1").arg(parser.value(discoveryOption))));
            return 2;
        }
        settings.overrideDiscoveryPort(quint16(port));
    }
    if (parser.isSet(noCaptureOption)) settings.overrideCaptureOff();
    if (parser.isSet(logOption)) settings.overrideLogFile(parser.value(logOption));

    Log::instance().setEcho(headless);

    Daemon daemon(&settings);
    if (parser.isSet(clipOption) && !daemon.setClipFile(parser.value(clipOption))) {
        ensureConsoleOutput();
        std::fprintf(stderr, "%s\n", qPrintable(QCoreApplication::translate("main", "The test clip cannot be used: see the log.")));
        if (headless) return 2;
    }

    std::unique_ptr<MainWindow> window;
    std::unique_ptr<TrayIcon> tray;
    QTimer quitPoll;

    if (headless) {
        std::signal(SIGINT, onQuitSignal);
        std::signal(SIGTERM, onQuitSignal);
        QObject::connect(&quitPoll, &QTimer::timeout, app.get(), []() {
            if (g_quitRequested.load()) QCoreApplication::quit();
        });
        quitPoll.start(200);
    } else {
        const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
        window = std::make_unique<MainWindow>(&daemon, trayAvailable);
        QObject::connect(&instance, &SingleInstance::showRequested, window.get(), &MainWindow::showAndRaise);
        if (trayAvailable) {
            tray = std::make_unique<TrayIcon>(&daemon);
            QObject::connect(tray.get(), &TrayIcon::showWindowRequested, window.get(), &MainWindow::showAndRaise);
            QObject::connect(tray.get(), &TrayIcon::quitRequested, app.get(), &QCoreApplication::quit);
        } else {
            // EN: no notification area (GNOME without extension): the window opens at start.
            // IT: nessuna area di notifica (GNOME senza estensione): la finestra si apre all'avvio.
            window->show();
        }
        QObject::connect(window.get(), &MainWindow::quitRequested, app.get(), &QCoreApplication::quit);
    }

    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, &daemon, &Daemon::stop);
    daemon.start();
    if (!headless && !tray) Log::warn(QCoreApplication::translate("main", "No notification area: the window stays open"));

    const int code = app->exec();
    tray.reset();
    window.reset();
    return code;
}
