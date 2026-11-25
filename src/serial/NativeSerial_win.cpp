#include "serial/NativeSerial.h"

#include <QByteArray>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// EN: Windows: overlapped I/O. A read is always in flight; at most one write. Exclusive by share mode 0.
// IT: Windows: I/O overlapped. Una lettura sempre in corso; al massimo una scrittura. Esclusiva con condivisione 0.

namespace {
constexpr DWORD kReadChunk = 8192;
constexpr DWORD kDriverBuffer = 64 * 1024;

QString lastErrorText(DWORD err)
{
    wchar_t *msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    QString text = msg ? QString::fromWCharArray(msg).trimmed() : QStringLiteral("error %1").arg(err);
    if (msg) LocalFree(msg);
    return text;
}
} // namespace

struct NativeSerial::Impl {
    HANDLE port = INVALID_HANDLE_VALUE;
    HANDLE wakeEvent = nullptr;
    OVERLAPPED readOv{};
    OVERLAPPED writeOv{};
    uint8_t readBuf[kReadChunk];
    DWORD readReady = 0;        // bytes of a read finished at once, not yet handed out / byte di una lettura finita subito, non ancora consegnati
    bool readPending = false;
    QByteArray out;
    bool writing = false;
    bool finishedPending = false;
    bool writeFailed = false;
    QString writeError;

    bool startRead(QString *error)
    {
        // EN: a read finished at once is used now and the read is started again, without waiting for the event.
        // IT: una lettura finita subito si usa ora e si rilancia, senza aspettare l'evento.
        ResetEvent(readOv.hEvent);
        DWORD got = 0;
        if (ReadFile(port, readBuf, kReadChunk, &got, &readOv)) {
            readReady = got;
            readPending = false;
            return true;
        }
        const DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            readPending = true;
            return true;
        }
        *error = lastErrorText(err);
        return false;
    }
};

NativeSerial::NativeSerial()
    : d(new Impl)
{
    d->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    d->readOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    d->writeOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

NativeSerial::~NativeSerial()
{
    close();
    CloseHandle(d->wakeEvent);
    CloseHandle(d->readOv.hEvent);
    CloseHandle(d->writeOv.hEvent);
    delete d;
}

NativeSerial::OpenResult NativeSerial::open(const QString &location, QString *detail)
{
    close();
    const QString path = QStringLiteral("\\\\.\\") + location;
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                           0 /* exclusive / esclusiva */, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (detail) *detail = lastErrorText(err);
        // EN: on Windows a COM port already open elsewhere gives ACCESS_DENIED. / IT: su Windows una COM gia' aperta da altri da' ACCESS_DENIED.
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION) return OpenResult::Busy;
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return OpenResult::NotFound;
        return OpenResult::Error;
    }

    SetupComm(h, kDriverBuffer, kDriverBuffer);

    DCB dcb;
    std::memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        if (detail) *detail = lastErrorText(GetLastError());
        CloseHandle(h);
        return OpenResult::Error;
    }
    dcb.BaudRate = CBR_115200;   // EN: ignored by USB CDC / IT: ignorata dalla USB CDC
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;   // EN: DTR and RTS off together: no reset / IT: DTR e RTS spenti insieme: niente reset
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb)) {
        if (detail) *detail = lastErrorText(GetLastError());
        CloseHandle(h);
        return OpenResult::Error;
    }
    EscapeCommFunction(h, CLRRTS);   // EN: confirmation, no effect if already off / IT: conferma, senza effetto se gia' spenti
    EscapeCommFunction(h, CLRDTR);

    // EN: a read returns as soon as at least one byte is there, or after 250 ms with nothing; writes end within 500 ms.
    // IT: una lettura torna appena c'e' almeno un byte, o dopo 250 ms senza niente; le scritture finiscono entro 500 ms.
    COMMTIMEOUTS to;
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = MAXDWORD;
    to.ReadTotalTimeoutConstant = 250;   // EN: like kIdleWaitMs / IT: come kIdleWaitMs
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = WriteTimeoutMs;
    SetCommTimeouts(h, &to);

    d->port = h;
    d->readReady = 0;
    d->readPending = false;
    d->writing = false;
    d->finishedPending = false;
    d->writeFailed = false;
    QString error;
    if (!d->startRead(&error)) {
        if (detail) *detail = error;
        close();
        return OpenResult::Error;
    }
    return OpenResult::Ok;
}

