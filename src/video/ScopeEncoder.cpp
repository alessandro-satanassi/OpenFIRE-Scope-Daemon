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

#include "video/ScopeEncoder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace {
constexpr int W = OF_SCOPE_W;
constexpr int H = OF_SCOPE_H;
constexpr int Strips = OF_SCOPE_STRIPS;
constexpr int Mcus = 15;              // EN: MCU columns per slice / IT: colonne di MCU per fascia
constexpr double Cx = 119.5, Cy = 119.5;
constexpr double RVisible = 120.0;    // EN: pixel shown by the GC9A01 / IT: pixel mostrato dal GC9A01
constexpr double RMcu = 120.5;        // EN: an MCU is coded if it touches this circle / IT: un MCU si codifica se tocca questo cerchio
constexpr double REdge = 118.5;       // EN: edge read for the outside pixels (as jfmt.py) / IT: bordo letto per i pixel esterni (come jfmt.py)
constexpr int MaxCoef = 1023;         // EN: baseline limit: DC categories 0-11, AC 1-10 / IT: limite baseline: categorie DC 0-11, AC 1-10

const uint8_t ZZ[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

// EN: K.1 luminance and K.2 chrominance, natural order. / IT: K.1 luminanza e K.2 crominanza, ordine naturale.
const uint8_t QLum[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,  12, 12, 14, 19, 26, 58, 60, 55,  14, 13, 16, 24, 40, 57, 69, 56,  14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77,  24, 35, 55, 64, 81,104,113, 92,  49, 64, 78, 87,103,121,120,101,  72, 92, 95, 98,112,100,103, 99 };
const uint8_t QChr[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,  18, 21, 26, 66, 99, 99, 99, 99,  24, 26, 56, 99, 99, 99, 99, 99,  47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,  99, 99, 99, 99, 99, 99, 99, 99,  99, 99, 99, 99, 99, 99, 99, 99,  99, 99, 99, 99, 99, 99, 99, 99 };

// EN: K.3-K.6: number of codes per length (1-16) and values. / IT: K.3-K.6: numero di codici per lunghezza (1-16) e valori.
const uint8_t DcLumBits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
const uint8_t DcChrBits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
const uint8_t DcVals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
const uint8_t AcLumBits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
const uint8_t AcLumVals[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };
const uint8_t AcChrBits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
const uint8_t AcChrVals[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };

// EN: AAN scale factors of the float DCT (IJG jfdctflt). / IT: fattori di scala AAN della DCT in virgola mobile (IJG jfdctflt).
const double AanScale[8] = { 1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100, 0.275899379 };

std::once_flag g_tablesOnce;
void initCategory();
bool g_coded[Strips][Mcus];
float g_norm[64];   // EN: AAN output -> orthonormal DCT, zigzag order / IT: uscita AAN -> DCT ortonormale, ordine zigzag

void buildTables()
{
    for (int sy = 0; sy < Strips; ++sy) {
        for (int mx = 0; mx < Mcus; ++mx) {
            bool coded = false;
            for (int y = sy * 16; y < sy * 16 + 16 && !coded; ++y)
                for (int x = mx * 16; x < mx * 16 + 16 && !coded; ++x)
                    coded = (x - Cx) * (x - Cx) + (y - Cy) * (y - Cy) <= RMcu * RMcu;
            g_coded[sy][mx] = coded;
        }
    }
    for (int k = 0; k < 64; ++k) {
        const int n = ZZ[k];
        g_norm[k] = float(1.0 / (8.0 * AanScale[n / 8] * AanScale[n % 8]));
    }
    initCategory();
}

// EN: once for the whole program, even with several encoders starting together.
// IT: una volta per tutto il programma, anche con piu' encoder che partono insieme.
void initTables()
{
    std::call_once(g_tablesOnce, buildTables);
}

// EN: float AAN forward DCT on 8 values (IJG jfdctflt), in place with a stride. / IT: DCT AAN in avanti su 8 valori (IJG jfdctflt), sul posto con un passo.
inline void aan8(float *d, int step)
{
    const float t0 = d[0 * step] + d[7 * step], t7 = d[0 * step] - d[7 * step];
    const float t1 = d[1 * step] + d[6 * step], t6 = d[1 * step] - d[6 * step];
    const float t2 = d[2 * step] + d[5 * step], t5 = d[2 * step] - d[5 * step];
    const float t3 = d[3 * step] + d[4 * step], t4 = d[3 * step] - d[4 * step];

    float t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
    d[0 * step] = t10 + t11;
    d[4 * step] = t10 - t11;
    const float z1 = (t12 + t13) * 0.707106781f;
    d[2 * step] = t13 + z1;
    d[6 * step] = t13 - z1;

    t10 = t4 + t5;
    t11 = t5 + t6;
    t12 = t6 + t7;
    const float z5 = (t10 - t12) * 0.382683433f;
    const float z2 = 0.541196100f * t10 + z5;
    const float z4 = 1.306562965f * t12 + z5;
    const float z3 = t11 * 0.707106781f;
    const float z11 = t7 + z3, z13 = t7 - z3;
    d[5 * step] = z13 + z2;
    d[3 * step] = z13 - z2;
    d[1 * step] = z11 + z4;
    d[7 * step] = z11 - z4;
}

// EN: block 8x8 from a plane (already level shifted) -> 64 coefficients, zigzag, orthonormal.
// IT: blocco 8x8 da un piano (con livello gia' spostato) -> 64 coefficienti, zigzag, ortonormali.
void dctBlock(const float *plane, int stride, int x0, int y0, float *out)
{
    float b[64];
    for (int r = 0; r < 8; ++r) {
        const float *src = plane + (y0 + r) * stride + x0;
        std::copy(src, src + 8, b + r * 8);
        aan8(b + r * 8, 1);
    }
    for (int c = 0; c < 8; ++c) aan8(b + c, 8);
    for (int k = 0; k < 64; ++k) out[k] = b[ZZ[k]] * g_norm[k];
}

void scaleTable(const uint8_t *base, int q, float *recipZigzag)
{
    q = std::clamp(q, 1, 100);
    const int scale = q < 50 ? 5000 / q : 200 - 2 * q;
    for (int k = 0; k < 64; ++k) {
        const int v = std::clamp((int(base[ZZ[k]]) * scale + 50) / 100, 1, 255);
        recipZigzag[k] = 1.0f / float(v);
    }
}

inline int quantize(float c, float recip)
{
    // EN: rounding half away from zero, without branches. / IT: arrotondamento con la meta' lontano da zero, senza salti.
    const float v = c * recip;
    const int i = std::min(int(std::fabs(v) + 0.5f), MaxCoef);
    return v < 0.0f ? -i : i;
}

// EN: bit length of |v| for |v| < 2048. / IT: numero di bit di |v| per |v| < 2048.
uint8_t g_category[4096];

void initCategory()
{
    for (int v = -2048; v < 2048; ++v) {
        unsigned a = unsigned(v < 0 ? -v : v);
        uint8_t n = 0;
        while (a) {
            ++n;
            a >>= 1;
        }
        g_category[v + 2048] = n;
    }
}

inline int category(int v) { return g_category[v + 2048]; }
} // namespace

