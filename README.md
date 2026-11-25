# OpenFIRE Scope Daemon

[English](#english-version) · [Italiano](#versione-italiana)

<a id="english-version"></a>

### Video from the PC to the OpenFIRE scopes.

The daemon runs in the notification area (the menu bar on macOS). It receives the video of each player from the games over UDP on 127.0.0.1 (or, on Windows, from a screen capture), encodes it and sends it through the USB dongles to the round display of the OpenFIRE scopes. Up to 4 players.

> **Status: in development.** This version is the skeleton (phase 1): tray icon, window, settings, log and single instance. Dongles, UDP, encoder and screen capture arrive in the next phases.

## Systems
 - Windows 10 (1809) or later, 64 bit.
 - Linux x86_64 and ARM64 (AppImage), X11 and Wayland.
 - macOS 12 or later (Apple Silicon and Intel).

## Command line
| Option | Meaning |
|---|---|
| `--headless` | no tray icon and no window (tests, UDP-only use); the log goes to the terminal |
| `--discovery-port N` | UDP discovery port for this run only |
| `--no-capture` | no screen capture for this run only |
| `--log FILE` | log file for this run only |

Only one daemon runs per user: starting it again opens the window of the running one.

## Settings
The settings are in an INI file:
 - Windows: `%APPDATA%\OpenFIRE\OpenFIRE-Scope-Daemon.ini`
 - Linux and macOS: `~/.config/OpenFIRE/OpenFIRE-Scope-Daemon.ini`

## Building
Qt 6.4 or later (Core, Gui, Widgets, Network, SerialPort, LinguistTools), CMake 3.21 or later, a C++17 compiler.
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```
The workflows in `.github/workflows` build the Windows packages, the Linux AppImages and the macOS DMG.

## License
GNU General Public License v3.0, see [LICENSE](LICENSE).

---

<a id="versione-italiana"></a>

### Video dal PC ai mirini OpenFIRE.

Il demone gira nell'area di notifica (nella barra dei menu su macOS). Riceve il video di ogni giocatore dai giochi via UDP su 127.0.0.1 (oppure, su Windows, da una cattura dello schermo), lo codifica e lo manda tramite i dongle USB al display rotondo dei mirini OpenFIRE. Fino a 4 giocatori.

> **Stato: in sviluppo.** Questa versione è lo scheletro (fase 1): icona nell'area di notifica, finestra, impostazioni, registro e istanza unica. Dongle, UDP, encoder e cattura dello schermo arrivano nelle fasi successive.

## Sistemi
 - Windows 10 (1809) o successivo, 64 bit.
 - Linux x86_64 e ARM64 (AppImage), X11 e Wayland.
 - macOS 12 o successivo (Apple Silicon e Intel).

## Riga di comando
| Opzione | Significato |
|---|---|
| `--headless` | nessuna icona e nessuna finestra (prove, uso solo UDP); il registro va nel terminale |
| `--discovery-port N` | porta UDP di discovery solo per questo avvio |
| `--no-capture` | nessuna cattura dello schermo solo per questo avvio |
| `--log FILE` | file di registro solo per questo avvio |

Gira un solo demone per utente: avviarlo di nuovo apre la finestra di quello già attivo.

## Impostazioni
Le impostazioni sono in un file INI:
 - Windows: `%APPDATA%\OpenFIRE\OpenFIRE-Scope-Daemon.ini`
 - Linux e macOS: `~/.config/OpenFIRE/OpenFIRE-Scope-Daemon.ini`

## Compilazione
Qt 6.4 o successivo (Core, Gui, Widgets, Network, SerialPort, LinguistTools), CMake 3.21 o successivo, un compilatore C++17.
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```
I workflow in `.github/workflows` creano i pacchetti per Windows, le AppImage per Linux e il DMG per macOS.

## Licenza
GNU General Public License v3.0, vedi [LICENSE](LICENSE).
