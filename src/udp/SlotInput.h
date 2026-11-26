#ifndef OFD_SLOTINPUT_H
#define OFD_SLOTINPUT_H

#include <QMutex>
#include <QtGlobal>
#include <atomic>
#include <cstdint>
#include <memory>

#include "OpenFIRE_Scope_Protocol.h"
#include "app/Status.h"

// EN: One input frame of a slot: RGB888 240x240, the single internal format (UDP and DXGI are converted
//     while they are copied). seq grows by 1 at every published frame of the slot.
// IT: Un frame di ingresso di uno slot: RGB888 240x240, l'unico formato interno (UDP e DXGI si convertono
//     mentre si copiano). seq cresce di 1 a ogni frame pubblicato dallo slot.
struct InputFrame {
    static constexpr int Stride = OF_SCOPE_W * 3;
    static constexpr int Bytes = Stride * OF_SCOPE_H;   // 172,800
    uint8_t rgb[Bytes];
    quint64 seq = 0;
    SourceKind source = SourceKind::None;
    qint64 timeMs = 0;
};
using InputFramePtr = std::shared_ptr<const InputFrame>;

// EN: Monotonic milliseconds shared by every thread. / IT: millisecondi monotoni comuni a tutti i thread.
qint64 monotonicMs();

// EN: Input buffer of one slot: a reserve of 6 frames and one shared reference to the "latest frame".
//     The producer (UDP receiver or screen capture) takes a free frame, fills it and publishes it: the
//     reference is replaced under a very short lock, no data are copied. Encoder and preview take the
//     reference under the lock and read the frame after releasing it. A frame goes back to the reserve
//     when nobody uses it any more.
// IT: Buffer di ingresso di uno slot: una riserva di 6 frame e un solo riferimento condiviso all'"ultimo
//     frame". Chi produce (ricevitore UDP o cattura dello schermo) prende un frame libero, lo riempie e lo pubblica:
//     il riferimento si sostituisce sotto un lock brevissimo, senza copiare dati. Encoder e anteprima
//     prendono il riferimento sotto il lock e leggono il frame dopo averlo rilasciato. Un frame torna nella
//     riserva quando nessuno lo usa piu'.
class SlotInput
{
public:
    static constexpr int Reserve = 6;
    static constexpr int SourceTimeoutMs = 500;   // EN: no valid event for so long -> no UDP source / IT: nessun evento valido per tanto -> niente sorgente UDP

    explicit SlotInput(int player);
    SlotInput(const SlotInput &) = delete;
    SlotInput &operator=(const SlotInput &) = delete;

    int player() const { return m_player; }

    // EN: producer side / IT: lato di chi produce
    std::shared_ptr<InputFrame> acquire();   // EN: null if the reserve is all in use / IT: nullo se la riserva e' tutta in uso
    void publish(const std::shared_ptr<InputFrame> &frame, SourceKind source);
    void notePresence();                     // EN: UDP presence packet (game paused) / IT: pacchetto di presenza UDP (gioco in pausa)

    // EN: screen capture. The slot decides who can publish: the UDP always (and the slot goes to the UDP),
    //     the capture only while the slot is in DXGI: allowed (paired scope, no test clip), capture live and no
    //     UDP source. A capture frame arriving during the switch to the UDP is dropped.
    // IT: cattura dello schermo. Lo slot decide chi puo' pubblicare: l'UDP sempre (e lo slot passa all'UDP),
    //     la cattura solo mentre lo slot e' in DXGI: permessa (mirino abbinato, niente clip di prova), cattura
    //     viva e nessuna sorgente UDP. Un frame della cattura arrivato durante il passaggio all'UDP si scarta.
    void setCaptureAllowed(bool on);         // EN: from the daemon / IT: dal demone
    void setCaptureLive(bool on);            // EN: from the capture / IT: dalla cattura
    bool captureWanted(qint64 nowMs) const;  // EN: allowed and no UDP source / IT: permessa e nessuna sorgente UDP
    bool publishCapture(const std::shared_ptr<InputFrame> &frame);   // EN: false = dropped / IT: false = scartato
    // EN: grows at every interruption of the capture (loss, monitor change, capture off, scope unpaired), also
    //     one too short for the encoder to see: with the same source DXGI a new value is a new generation.
    // IT: cresce a ogni interruzione della cattura (perdita, cambio di monitor, cattura spenta, mirino
    //     disabbinato), anche una troppo breve perche' l'encoder la veda: con la stessa sorgente DXGI un valore
    //     nuovo e' una generazione nuova.
    quint32 captureEpoch() const { return m_captureEpoch.load(); }

    // EN: consumer side / IT: lato di chi legge
    InputFramePtr latest() const;
    SourceKind udpSource(qint64 nowMs) const;   // Udp, UdpPaused or None, from the latest valid event / dall'ultimo evento valido
    SourceKind source(qint64 nowMs) const;      // EN: UDP first, then Dxgi, otherwise None / IT: prima l'UDP, poi Dxgi, altrimenti None

    // EN: counters for the window (cumulative) / IT: contatori per la finestra (cumulativi)
    std::atomic<quint64> framesComplete{0};
    std::atomic<quint64> framesIncomplete{0};   // EN: replaced before all 15 strips arrived / IT: sostituiti prima che arrivassero tutte le 15 fasce
    std::atomic<quint64> packetsInvalid{0};
    std::atomic<qint64> tcpFrameMs{-1};          // EN: latest complete frame over TCP, -1 = none / IT: ultimo frame completo via TCP, -1 = nessuno

private:
    void dropCaptureFrame();

    const int m_player;
    mutable QMutex m_mutex;
    std::shared_ptr<InputFrame> m_reserve[Reserve];
    InputFramePtr m_latest;
    quint64 m_seq = 0;
    // EN: time and kind of the latest valid UDP event in one value, (ms << 1) | presence: the one handled last
    //     wins, even within the same millisecond. -1 = none.
    // IT: ora e tipo dell'ultimo evento UDP valido in un solo valore, (ms << 1) | presenza: vince quello elaborato
    //     per ultimo, anche nello stesso millisecondo. -1 = nessuno.
    std::atomic<qint64> m_lastEvent{-1};
    std::atomic<bool> m_captureAllowed{false};
    std::atomic<bool> m_captureLive{false};
    std::atomic<quint32> m_captureEpoch{0};
};

#endif // OFD_SLOTINPUT_H