void NativeSerial::close()
{
    if (d->port == INVALID_HANDLE_VALUE) return;
    // EN: never PurgeComm: a frame is never cut. CancelIo only stops our pending read.
    // IT: mai PurgeComm: una trama non si tronca mai. CancelIo ferma solo la nostra lettura in corso.
    CancelIo(d->port);
    DWORD dummy = 0;
    if (d->readPending) GetOverlappedResult(d->port, &d->readOv, &dummy, TRUE);
    if (d->writing) GetOverlappedResult(d->port, &d->writeOv, &dummy, TRUE);
    CloseHandle(d->port);
    d->port = INVALID_HANDLE_VALUE;
    d->readPending = false;
    d->writing = false;
    d->finishedPending = false;
}

bool NativeSerial::isOpen() const
{
    return d->port != INVALID_HANDLE_VALUE;
}

bool NativeSerial::writeBusy() const
{
    return d->writing || d->finishedPending;
}

bool NativeSerial::startWrite(const uint8_t *data, int len)
{
    if (d->port == INVALID_HANDLE_VALUE || writeBusy() || len <= 0) return false;
    d->out = QByteArray(reinterpret_cast<const char *>(data), len);
    ResetEvent(d->writeOv.hEvent);
    DWORD written = 0;
    if (WriteFile(d->port, d->out.constData(), DWORD(d->out.size()), &written, &d->writeOv)) {
        // EN: finished at once: reported by the next wait(). / IT: finita subito: la segnala il prossimo wait().
        d->finishedPending = true;
        if (written != DWORD(d->out.size())) {
            d->writeFailed = true;
            d->writeError = QStringLiteral("write timeout");
        }
        return true;
    }
    const DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
        d->writing = true;
        return true;
    }
    d->finishedPending = true;
    d->writeFailed = true;
    d->writeError = lastErrorText(err);
    return true;
}

void NativeSerial::wake()
{
    SetEvent(d->wakeEvent);
}

NativeSerial::Events NativeSerial::wait(int timeoutMs, uint8_t *readBuf, int readCap)
{
    Events ev;
    if (d->port == INVALID_HANDLE_VALUE) {
        WaitForSingleObject(d->wakeEvent, timeoutMs < 0 ? INFINITE : DWORD(timeoutMs));
        return ev;
    }

    if (d->finishedPending) {
        d->finishedPending = false;
        if (d->writeFailed) {
            d->writeFailed = false;
            ev.error = true;
            ev.errorText = d->writeError;
            return ev;
        }
        ev.writeFinished = true;
        timeoutMs = 0;
    }
    if (d->readReady > 0) timeoutMs = 0;

    HANDLE handles[3];
    DWORD n = 0;
    if (d->readPending) handles[n++] = d->readOv.hEvent;
    if (d->writing) handles[n++] = d->writeOv.hEvent;
    handles[n++] = d->wakeEvent;
    WaitForMultipleObjects(n, handles, FALSE, timeoutMs < 0 ? INFINITE : DWORD(timeoutMs));

    // EN: read: hand out what arrived, then start the next read. / IT: lettura: consegna cio' che e' arrivato, poi avvia la successiva.
    if (d->readPending && WaitForSingleObject(d->readOv.hEvent, 0) == WAIT_OBJECT_0) {
        DWORD got = 0;
        if (!GetOverlappedResult(d->port, &d->readOv, &got, FALSE)) {
            ev.error = true;
            ev.errorText = lastErrorText(GetLastError());
            return ev;
        }
        d->readPending = false;
        d->readReady = got;
    }
    if (!d->readPending) {
        if (d->readReady > 0) {
            const int take = int(d->readReady) < readCap ? int(d->readReady) : readCap;
            std::memcpy(readBuf, d->readBuf, size_t(take));
            ev.readCount = take;
            // EN: readCap is never smaller than kReadChunk in this program; any rest would be lost.
            // IT: in questo programma readCap non e' mai minore di kReadChunk; un eventuale resto andrebbe perso.
            d->readReady = 0;
        }
        QString error;
        if (!d->startRead(&error)) {
            ev.error = true;
            ev.errorText = error;
            return ev;
        }
    }

    if (d->writing && WaitForSingleObject(d->writeOv.hEvent, 0) == WAIT_OBJECT_0) {
        DWORD written = 0;
        const BOOL ok = GetOverlappedResult(d->port, &d->writeOv, &written, FALSE);
        d->writing = false;
        if (!ok || written != DWORD(d->out.size())) {
            ev.error = true;
            ev.errorText = ok ? QStringLiteral("write timeout") : lastErrorText(GetLastError());
            return ev;
        }
        ev.writeFinished = true;
    }
    return ev;
}
