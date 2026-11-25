#ifndef OFD_UDPRECEIVER_H
#define OFD_UDPRECEIVER_H

#include <QThread>
#include <atomic>
#include <memory>

#include "udp/NativeUdp.h"
#include "udp/SlotInput.h"

// EN: Receiver of one slot: blocking recv on its own socket, checks every packet, puts the 15 OF strips
//     together straight into a frame of the slot (RGB565 and BGRX become RGB888 while they are copied) and
//     publishes only complete frames. One copy per datagram, no allocation.
// IT: Ricevitore di uno slot: recv bloccante sul suo socket, controlla ogni pacchetto, ricompone le 15
//     fasce OF direttamente in un frame dello slot (RGB565 e BGRX diventano RGB888 mentre si copiano) e
//     pubblica solo frame completi. Una sola copia per datagramma, nessuna allocazione.
class UdpReceiver : public QThread
{
    Q_OBJECT

public:
    // EN: takes the socket already bound. / IT: prende il socket gia' legato.
    UdpReceiver(std::unique_ptr<NativeUdp> socket, SlotInput *slot, QThread::Priority priority, QObject *parent = nullptr);
    ~UdpReceiver() override;

    quint16 port() const { return m_port; }
    int receiveBufferBytes() const { return m_bufferBytes; }

    void requestStop() { m_stop.store(true); }

signals:
    void failed(const QString &detail);   // EN: socket error: the receiver ends / IT: errore del socket: il ricevitore finisce

protected:
    void run() override;

private:
    void handle(const uint8_t *p, int n, qint64 now);

    std::unique_ptr<NativeUdp> m_socket;
    SlotInput *m_slot = nullptr;
    const QThread::Priority m_priority;
    const quint16 m_port;
    const int m_bufferBytes;
    std::atomic<bool> m_stop{false};

    // EN: reassembly, receiver thread only / IT: ricomposizione, solo thread del ricevitore
    std::shared_ptr<InputFrame> m_building;
    bool m_haveCurrent = false;
    quint32 m_session = 0;
    quint32 m_current = 0;
    quint16 m_mask = 0;
    bool m_completed = false;
    qint64 m_lastStripMs = -1;
};

#endif // OFD_UDPRECEIVER_H