int ScopeEncodeResult::averageQ() const
{
    int sum = 0;
    for (int i = 0; i < OF_SCOPE_STRIPS; ++i) sum += sliceQ[i];
    return (sum + OF_SCOPE_STRIPS / 2) / OF_SCOPE_STRIPS;
}

bool ScopeEncoder::mcuCoded(int sy, int mx)
{
    initTables();
    return sy >= 0 && sy < Strips && mx >= 0 && mx < Mcus && g_coded[sy][mx];
}

ScopeEncoder::ScopeEncoder()
{
    initTables();

    m_fillIndex.resize(W * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double dx = x - Cx, dy = y - Cy;
            int index = y * W + x;
            if (dx * dx + dy * dy > RVisible * RVisible) {
                // EN: same line from the centre, on the edge; nearbyint rounds half to even, as numpy.rint.
                // IT: stessa retta dal centro, sul bordo; nearbyint arrotonda alla pari, come numpy.rint.
                const double a = std::atan2(dy, dx);
                const int sx = std::clamp(int(std::nearbyint(Cx + REdge * std::cos(a))), 0, W - 1);
                const int sy = std::clamp(int(std::nearbyint(Cy + REdge * std::sin(a))), 0, H - 1);
                index = sy * W + sx;
            }
            m_fillIndex[y * W + x] = index;
        }
    }

    m_y.resize(W * H);
    m_cb.resize((W / 2) * (H / 2));
    m_cr.resize((W / 2) * (H / 2));
    m_coef.resize(size_t(Blocks) * 64);
    m_small.resize(60 * 60);
    m_prevSmall.resize(60 * 60);

    int block = 0;
    for (int sy = 0; sy < Strips; ++sy) {
        m_sliceFirstBlock.push_back(block);
        for (int mx = 0; mx < Mcus; ++mx)
            if (g_coded[sy][mx]) block += 6;
    }
    m_sliceFirstBlock.push_back(block);

    auto build = [](const uint8_t *bits, const uint8_t *vals, Code *table) {
        int code = 0, k = 0;
        for (int len = 1; len <= 16; ++len) {
            for (int i = 0; i < bits[len - 1]; ++i) {
                table[vals[k]].code = uint16_t(code);
                table[vals[k]].len = uint8_t(len);
                ++code;
                ++k;
            }
            code <<= 1;
        }
    };
    build(DcLumBits, DcVals, m_lum.dc);
    build(DcChrBits, DcVals, m_chr.dc);
    build(AcLumBits, AcLumVals, m_lum.ac);
    build(AcChrBits, AcChrVals, m_chr.ac);
}

