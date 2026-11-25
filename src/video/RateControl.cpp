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

#include "video/RateControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr double MinFps = 30.0;
constexpr double MaxFps = 60.0;
constexpr double FpsSmoothing = 0.25;     // EN: weight of the new value in the moving average / IT: peso del valore nuovo nella media mobile
constexpr double DefaultSlope = 0.02;     // EN: d ln(bytes) / dq before two points are known / IT: d ln(byte) / dq prima di avere due punti
constexpr double MinSlope = 0.004, MaxSlope = 0.15;
constexpr double TargetFill = 0.96;       // EN: aim a bit below the budget / IT: si mira poco sotto il budget
constexpr double GoodFill = 0.92;         // EN: from here a frame that fits is good enough / IT: da qui un frame che sta nel budget va bene
constexpr int CapDownEveryMs = 250;
constexpr int CapUpEveryMs = 500;
constexpr double CpuHz = 240e6;           // EN: ESP32-C5 of the scope / IT: ESP32-C5 del mirino
} // namespace

RateControl::RateControl(const EncoderConfig &config)
    : m_cfg(config)
{
}

double RateControl::chooseFps(double motion, int64_t nowMs)
{
    (void)nowMs;
    double raw = MinFps;
    if (m_cfg.motionHigh > m_cfg.motionLow)
        raw = MinFps + (MaxFps - MinFps) * (motion - m_cfg.motionLow) / (m_cfg.motionHigh - m_cfg.motionLow);
    raw = std::clamp(raw, MinFps, MaxFps);
    m_fpsSmooth = m_fpsSmooth < 0.0 ? raw : m_fpsSmooth + FpsSmoothing * (raw - m_fpsSmooth);
    return std::clamp(std::min({ m_fpsSmooth, m_fpsCap, m_fpsCost }), MinFps, MaxFps);
}

int RateControl::budget(double fps, double capacityKBs) const
{
    fps = std::clamp(fps, MinFps, MaxFps);
    const double profile = m_cfg.profile30 + (m_cfg.profile60 - m_cfg.profile30) * (fps - MinFps) / (MaxFps - MinFps);
    const double usb = 0.8 * capacityKBs * 1000.0 / fps;
    return int(std::min(profile, usb));
}

double RateControl::cycleMs(int nonZero) const
{
    const double instructions = m_cfg.instrPerMcu * ScopeEncoder::CodedMcus + m_cfg.instrPerCoef * nonZero + m_cfg.instrFixed;
    return instructions * m_cfg.cpi / CpuHz * 1000.0 + m_cfg.dmaWaitMs;
}

bool RateControl::fits(const ScopeEncodeResult &r, int budgetBytes) const
{
    return r.ok && r.bytes + HeaderBytes <= budgetBytes && cycleMs(r.nonZero) <= m_cfg.maxCycleMs;
}

