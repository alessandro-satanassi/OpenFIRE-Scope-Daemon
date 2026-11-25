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

#ifndef OFD_AUTOSTART_H
#define OFD_AUTOSTART_H

#include <QCoreApplication>
#include <QString>

// EN: Start at login of the user, kept by the system and not in the INI: Windows HKCU Run key,
//     Linux ~/.config/autostart/*.desktop (the AppImage file if started from it), macOS LaunchAgent.
//     The program starts with the tray icon only, as when it is started by hand.
// IT: Avvio all'accesso dell'utente, tenuto dal sistema e non nell'INI: Windows chiave Run di HKCU,
//     Linux ~/.config/autostart/*.desktop (il file AppImage se avviato da li'), macOS LaunchAgent.
//     Il programma parte con la sola icona nell'area di notifica, come quando si avvia a mano.
class Autostart
{
    Q_DECLARE_TR_FUNCTIONS(Autostart)

public:
    static bool isEnabled();
    static bool setEnabled(bool on, QString *error);
    // EN: at the start of the window: if the program started at login no longer exists (moved, new AppImage),
    //     the entry points to this one. / IT: all'avvio della finestra: se il programma avviato all'accesso non
    //     esiste piu' (spostato, nuova AppImage), la voce punta a questo.
    static void refresh();

private:
    static QString program();         // EN: this program / IT: questo programma
    static QString storedProgram();   // EN: program of the entry, empty = none / IT: programma della voce, vuoto = nessuna
};

#endif // OFD_AUTOSTART_H