void ScopeEncoder::analyze(const uint8_t *rgb)
{
    // EN: luminance of every pixel, the outside ones read at the edge; JFIF in 16-bit fixed point.
    // IT: luminanza di ogni pixel, quelli esterni letti sul bordo; JFIF in virgola fissa a 16 bit.
    constexpr float inv16 = 1.0f / 65536.0f;
    for (int i = 0; i < W * H; ++i) {
        const uint8_t *p = rgb + size_t(m_fillIndex[i]) * 3;
        m_y[i] = float(19595 * p[0] + 38470 * p[1] + 7471 * p[2]) * inv16 - 128.0f;
    }
    // EN: Cb and Cr as the mean of every 2x2 square (the sum of 4 pixels, divided once).
    // IT: Cb e Cr come media di ogni quadrato 2x2 (la somma di 4 pixel, divisa una volta).
    constexpr float inv18 = 1.0f / 262144.0f;
    for (int cy = 0; cy < H / 2; ++cy) {
        for (int cx = 0; cx < W / 2; ++cx) {
            int r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; ++k) {
                const int i = (cy * 2 + (k >> 1)) * W + cx * 2 + (k & 1);
                const uint8_t *p = rgb + size_t(m_fillIndex[i]) * 3;
                r += p[0];
                g += p[1];
                b += p[2];
            }
            m_cb[cy * (W / 2) + cx] = float(-11059 * r - 21709 * g + 32768 * b) * inv18;
            m_cr[cy * (W / 2) + cx] = float(32768 * r - 27439 * g - 5329 * b) * inv18;
        }
    }

    // EN: motion: luminance reduced to 60x60 with 4x4 means, mean absolute difference from the previous frame.
    // IT: movimento: luminanza ridotta a 60x60 con medie 4x4, differenza media assoluta dal frame precedente.
    for (int sy = 0; sy < 60; ++sy) {
        for (int sx = 0; sx < 60; ++sx) {
            float s = 0.0f;
            for (int r = 0; r < 4; ++r) {
                const float *row = &m_y[size_t(sy * 4 + r) * W + sx * 4];
                s += row[0] + row[1] + row[2] + row[3];
            }
            m_small[sy * 60 + sx] = s * (1.0f / 16.0f);
        }
    }
    if (m_havePrev) {
        double d = 0.0;
        for (int i = 0; i < 60 * 60; ++i) d += std::fabs(double(m_small[i]) - double(m_prevSmall[i]));
        m_motion = d / (60.0 * 60.0);
    } else {
        m_motion = 0.0;
    }
    m_small.swap(m_prevSmall);
    m_havePrev = true;

    // EN: DCT of the coded MCUs: 4 Y blocks, Cb, Cr. / IT: DCT degli MCU codificati: 4 blocchi Y, Cb, Cr.
    float *out = m_coef.data();
    for (int sy = 0; sy < Strips; ++sy) {
        for (int mx = 0; mx < Mcus; ++mx) {
            if (!g_coded[sy][mx]) continue;
            const int x = mx * 16, y = sy * 16;
            dctBlock(m_y.data(), W, x, y, out);           out += 64;
            dctBlock(m_y.data(), W, x + 8, y, out);       out += 64;
            dctBlock(m_y.data(), W, x, y + 8, out);       out += 64;
            dctBlock(m_y.data(), W, x + 8, y + 8, out);   out += 64;
            dctBlock(m_cb.data(), W / 2, mx * 8, sy * 8, out); out += 64;
            dctBlock(m_cr.data(), W / 2, mx * 8, sy * 8, out); out += 64;
        }
    }
}

