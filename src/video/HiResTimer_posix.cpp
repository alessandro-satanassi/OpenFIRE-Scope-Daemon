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

#include <cerrno>
#include <chrono>
#include <ctime>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

struct HiResTimer::Impl {
#if defined(__APPLE__)
    mach_timebase_info_data_t timebase{};
#endif
};

HiResTimer::HiResTimer()
    : d(new Impl)
{
#if defined(__APPLE__)
    mach_timebase_info(&d->timebase);
#endif
}

HiResTimer::~HiResTimer()
{
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
#if defined(__APPLE__)
    // EN: microseconds -> mach ticks / IT: microsecondi -> tick mach
    const uint64_t ns = uint64_t(left) * 1000u;
    const uint64_t ticks = d->timebase.numer ? ns * d->timebase.denom / d->timebase.numer : ns;
    mach_wait_until(mach_absolute_time() + ticks);
#else
    // EN: absolute time on CLOCK_MONOTONIC: an interrupted sleep goes on to the same instant.
    // IT: tempo assoluto su CLOCK_MONOTONIC: un'attesa interrotta prosegue fino allo stesso istante.
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    int64_t ns = int64_t(t.tv_nsec) + left * 1000;
    t.tv_sec += time_t(ns / 1000000000);
    t.tv_nsec = long(ns % 1000000000);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr) == EINTR) {
    }
#endif
}
