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

#include "capture/AimInput.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QRegularExpression>
#include <map>
#include <string>
#include <vector>

#include "app/Log.h"
#include "capture/CaptureGeometry.h"

namespace {
const wchar_t *const kWindowClass = L"OpenFIREScopeDaemonAim";
constexpr UINT kQuitMessage = WM_APP + 1;
constexpr UINT kMapChangedMessage = WM_APP + 2;
} // namespace

// EN: State of the hidden window, only in the Raw Input thread. / IT: stato della finestra nascosta, solo nel thread di Raw Input.
struct AimInputWindow {
    struct Device {
        quint16 vid = 0;
        quint16 pid = 0;
        bool known = false;   // EN: VID and PID read from the name / IT: VID e PID letti dal nome
        int player = 0;       // 0 = not a lightgun of the table / 0 = non e' una lightgun della tabella
        bool logged = false;  // EN: flags already in the log / IT: flag gia' nel registro
    };

    AimInput *owner = nullptr;
    QVector<LightgunMapEntry> map;
    std::map<HANDLE, Device> devices;
    HANDLE first[Settings::Players] = {};   // EN: the lightgun that counts for each player / IT: la lightgun che conta per ogni giocatore

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    Device readDevice(HANDLE h) const;
    int playerOf(const Device &d) const;
    void refreshMap();
    void deviceAdded(HANDLE h);
    void deviceRemoved(HANDLE h);
    void updatePlayers();
    void input(HRAWINPUT handle);
};

AimInputWindow::Device AimInputWindow::readDevice(HANDLE h) const
{
    Device d;
    UINT chars = 0;
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, nullptr, &chars) != 0 || chars == 0) return d;
    std::wstring name(chars, L'\0');
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, name.data(), &chars) == UINT(-1)) return d;
    // EN: USB: "...VID_F143&PID_0001...", Bluetooth: "...VID&0002f143_PID&0001...".
    // IT: USB: "...VID_F143&PID_0001...", Bluetooth: "...VID&0002f143_PID&0001...".
    static const QRegularExpression re(QStringLiteral("VID[_&](?:[0-9A-F]{4})?([0-9A-F]{4})[_&]PID[_&]([0-9A-F]{4})"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(QString::fromWCharArray(name.c_str()));
    if (!m.hasMatch()) return d;
    d.vid = quint16(m.captured(1).toUInt(nullptr, 16));
    d.pid = quint16(m.captured(2).toUInt(nullptr, 16));
    d.known = true;
    d.player = playerOf(d);
    return d;
}

int AimInputWindow::playerOf(const Device &d) const
{
    if (!d.known) return 0;
    for (const LightgunMapEntry &e : map)
        if (e.vid == d.vid && e.pid == d.pid && e.player >= 1 && e.player <= Settings::Players) return e.player;
    return 0;
}

void AimInputWindow::refreshMap()
{
    {
        QMutexLocker lock(&owner->m_mutex);
        if (!owner->m_mapChanged) return;
        owner->m_mapChanged = false;
        map = owner->m_map;
    }
    for (auto &entry : devices) entry.second.player = playerOf(entry.second);
    updatePlayers();
}

void AimInputWindow::deviceAdded(HANDLE h)
{
    if (devices.count(h)) return;
    devices[h] = readDevice(h);
    updatePlayers();
}

void AimInputWindow::deviceRemoved(HANDLE h)
{
    if (devices.erase(h)) updatePlayers();
}

void AimInputWindow::updatePlayers()
{
    // EN: the first lightgun of a player stays while it is connected and on that player; a second one is only
    //     reported. When the first goes away, the next one to move takes its place.
    // IT: la prima lightgun di un giocatore resta finche' e' collegata e su quel giocatore; una seconda si
    //     segnala soltanto. Quando la prima se ne va, prende il suo posto la prossima che si muove.
    int count[Settings::Players] = {};
    for (const auto &entry : devices)
        if (entry.second.player) ++count[entry.second.player - 1];
    QMutexLocker lock(&owner->m_mutex);
    for (int p = 0; p < Settings::Players; ++p) {
        if (first[p]) {
            auto it = devices.find(first[p]);
            if (it == devices.end() || it->second.player != p + 1) {
                first[p] = nullptr;
                owner->m_points[p] = AimInput::Point();
            }
        }
        owner->m_duplicate[p] = count[p] > 1;
    }
}

void AimInputWindow::input(HRAWINPUT handle)
{
    RAWINPUT raw;
    UINT size = sizeof(raw);
    if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (raw.header.dwType != RIM_TYPEMOUSE || !raw.header.hDevice) return;   // EN: injected input has no device / IT: l'input simulato non ha dispositivo

    refreshMap();
    auto it = devices.find(raw.header.hDevice);
    if (it == devices.end()) {
        deviceAdded(raw.header.hDevice);
        it = devices.find(raw.header.hDevice);
    }
    const int player = it->second.player;
    if (player == 0) return;
    const RAWMOUSE &m = raw.data.mouse;
    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) return;

    Device &dev = it->second;
    if (!dev.logged) {
        dev.logged = true;
        const QString id = QStringLiteral("%1:%2").arg(dev.vid, 4, 16, QLatin1Char('0')).arg(dev.pid, 4, 16, QLatin1Char('0')).toUpper();
        const QString flags = QStringLiteral("0x%1").arg(uint(m.usFlags), 2, 16, QLatin1Char('0'));
        Log::info((m.usFlags & MOUSE_VIRTUAL_DESKTOP)
                      ? AimInput::tr("P%1: lightgun %2, absolute positions on the virtual desktop (flags %3)").arg(player).arg(id, flags)
                      : AimInput::tr("P%1: lightgun %2, absolute positions on the primary monitor (flags %3)").arg(player).arg(id, flags));
    }
    // EN: 0-1 on the screen the lightgun aims at: the capture puts it on the game monitor.
    // IT: 0-1 sullo schermo che la lightgun inquadra: la cattura lo porta sul monitor del gioco.
    AimInput::Point point;
    point.valid = true;
    point.x = CaptureGeometry::absoluteToUnit(m.lLastX);
    point.y = CaptureGeometry::absoluteToUnit(m.lLastY);

    QMutexLocker lock(&owner->m_mutex);
    HANDLE &f = first[player - 1];
    if (!f) f = raw.header.hDevice;
    if (f != raw.header.hDevice) return;   // EN: second lightgun on the same player / IT: seconda lightgun sullo stesso giocatore
    owner->m_points[player - 1] = point;
}