int ScopeEncoder::encodeSlice(int sy, int q, uint8_t *out, int cap, int *nonZero) const
{
    float recipLum[64], recipChr[64];
    scaleTable(QLum, q, recipLum);
    scaleTable(QChr, q, recipChr);

    // EN: the bit writer stays in local variables (writing through a uint8_t pointer would otherwise force them
    //     back to memory at every byte); 32 bits are written at a time. After a flush at most 31 bits wait, and
    //     every step adds at most 27 (DC 9 + 11, AC 16 + 10, ZRL 11): the 64 bits never overflow.
    // IT: lo scrittore di bit resta in variabili locali (scrivere con un puntatore uint8_t altrimenti le
    //     riporterebbe in memoria a ogni byte); si scrivono 32 bit alla volta. Dopo uno svuotamento restano al
    //     massimo 31 bit, e ogni passo ne aggiunge al massimo 27 (DC 9 + 11, AC 16 + 10, ZRL 11): i 64 bit non traboccano.
    uint64_t acc = 0;
    int bits = 0;
    int pos = 0;
    const int limit = cap - 4;
    auto put = [&](uint32_t code, int len) {
        acc = (acc << len) | code;
        bits += len;
    };
    auto flush = [&]() -> bool {
        while (bits >= 32) {
            if (pos > limit) return false;
            bits -= 32;
            const uint32_t w = uint32_t(acc >> bits);
            out[pos] = uint8_t(w >> 24);
            out[pos + 1] = uint8_t(w >> 16);
            out[pos + 2] = uint8_t(w >> 8);
            out[pos + 3] = uint8_t(w);
            pos += 4;
        }
        return true;
    };

    int pred[3] = { 0, 0, 0 };
    int nz = 0;
    const int first = m_sliceFirstBlock[size_t(sy)];
    const int last = m_sliceFirstBlock[size_t(sy) + 1];
    for (int blk = first; blk < last; ++blk) {
        const int inMcu = (blk - first) % 6;
        const int comp = inMcu < 4 ? 0 : inMcu - 3;
        const Huffman &h = comp == 0 ? m_lum : m_chr;
        const float *recip = comp == 0 ? recipLum : recipChr;
        const float *c = &m_coef[size_t(blk) * 64];

        // EN: quantization, keeping only the non-zero AC values with their position.
        // IT: quantizzazione, tenendo solo gli AC non nulli con la loro posizione.
        int val[64];
        int at[64];
        int n = 0;
        const int dc = quantize(c[0], recip[0]);
        for (int k = 1; k < 64; ++k) {
            const int v = quantize(c[k], recip[k]);
            val[n] = v;
            at[n] = k;
            n += (v != 0);
        }
        nz += n + (dc != 0);

        // EN: DC difference (|diff| <= 2046) / IT: differenza DC (|diff| <= 2046)
        const int diff = dc - pred[comp];
        pred[comp] = dc;
        int s = category(diff);
        put(h.dc[s].code, h.dc[s].len);
        if (s) put(uint32_t(diff >= 0 ? diff : diff + (1 << s) - 1) & ((1u << s) - 1u), s);
        if (!flush()) return -1;

        // EN: AC run-length with ZRL and EOB / IT: AC a lunghezza di serie con ZRL ed EOB
        int prev = 0;
        for (int i = 0; i < n; ++i) {
            int run = at[i] - prev - 1;
            prev = at[i];
            while (run > 15) {
                put(h.ac[0xF0].code, h.ac[0xF0].len);
                run -= 16;
                if (!flush()) return -1;
            }
            const int v = val[i];
            s = category(v);
            const Code &code = h.ac[(run << 4) | s];
            put(code.code, code.len);
            put(uint32_t(v >= 0 ? v : v + (1 << s) - 1) & ((1u << s) - 1u), s);
            if (!flush()) return -1;
        }
        if (prev != 63) put(h.ac[0x00].code, h.ac[0x00].len);
        if (!flush()) return -1;
    }
    // EN: end of slice padded with 1, then the last bytes. / IT: fine fascia riempita con 1, poi gli ultimi byte.
    const int pad = (8 - bits % 8) % 8;
    if (pad) put((1u << pad) - 1u, pad);
    while (bits >= 8) {
        if (pos >= cap) return -1;
        bits -= 8;
        out[pos++] = uint8_t(acc >> bits);
    }
    if (nonZero) *nonZero = nz;
    return pos;
}

void ScopeEncoder::encode(int q, ScopeEncodeResult *out) const
{
    q = std::clamp(q, 1, 100);
    out->ok = true;
    out->q = q;
    out->bytes = 0;
    out->nonZero = 0;
    for (int sy = 0; sy < Strips; ++sy) {
        int sq = q;
        int nz = 0;
        int len = encodeSlice(sy, sq, out->data[sy], OF_VIDEO_MAX_DATA, &nz);
        // EN: slice over 1,400 bytes: only its q goes down. / IT: fascia oltre 1.400 byte: si abbassa solo il suo q.
        while (len < 0 && sq > 1) {
            sq = std::max(1, std::min(sq - 1, sq * 85 / 100));
            len = encodeSlice(sy, sq, out->data[sy], OF_VIDEO_MAX_DATA, &nz);
        }
        if (len < 0) {
            out->ok = false;
            len = 0;
        }
        out->sliceQ[sy] = uint8_t(sq);
        out->sliceLen[sy] = len;
        out->bytes += len;
        out->nonZero += nz;
    }
}
