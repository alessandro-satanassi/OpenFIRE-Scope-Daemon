#ifndef OFD_NATIVEUDP_H
#define OFD_NATIVEUDP_H

#include <QString>
#include <QtGlobal>
#include <cstdint>

// EN: Native blocking UDP socket for the video of one slot, bound only to 127.0.0.1. Exclusive port
//     (SO_EXCLUSIVEADDRUSE on Windows, no SO_REUSEADDR on Linux and macOS), receive buffer of 2 MB read
//     back after setting it, receive timeout of 250 ms so the thread can check for the exit.
//     Code of one system only: NativeUdp_win.cpp and NativeUdp_posix.cpp.
// IT: Socket UDP nativo bloccante per il video di uno slot, legato solo a 127.0.0.1. Porta esclusiva
//     (SO_EXCLUSIVEADDRUSE su Windows, niente SO_REUSEADDR su Linux e macOS), buffer di ricezione da 2 MB
//     riletto dopo averlo impostato, timeout di ricezione di 250 ms per controllare l'uscita.
//     Codice di un solo sistema: NativeUdp_win.cpp e NativeUdp_posix.cpp.
class NativeUdp
{
public:
    enum class BindResult { Ok, InUse, Error };

    static constexpr int WantedBufferBytes = 2 * 1024 * 1024;
    static constexpr int ReceiveTimeoutMs = 250;

    NativeUdp();
    ~NativeUdp();
    NativeUdp(const NativeUdp &) = delete;
    NativeUdp &operator=(const NativeUdp &) = delete;

    // EN: port 0 = chosen by the system. / IT: porta 0 = scelta dal sistema.
    BindResult bind(quint16 port, QString *detail);
    void close();
    bool isOpen() const;
    quint16 port() const { return m_port; }

    // EN: receive buffer really usable, in bytes (Linux reports twice the value). / IT: buffer di ricezione davvero usabile, in byte (Linux riporta il doppio).
    int receiveBufferBytes() const { return m_bufferBytes; }

    // EN: > 0: bytes of one datagram; 0: timeout or nothing to do; -1: socket error (detail set).
    // IT: > 0: byte di un datagramma; 0: timeout o niente da fare; -1: errore del socket (detail impostato).
    int receive(uint8_t *buf, int cap, QString *detail);

private:
    qintptr m_socket;
    quint16 m_port = 0;
    int m_bufferBytes = 0;
};

#endif // OFD_NATIVEUDP_H
