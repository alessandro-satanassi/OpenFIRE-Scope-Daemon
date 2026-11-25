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

#include "video/HiResTimer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // EN: CreateWaitableTimerExW needs Vista or later / IT: CreateWaitableTimerExW richiede Vista o successivi
#endif
#include <windows.h>
#include <mmsystem.h>

#include <chrono>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

struct HiResTimer::Impl {
    HANDLE timer = nullptr;
    bool periodSet = false;   // EN: timeBeginPeriod(1) used (before Windows 10 1803) / IT: usato timeBeginPeriod(1) (prima di Windows 10 1803)
};

HiResTimer::HiResTimer()
    : d(new Impl)
{
    d->timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!d->timer) {
        d->periodSet = timeBeginPeriod(1) == TIMERR_NOERROR;
        d->timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
}

HiResTimer::~HiResTimer()
{
    if (d->timer) CloseHandle(d->timer);
    if (d->periodSet) timeEndPeriod(1);
    delete d;
}

int64_t HiResTimer::nowUs()
{
    using namespace std::chrono;
    return int64_t(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

void HiResTimer::sleepUntil(int64_t targetUs)
{
    const int64_t left = targetUs - nowUs();
    if (left <= 0) return;
    if (d->timer) {
        LARGE_INTEGER due;
        due.QuadPart = -left * 10;   // EN: relative, in 100 ns units / IT: relativo, in unita' da 100 ns
        if (SetWaitableTimer(d->timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(d->timer, INFINITE);
            return;
        }
    }
    Sleep(DWORD((left + 999) / 1000));
}