LRESULT CALLBACK AimInputWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto *self = reinterpret_cast<AimInputWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        switch (msg) {
        case WM_INPUT:
            self->input(reinterpret_cast<HRAWINPUT>(lParam));
            break;   // EN: DefWindowProc does the cleanup / IT: DefWindowProc fa la pulizia
        case WM_INPUT_DEVICE_CHANGE:
            self->refreshMap();
            if (wParam == GIDC_ARRIVAL) self->deviceAdded(reinterpret_cast<HANDLE>(lParam));
            else if (wParam == GIDC_REMOVAL) self->deviceRemoved(reinterpret_cast<HANDLE>(lParam));
            return 0;
        case kQuitMessage:
            PostQuitMessage(0);
            return 0;
        case kMapChangedMessage:
            self->refreshMap();
            return 0;
        default:
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

AimInput::AimInput(QObject *parent)
    : QThread(parent)
{
}

AimInput::~AimInput()
{
    stopInput();
}

bool AimInput::supported()
{
    return true;
}

void AimInput::setMap(const QVector<LightgunMapEntry> &map)
{
    {
        QMutexLocker lock(&m_mutex);
        m_map = map;
        m_mapChanged = true;
    }
    // EN: the new table is used at once, not at the next movement. / IT: la tabella nuova vale subito, non al prossimo movimento.
    if (void *w = m_window.load()) PostMessageW(static_cast<HWND>(w), kMapChangedMessage, 0, 0);
}

AimInput::Point AimInput::aim(int player) const
{
    if (player < 1 || player > Settings::Players) return Point();
    QMutexLocker lock(&m_mutex);
    return m_points[player - 1];
}

bool AimInput::duplicate(int player) const
{
    if (player < 1 || player > Settings::Players) return false;
    QMutexLocker lock(&m_mutex);
    return m_duplicate[player - 1];
}

void AimInput::stopInput()
{
    m_stop.store(true);
    if (void *w = m_window.load()) PostMessageW(static_cast<HWND>(w), kQuitMessage, 0, 0);
    wait();
}

void AimInput::run()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AimInputWindow::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Log::warn(tr("Aim point: window not created (error %1)").arg(GetLastError()));
        return;
    }
    // EN: message-only window: never shown, it only receives Raw Input. / IT: finestra di soli messaggi: mai mostrata, riceve solo Raw Input.
    const HWND hwnd = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd) {
        Log::warn(tr("Aim point: window not created (error %1)").arg(GetLastError()));
        return;
    }
    AimInputWindow state;
    state.owner = this;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    state.refreshMap();

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;   // generic desktop
    rid.usUsage = 0x02;       // mouse
    rid.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid.hwndTarget = hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        Log::warn(tr("Aim point: Raw Input not available (error %1)").arg(GetLastError()));
        DestroyWindow(hwnd);
        return;
    }

    // EN: the devices already connected: Windows usually notifies them too, but it is not documented.
    // IT: i dispositivi gia' collegati: di solito Windows li notifica anche lui, ma non e' documentato.
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == 0 && count > 0) {
        std::vector<RAWINPUTDEVICELIST> list(count);
        const UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
        if (got != UINT(-1))
            for (UINT i = 0; i < got; ++i)
                if (list[i].dwType == RIM_TYPEMOUSE) state.deviceAdded(list[i].hDevice);
    }

    m_window.store(hwnd);
    state.refreshMap();   // EN: a table set before the line above / IT: una tabella impostata prima della riga sopra
    if (!m_stop.load()) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    }
    m_window.store(nullptr);

    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    DestroyWindow(hwnd);
}
