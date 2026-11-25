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

#ifndef OFD_RATECONTROL_H
#define OFD_RATECONTROL_H

#include <cstdint>

#include "video/ScopeEncoder.h"

// EN: Constants of the encoder, from the [encoder] section of the INI (estimates, to be tuned on the bench).
// IT: Costanti dell'encoder, dalla sezione [encoder] dell'INI (stime, da tarare al banco).
struct EncoderConfig {
    double motionLow = 1.5;        // m0: at or below -> 30 fps / m0: fino a qui -> 30 fps
    double motionHigh = 10.0;      // m1: at or above -> 60 fps / m1: da qui in su -> 60 fps
    int profile30 = 15000;         // EN: bytes per frame at 30 fps, headers included / IT: byte per frame a 30 fps, intestazioni comprese
    int profile60 = 8000;          // EN: bytes per frame at 60 fps / IT: byte per frame a 60 fps
    int qLow = 30;                 // EN: mean q below this -> fps cap -5 / IT: q medio sotto questo -> tetto di fps -5
    int qHigh = 50;                // EN: mean q above this -> fps cap +1 / IT: q medio sopra questo -> tetto di fps +1
    double instrPerMcu = 11500.0;  // EN: cost model of the scope / IT: modello di costo del mirino
    double instrPerCoef = 100.0;
    double instrFixed = 660000.0;  // EN: rotation / IT: rotazione
    double cpi = 1.5;
    double dmaWaitMs = 2.0;
    double maxCycleMs = 33.0;      // EN: over this the q goes down / IT: oltre questo si abbassa q
};

// EN: Frequency, budget and q of the frames of one slot ("Frequenza, budget e capacita' USB" of the plan).
//     f from the motion (30-60, moving average), limited by the fps cap (first the quality goes down, then
//     the frequency) and by the cost model of the scope. Budget = min(profile, 0.8 * C / f), with the 180
//     bytes of headers. q: starts from the q of the previous frame, logarithmic model between size and q, at
//     most 3 tries and a 4th one with q reduced in proportion to the excess. Plain C++17.
// IT: Frequenza, budget e q dei frame di uno slot ("Frequenza, budget e capacita' USB" del piano).
//     f dal movimento (30-60, media mobile), limitata dal tetto di fps (prima cala la qualita', poi la
//     frequenza) e dal modello di costo del mirino. Budget = min(profilo, 0,8 * C / f), con i 180 byte di
//     intestazioni. q: parte dal q del frame precedente, modello logaritmico tra dimensione e q, al massimo
//     3 prove e una quarta con q ridotto in proporzione all'eccesso. C++17 semplice.
class RateControl
{
public:
    static constexpr int HeaderBytes = OF_SCOPE_STRIPS * (OF_USB_HEADER + OF_VIDEO_HEADER);   // 180

    explicit RateControl(const EncoderConfig &config);

    double chooseFps(double motion, int64_t nowMs);
    int budget(double fps, double capacityKBs) const;
    // EN: encodes the frame analysed last; a and b are the two work buffers, the result is one of them.
    // IT: codifica il frame analizzato per ultimo; a e b sono i due buffer di lavoro, il risultato e' uno dei due.
    ScopeEncodeResult *encode(const ScopeEncoder &encoder, int budgetBytes, ScopeEncodeResult *a, ScopeEncodeResult *b);
    void frameDone(const ScopeEncodeResult &result, int64_t nowMs);

    double cycleMs(int nonZero) const;   // EN: decode and rotation time on the scope / IT: tempo di decodifica e rotazione sul mirino
    int lastTries() const { return m_tries; }
    double fpsCap() const { return m_fpsCap; }

private:
    bool fits(const ScopeEncodeResult &r, int budgetBytes) const;

    EncoderConfig m_cfg;
    int m_q = 75;
    double m_fpsSmooth = -1.0;
    double m_fpsCap = 60.0;
    double m_fpsCost = 60.0;
    int64_t m_capChangedMs = -1000000;
    int m_tries = 0;
};

#endif // OFD_RATECONTROL_H
