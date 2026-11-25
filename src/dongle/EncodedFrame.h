#ifndef OFD_ENCODEDFRAME_H
#define OFD_ENCODEDFRAME_H

#include <QByteArray>
#include <cstdint>
#include <memory>

#include "OpenFIRE_Scope_Protocol.h"

// EN: One encoded frame: the 15 slices of the scope bitstream, each with its q. Immutable once built and
//     shared between threads by pointer. The writer of the dongle wraps every slice in a USB frame and
//     writes the frame number only when the frame goes into transmission.
// IT: Un frame codificato: le 15 fasce del bitstream del mirino, ognuna con il suo q. Immutabile una volta
//     costruito e condiviso tra i thread per puntatore. Lo scrittore del dongle avvolge ogni fascia in una
//     trama USB e scrive il numero di frame solo quando il frame passa in trasmissione.
struct EncodedSlice {
    uint8_t q = 0;
    QByteArray data;   // max OF_VIDEO_MAX_DATA
};

struct EncodedFrame {
    EncodedSlice slices[OF_SCOPE_STRIPS];

    int bytes() const
    {
        int n = 0;
        for (const EncodedSlice &s : slices) n += int(s.data.size());
        return n;
    }
};

using EncodedFramePtr = std::shared_ptr<const EncodedFrame>;

#endif // OFD_ENCODEDFRAME_H
