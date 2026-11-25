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

#ifndef OFD_SCOPEENCODER_H
#define OFD_SCOPEENCODER_H

#include <cstdint>
#include <vector>

#include "OpenFIRE_Scope_Protocol.h"

// EN: Result of one encoding: the 15 slices of the scope bitstream, each with its q.
// IT: Risultato di una codifica: le 15 fasce del bitstream del mirino, ognuna con il suo q.
struct ScopeEncodeResult {
    bool ok = false;              // EN: false only if a slice exceeds 1,400 bytes even at q = 1 / IT: false solo se una fascia supera 1.400 byte anche a q = 1
    int q = 0;                    // EN: q asked for the frame / IT: q chiesto per il frame
    int bytes = 0;                // EN: slice data only, without headers / IT: solo i dati delle fasce, senza intestazioni
    int nonZero = 0;              // EN: non-zero coefficients (cost model of the scope) / IT: coefficienti non nulli (modello di costo del mirino)
    uint8_t sliceQ[OF_SCOPE_STRIPS] = {};
    int sliceLen[OF_SCOPE_STRIPS] = {};
    uint8_t data[OF_SCOPE_STRIPS][OF_VIDEO_MAX_DATA];

    int averageQ() const;
};

// EN: Encoder of the scope format ("Specifica video del mirino OpenFIRE", Formato del frame): 15 independent
//     slices of 16 rows, YCbCr JFIF 4:2:0, tables K.1-K.6 of ITU-T T.81, MCUs outside the circle left out, DC
//     predictors reset at every slice, bits from the most significant, end of slice padded with 1, no header,
//     marker or byte stuffing. Plain C++17, no library.
//     analyze() is done once per frame: outside pixels get the colour of the nearest edge pixel, colour
//     conversion, motion and the AAN DCT of the 201 x 6 blocks. encode() can then be called with several q
//     values: it redoes only quantization and Huffman, and lowers the q of a single slice that exceeds 1,400 bytes.
// IT: Encoder del formato del mirino ("Specifica video del mirino OpenFIRE", Formato del frame): 15 fasce
//     indipendenti da 16 righe, YCbCr JFIF 4:2:0, tabelle K.1-K.6 di ITU-T T.81, MCU fuori dal cerchio omessi,
//     predittori DC azzerati a ogni fascia, bit dal piu' significativo, fine fascia riempita con 1, nessuna
//     intestazione, marker o byte stuffing. C++17 semplice, nessuna libreria.
//     analyze() si fa una volta per frame: i pixel esterni prendono il colore del bordo piu' vicino, conversione
//     del colore, movimento e DCT AAN dei 201 x 6 blocchi. encode() si puo' poi chiamare con piu' valori di q:
//     rifa' solo quantizzazione e Huffman, e abbassa il q della sola fascia che supera 1.400 byte.
class ScopeEncoder
{
public:
    static constexpr int CodedMcus = 201;
    static constexpr int Blocks = CodedMcus * 6;

    ScopeEncoder();

    // EN: rgb: RGB888 240x240, rows of 720 bytes. It is only read. / IT: rgb: RGB888 240x240, righe da 720 byte. Si legge soltanto.
    void analyze(const uint8_t *rgb);
    // EN: mean absolute difference of the luminance at 60x60 from the previous frame (0 for the first one).
    // IT: differenza media assoluta della luminanza a 60x60 dal frame precedente (0 per il primo).
    double motion() const { return m_motion; }

    // EN: encodes the frame analysed last with q (1-100). / IT: codifica il frame analizzato per ultimo con q (1-100).
    void encode(int q, ScopeEncodeResult *out) const;

    static bool mcuCoded(int sy, int mx);

private:
    struct Code {
        uint16_t code = 0;
        uint8_t len = 0;
    };
    struct Huffman {
        Code dc[12];
        Code ac[256];
    };

    // EN: returns the length, or -1 if the slice does not fit in cap bytes. / IT: ritorna la lunghezza, o -1 se la fascia non sta in cap byte.
    int encodeSlice(int sy, int q, uint8_t *out, int cap, int *nonZero) const;

    std::vector<int32_t> m_fillIndex;    // EN: pixel read for every pixel (outside -> edge) / IT: pixel letto per ogni pixel (esterno -> bordo)
    std::vector<float> m_y;              // 240 x 240, level shifted / con livello spostato
    std::vector<float> m_cb, m_cr;       // 120 x 120, level shifted / con livello spostato
    std::vector<float> m_coef;           // Blocks x 64, zigzag order, orthonormal DCT / ordine zigzag, DCT ortonormale
    std::vector<int> m_sliceFirstBlock;  // EN: first block of every slice / IT: primo blocco di ogni fascia
    std::vector<float> m_small, m_prevSmall;   // 60 x 60
    bool m_havePrev = false;
    double m_motion = 0.0;
    Huffman m_lum, m_chr;
};

#endif // OFD_SCOPEENCODER_H