ScopeEncodeResult *RateControl::encode(const ScopeEncoder &encoder, int budgetBytes, ScopeEncodeResult *a, ScopeEncodeResult *b)
{
    // EN: kept = best so far: a frame that fits beats one that does not; among those that fit the highest q
    //     wins, among the others the smallest one. cur = buffer for the next try.
    // IT: kept = il migliore finora: un frame che sta nel budget batte uno che non ci sta; tra quelli che ci
    //     stanno vince il q piu' alto, tra gli altri il piu' piccolo. cur = buffer per la prova successiva.
    ScopeEncodeResult *kept = nullptr;
    ScopeEncodeResult *cur = a;
    auto consider = [&]() {
        bool better = !kept;
        if (kept) {
            const bool fc = fits(*cur, budgetBytes), fk = fits(*kept, budgetBytes);
            if (fc != fk) better = fc;
            else if (fc) better = cur->q > kept->q;
            else better = cur->ok && (!kept->ok || cur->bytes < kept->bytes);
        }
        if (better) {
            kept = cur;
            cur = (cur == a) ? b : a;
        }
    };

    // EN: load of a try: the larger of bytes / budget and of the cycle excess on the scope; it fits at <= 1.
    //     The tries keep a bracket (highest q that fits, lowest q that does not) and move in it with the
    //     logarithmic model: ln(load) linear in q.
    // IT: carico di una prova: il maggiore tra byte / budget e l'eccesso del ciclo sul mirino; sta a <= 1.
    //     Le prove tengono un intervallo (q piu' alto che sta, q piu' basso che non sta) e ci si muovono con il
    //     modello logaritmico: ln(carico) lineare in q.
    const double fixedMs = cycleMs(0);
    const double cycleRoom = std::max(1e-3, m_cfg.maxCycleMs - fixedMs);
    auto load = [&](const ScopeEncodeResult &r) {
        const double lb = double(r.bytes + HeaderBytes) / std::max(1, budgetBytes);
        const double lc = (cycleMs(r.nonZero) - fixedMs) / cycleRoom;
        return std::max({ lb, lc, 1e-3 });
    };
    int loQ = 0, hiQ = 101;          // EN: bracket / IT: intervallo
    double loL = 0.0, hiL = 0.0;
    int q = std::clamp(m_q, 1, 100);
    int lastQ = -1;
    double lastL = 0.0;
    m_tries = 0;

    for (int t = 0; t < 3; ++t) {
        encoder.encode(q, cur);
        ++m_tries;
        const bool fit = fits(*cur, budgetBytes);
        const double l = cur->ok ? load(*cur) : 1e9;
        consider();
        if (fit) { loQ = q; loL = l; } else { hiQ = q; hiL = l; }
        if (fit && (l >= GoodFill || q == 100)) break;
        if (hiQ - loQ <= 1) break;

        double slope = DefaultSlope;
        if (lastQ >= 0 && lastQ != q && cur->ok && lastL > 0.0)
            slope = std::clamp((std::log(l) - std::log(lastL)) / double(q - lastQ), MinSlope, MaxSlope);
        int next;
        if (loQ >= 1 && hiQ <= 100 && hiL < 1e8 && hiL > loL) {
            // EN: inside the bracket the model is used, but at least a quarter of it away from both ends: at low q
            //     the size is almost flat (the DC values dominate) and the model alone would stick to one end.
            // IT: dentro l'intervallo si usa il modello, ma ad almeno un quarto da entrambi gli estremi: a q bassi la
            //     dimensione e' quasi piatta (dominano i DC) e il modello da solo si incollerebbe a un estremo.
            const double f = (std::log(TargetFill) - std::log(loL)) / (std::log(hiL) - std::log(loL));
            next = int(std::lround(loQ + (hiQ - loQ) * std::clamp(f, 0.25, 0.75)));
        } else {
            next = int(std::lround(q + (std::log(TargetFill) - std::log(l)) / slope));
            // EN: with one end of the bracket only, the model can jump straight to the other end where the size
            //     is almost flat: going down with a flat slope at most to half of q, going up from q < 15 (the DC
            //     values dominate) at most to q + 25.
            // IT: con un solo estremo dell'intervallo il modello puo' saltare dritto all'altro estremo dove la
            //     dimensione e' quasi piatta: in discesa con pendenza piatta al massimo a meta' di q, in salita da
            //     q < 15 (dominano i DC) al massimo a q + 25.
            if (next < q && slope <= MinSlope) next = std::max(next, q / 2);
            else if (next > q && q < 15) next = std::min(next, q + 25);
        }
        next = std::clamp(next, loQ + 1, hiQ - 1);
        next = std::clamp(next, 1, 100);
        if (next == q) break;
        lastQ = q;
        lastL = l;
        q = next;
    }

    // EN: 4th try. Nothing fits: from the lowest q that does not fit, reduced in proportion to its excess.
    //     The best one that fits is well below the budget: halfway between it and the lowest q that does not fit.
    // IT: quarta prova. Non sta nulla: dal q piu' basso che non sta, ridotto in proporzione al suo eccesso.
    //     Il migliore che sta resta molto sotto il budget: a meta' tra lui e il q piu' basso che non sta.
    if (hiQ <= 100 && hiQ > 1 && hiQ - loQ > 1 && (loQ == 0 || loL < GoodFill)) {
        int q4;
        if (loQ == 0) {
            const double excess = hiL < 1e8 ? hiL : 2.0;
            q4 = std::clamp(int(std::floor(hiQ / excess)), 1, hiQ - 1);
        } else {
            q4 = (loQ + hiQ) / 2;
        }
        encoder.encode(q4, cur);
        ++m_tries;
        consider();
    }
    m_q = kept->q;
    return kept;
}

void RateControl::frameDone(const ScopeEncodeResult &result, int64_t nowMs)
{
    // EN: first the quality goes down, then the frequency: the fps cap moves only when q stays at the edges.
    // IT: prima cala la qualita', poi la frequenza: il tetto di fps si muove solo quando q resta agli estremi.
    const int q = result.averageQ();
    if (q < m_cfg.qLow && m_fpsCap > MinFps && nowMs - m_capChangedMs >= CapDownEveryMs) {
        m_fpsCap = std::max(MinFps, m_fpsCap - 5.0);
        m_capChangedMs = nowMs;
    } else if (q > m_cfg.qHigh && m_fpsCap < MaxFps && nowMs - m_capChangedMs >= CapUpEveryMs) {
        m_fpsCap = std::min(MaxFps, m_fpsCap + 1.0);
        m_capChangedMs = nowMs;
    }
    // EN: cost model: a cycle longer than 1/f lowers f. / IT: modello di costo: un ciclo piu' lungo di 1/f abbassa f.
    m_fpsCost = std::clamp(1000.0 / cycleMs(result.nonZero), MinFps, MaxFps);
}
