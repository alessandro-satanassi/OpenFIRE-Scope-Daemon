#ifndef OFD_NATIVETCP_H
#define OFD_NATIVETCP_H

#include <QString>
#include <QtGlobal>
#include <cstdint>

// EN: Native TCP listener for the video of one slot, on the same port as its UDP socket, bound only to
//     127.0.0.1. Exclusive port as NativeUdp. One client at a time: a new connection replaces the current one.
//     Waits of 250 ms at most so the thread can check for the exit.
//     Code of one system only: NativeTcp_win.cpp and NativeTcp_posix.cpp.
// IT: Ascolto TCP nativo per il video di uno slot, sulla stessa porta del suo socket UDP, legato solo a
//     127.0.0.1. Porta esclusiva come NativeUdp. Un client alla volta: una connessione nuova sostituisce quella
//     attuale. Attese di 250 ms al massimo per controllare l'uscita.
//     Codice di un solo sistema: NativeTcp_win.cpp e NativeTcp_posix.cpp.
class NativeTcp
{
public:
    enum class BindResult { Ok, InUse, Error };

    static constexpr int WaitTimeoutMs = 250;

    NativeTcp();
    ~NativeTcp();
    NativeTcp(const NativeTcp &) = delete;
    NativeTcp &operator=(const NativeTcp &) = delete;

    BindResult listen(quint16 port, QString *detail);
    void close();   // EN: listener and client / IT: ascolto e client
    quint16 port() const { return m_port; }

    // EN: grows at every accepted connection: a new value is a new byte stream.
    // IT: cresce a ogni connessione accettata: un valore nuovo e' un flusso di byte nuovo.
    quint32 clientSerial() const { return m_serial; }
    void dropClient();

    // EN: waits at most 250 ms for a new connection or data of the client, then reads at most cap bytes.
    //     > 0: bytes read; 0: nothing (timeout, new client, client gone); -1: error of the listener (detail set).
    // IT: aspetta al massimo 250 ms una connessione nuova o dati del client, poi legge al massimo cap byte.
    //     > 0: byte letti; 0: niente (timeout, client nuovo, client andato via); -1: errore dell'ascolto (detail impostato).
    int receive(uint8_t *buf, int cap, QString *detail);

private:
    qintptr m_listener;
    qintptr m_client;
    quint16 m_port = 0;
    quint32 m_serial = 0;
};

#endif // OFD_NATIVETCP_H
