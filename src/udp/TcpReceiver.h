#ifndef OFD_TCPRECEIVER_H
#define OFD_TCPRECEIVER_H

#include <QThread>
#include <atomic>
#include <memory>

#include "OpenFIRE_Scope_Protocol.h"
#include "udp/NativeTcp.h"

class SlotInput;
class UdpReceiver;

// EN: TCP layer of one slot, on the same port as its UDP receiver. The game sends whole frames: the same
//     16-byte header followed by the 240x240 pixels in the format of the header, or the header alone with
//     strip = 0xFF (game paused); any other value of strip is a whole frame. The length comes from the format,
//     no length field.
//     Every frame is cut into the same 15 strips of the UDP and given to the UDP receiver, which does the
//     rest exactly as for UDP. A header that is not valid breaks the stream: the connection is closed.
// IT: Livello TCP di uno slot, sulla stessa porta del suo ricevitore UDP. Il gioco manda frame interi: la stessa
//     intestazione da 16 byte seguita dai pixel 240x240 nel formato dell'intestazione, o la sola intestazione
//     con fascia = 0xFF (gioco in pausa); qualunque altro valore di fascia e' un frame intero. La lunghezza viene
//     dal formato, nessun campo lunghezza.
//     Ogni frame si taglia nelle stesse 15 fasce dell'UDP e si passa al ricevitore UDP, che fa il resto
//     esattamente come per l'UDP. Un'intestazione non valida rompe il flusso: la connessione si chiude.
class TcpReceiver : public QThread
{
    Q_OBJECT

public:
    // EN: takes the listener already open. / IT: prende l'ascolto gia' aperto.
    TcpReceiver(std::unique_ptr<NativeTcp> listener, UdpReceiver *target, SlotInput *slot, QThread::Priority priority,
                QObject *parent = nullptr);
    ~TcpReceiver() override;

    quint16 port() const { return m_port; }
    void requestStop() { m_stop.store(true); }

protected:
    void run() override;

private:
    void resetStream();
    bool headerDone(qint64 now);   // EN: false = not valid / IT: false = non valida
    void frameDone(qint64 now);

    std::unique_ptr<NativeTcp> m_listener;
    UdpReceiver *m_target = nullptr;
    SlotInput *m_slot = nullptr;
    const QThread::Priority m_priority;
    const quint16 m_port;
    std::atomic<bool> m_stop{false};

    // EN: stream state, receiver thread only. The 15 strips are read straight after 15 copies of the header.
    // IT: stato del flusso, solo thread del ricevitore. Le 15 fasce si leggono subito dopo 15 copie dell'intestazione.
    std::unique_ptr<uint8_t[]> m_packets;
    uint8_t m_header[OF_UDP_HEADER];
    int m_headerHave = 0;
    int m_stripBytes = 0;    // EN: bytes of one strip of the frame being read, 0 = header / IT: byte di una fascia del frame in lettura, 0 = intestazione
    int m_payloadHave = 0;
};

#endif // OFD_TCPRECEIVER_H
