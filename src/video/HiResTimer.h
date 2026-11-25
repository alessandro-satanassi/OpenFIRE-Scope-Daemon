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

#ifndef OFD_HIRESTIMER_H
#define OFD_HIRESTIMER_H

#include <cstdint>

// EN: High resolution sleep for the encoder thread, one object per thread.
//     Windows 10 1803 and later: CreateWaitableTimerExW with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION; before that,
//     timeBeginPeriod(1) with a normal timer and timeEndPeriod at the end. Linux: clock_nanosleep with
//     CLOCK_MONOTONIC and absolute time. macOS: mach_wait_until.
// IT: Attesa ad alta risoluzione per il thread dell'encoder, un oggetto per thread.
//     Windows 10 1803 e successivi: CreateWaitableTimerExW con CREATE_WAITABLE_TIMER_HIGH_RESOLUTION; prima,
//     timeBeginPeriod(1) con un timer normale e timeEndPeriod alla fine. Linux: clock_nanosleep con
//     CLOCK_MONOTONIC e tempo assoluto. macOS: mach_wait_until.
class HiResTimer
{
public:
    HiResTimer();
    ~HiResTimer();
    HiResTimer(const HiResTimer &) = delete;
    HiResTimer &operator=(const HiResTimer &) = delete;

    // EN: monotonic microseconds (std::chrono::steady_clock). / IT: microsecondi monotoni (std::chrono::steady_clock).
    static int64_t nowUs();
    // EN: sleeps until nowUs() >= targetUs; returns at once if it is already past.
    // IT: dorme finche' nowUs() >= targetUs; torna subito se e' gia' passato.
    void sleepUntil(int64_t targetUs);

private:
    struct Impl;
    Impl *d;
};

#endif // OFD_HIRESTIMER_H
