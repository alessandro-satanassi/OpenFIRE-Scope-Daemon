#ifndef OFD_NATIVESERIAL_H
#define OFD_NATIVESERIAL_H

#include <QString>
#include <cstdint>

// EN: Native serial port of a dongle, used by one thread only (the DongleLink thread); only wake() may be
//     called from other threads. The port is opened in exclusive mode with DTR and RTS off without passing
//     through the reset state of the C5 (Windows: SetCommState sets both together; POSIX: RTS first, then DTR).
//     At most one write (one whole USB frame) is in flight; partial writes are resumed inside. Nothing is ever
//     purged: a frame is never cut by the daemon.
//     Code of one system only: NativeSerial_win.cpp and NativeSerial_posix.cpp.
// IT: Porta seriale nativa di un dongle, usata da un solo thread (quello del DongleLink); solo wake() si puo'
//     chiamare da altri thread. La porta si apre in esclusiva con DTR e RTS spenti senza passare per lo stato
//     di reset del C5 (Windows: SetCommState li imposta insieme; POSIX: prima RTS, poi DTR).
//     Al massimo una scrittura (una trama USB intera) in volo; le scritture parziali riprendono all'interno.
//     Non si svuota mai nulla: il demone non tronca mai una trama.
//     Codice di un solo sistema: NativeSerial_win.cpp e NativeSerial_posix.cpp.
class NativeSerial
{
public:
    enum class OpenResult { Ok, Busy, AccessDenied, NotFound, Error };

    struct Events {
        int readCount = 0;          // bytes copied into the read buffer / byte copiati nel buffer di lettura
        bool writeFinished = false; // the write in flight is complete / la scrittura in volo e' finita
        bool error = false;         // read or write error, or write timeout: the port is lost / errore o scrittura scaduta: porta persa
        QString errorText;
    };

    static constexpr int WriteTimeoutMs = 500;   // EN: over this the dongle is stuck / IT: oltre, il dongle e' bloccato

    NativeSerial();
    ~NativeSerial();
    NativeSerial(const NativeSerial &) = delete;
    NativeSerial &operator=(const NativeSerial &) = delete;

    // EN: location: "COM5" (Windows) or "/dev/ttyACM0", "/dev/cu.usbmodem1101" (POSIX). detail: the reason of a
    //     failure; with Ok, if not empty, why RTS/DTR could not be turned off (the port is used all the same).
    // IT: location: "COM5" (Windows) oppure "/dev/ttyACM0", "/dev/cu.usbmodem1101" (POSIX). detail: il motivo di un
    //     errore; con Ok, se non vuoto, perche' RTS/DTR non si sono potuti spegnere (la porta si usa lo stesso).
    OpenResult open(const QString &location, QString *detail);
    void close();
    bool isOpen() const;

    // EN: Starts the write of a whole frame (the data are copied). Only when !writeBusy().
    // IT: Avvia la scrittura di una trama intera (i dati si copiano). Solo con !writeBusy().
    bool startWrite(const uint8_t *data, int len);
    bool writeBusy() const;

    // EN: Waits until data arrive, the write ends, wake() is called or timeoutMs passes (-1 = forever).
    // IT: Aspetta che arrivino dati, che finisca la scrittura, che si chiami wake() o che passino timeoutMs (-1 = sempre).
    Events wait(int timeoutMs, uint8_t *readBuf, int readCap);

    // EN: Thread safe: wakes wait() up. / IT: sicura tra i thread: sveglia wait().
    void wake();

private:
    struct Impl;
    Impl *d;
};

#endif // OFD_NATIVESERIAL_H
