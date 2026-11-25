#include "serial/NativeSerial.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// EN: Linux and macOS. The wake-up uses a non-blocking pipe (same code on both systems).
// IT: Linux e macOS. Il risveglio usa una pipe non bloccante (stesso codice sui due sistemi).

struct NativeSerial::Impl {
    int fd = -1;
    int wakeRead = -1;
    int wakeWrite = -1;
    QByteArray out;          // frame being written / trama in scrittura
    int outPos = 0;
    bool writing = false;
    bool finishedPending = false;   // write ended inside startWrite / scrittura finita dentro startWrite
    bool writeFailed = false;       // write failed inside startWrite / scrittura fallita dentro startWrite
    QString writeError;
    QElapsedTimer writeTimer;
};

namespace {
void setNonBlockingCloexec(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

QString errnoText(int err)
{
    return QString::fromLocal8Bit(std::strerror(err));
}
} // namespace

NativeSerial::NativeSerial()
    : d(new Impl)
{
    int p[2];
    if (::pipe(p) == 0) {
        setNonBlockingCloexec(p[0]);
        setNonBlockingCloexec(p[1]);
        d->wakeRead = p[0];
        d->wakeWrite = p[1];
    }
}

NativeSerial::~NativeSerial()
{
    close();
    if (d->wakeRead >= 0) ::close(d->wakeRead);
    if (d->wakeWrite >= 0) ::close(d->wakeWrite);
    delete d;
}

NativeSerial::OpenResult NativeSerial::open(const QString &location, QString *detail)
{
    close();
    const QByteArray path = location.toLocal8Bit();
    const int fd = ::open(path.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        if (detail) *detail = errnoText(err);
        if (err == EACCES || err == EPERM) return OpenResult::AccessDenied;
        if (err == EBUSY) return OpenResult::Busy;
        if (err == ENOENT || err == ENODEV || err == ENXIO) return OpenResult::NotFound;
        return OpenResult::Error;
    }

    // EN: exclusive: TIOCEXCL stops later opens; flock is respected by pyserial (esptool) in exclusive mode.
    // IT: esclusiva: TIOCEXCL ferma le aperture successive; flock lo rispetta pyserial (esptool) in esclusiva.
    if (::ioctl(fd, TIOCEXCL) != 0 && errno == EBUSY) {
        if (detail) *detail = errnoText(EBUSY);
        ::close(fd);
        return OpenResult::Busy;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int err = errno;
        if (err == EWOULDBLOCK || err == EAGAIN) {
            if (detail) *detail = errnoText(err);
            ::ioctl(fd, TIOCNXCL);
            ::close(fd);
            return OpenResult::Busy;
        }
        // EN: flock not supported here: the exclusivity of TIOCEXCL stays. / IT: flock non supportato: resta l'esclusiva di TIOCEXCL.
    }

    // EN: a port that cannot be configured is an error, not "no answer". / IT: una porta non configurabile e' un errore, non "nessuna risposta".
    termios tio;
    bool configured = ::tcgetattr(fd, &tio) == 0;
    if (configured) {
        ::cfmakeraw(&tio);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~(CRTSCTS | HUPCL);
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        ::cfsetispeed(&tio, B115200);   // EN: ignored by USB CDC / IT: ignorata dalla USB CDC
        ::cfsetospeed(&tio, B115200);
        configured = ::tcsetattr(fd, TCSANOW, &tio) == 0;
    }
    if (!configured) {
        if (detail) *detail = errnoText(errno);
        ::flock(fd, LOCK_UN);
        ::ioctl(fd, TIOCNXCL);
        ::close(fd);
        return OpenResult::Error;
    }

    // EN: RTS off first, then DTR: never "RTS on, DTR off", the reset state of the C5. If RTS cannot be turned off,
    //     DTR is not touched. A failure is only reported: a driver without these calls would otherwise make the
    //     dongle unusable.
    // IT: prima RTS spento, poi DTR: mai "RTS acceso, DTR spento", lo stato di reset del C5. Se RTS non si spegne,
    //     DTR non si tocca. Un errore si segnala soltanto: un driver senza queste chiamate renderebbe altrimenti il
    //     dongle inutilizzabile.
    int bit = TIOCM_RTS;
    if (::ioctl(fd, TIOCMBIC, &bit) != 0) {
        if (detail) *detail = errnoText(errno);
    } else {
        bit = TIOCM_DTR;
        if (::ioctl(fd, TIOCMBIC, &bit) != 0 && detail) *detail = errnoText(errno);
    }

    d->fd = fd;
    d->writing = false;
    d->finishedPending = false;
    d->writeFailed = false;
    d->out.clear();
    d->outPos = 0;
    return OpenResult::Ok;
}

void NativeSerial::close()
{
    if (d->fd < 0) return;
    ::flock(d->fd, LOCK_UN);
    ::ioctl(d->fd, TIOCNXCL);
    ::close(d->fd);   // EN: never tcflush: a frame is never cut / IT: mai tcflush: una trama non si tronca mai
    d->fd = -1;
    d->writing = false;
    d->finishedPending = false;
    d->writeFailed = false;
}

bool NativeSerial::isOpen() const
{
    return d->fd >= 0;
}

bool NativeSerial::writeBusy() const
{
    return d->writing || d->finishedPending;
}

bool NativeSerial::startWrite(const uint8_t *data, int len)
{
    if (d->fd < 0 || writeBusy() || len <= 0) return false;
    d->out = QByteArray(reinterpret_cast<const char *>(data), len);
    d->outPos = 0;
    d->writing = true;
    d->writeTimer.start();

    // EN: try at once: usually the whole frame enters the driver. / IT: si prova subito: di solito la trama entra tutta nel driver.
    const ssize_t n = ::write(d->fd, d->out.constData(), size_t(d->out.size()));
    if (n > 0) {
        d->outPos = int(n);
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        // EN: real error: reported by the next wait(). / IT: errore vero: lo segnala il prossimo wait().
        d->writing = false;
        d->finishedPending = true;
        d->writeFailed = true;
        d->writeError = errnoText(errno);
        return true;
    }
    if (d->outPos >= d->out.size()) {
        d->writing = false;
        d->finishedPending = true;
    }
    return true;
}

void NativeSerial::wake()
{
    if (d->wakeWrite < 0) return;
    const char c = 1;
    ssize_t r = ::write(d->wakeWrite, &c, 1);   // EN: pipe full = already awake / IT: pipe piena = gia' sveglio
    (void)r;
}

NativeSerial::Events NativeSerial::wait(int timeoutMs, uint8_t *readBuf, int readCap)
{
    Events ev;
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
    if (d->writing) {
        const int left = WriteTimeoutMs - int(d->writeTimer.elapsed());
        if (left <= 0) {
            ev.error = true;
            ev.errorText = QStringLiteral("write timeout");
            return ev;
        }
        if (timeoutMs < 0 || timeoutMs > left) timeoutMs = left;
    }

    pollfd fds[2];
    int n = 0;
    if (d->fd >= 0) {
        fds[n].fd = d->fd;
        fds[n].events = short(POLLIN | (d->writing ? POLLOUT : 0));
        fds[n].revents = 0;
        ++n;
    }
    const int wakeIndex = n;
    if (d->wakeRead >= 0) {
        fds[n].fd = d->wakeRead;
        fds[n].events = POLLIN;
        fds[n].revents = 0;
        ++n;
    }

    const int r = ::poll(fds, nfds_t(n), timeoutMs);
    if (r < 0) {
        if (errno != EINTR) {
            ev.error = true;
            ev.errorText = errnoText(errno);
        }
        return ev;
    }
    if (r == 0) return ev;

    if (wakeIndex < n && (fds[wakeIndex].revents & POLLIN)) {
        char buf[64];
        while (::read(d->wakeRead, buf, sizeof(buf)) > 0) {}
    }

    if (d->fd < 0) return ev;
    const short re = fds[0].revents;

    if (re & POLLIN) {
        const ssize_t got = ::read(d->fd, readBuf, size_t(readCap));
        if (got > 0) {
            ev.readCount = int(got);
        } else if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            // EN: readable with 0 bytes: the device is gone. / IT: leggibile con 0 byte: il dispositivo non c'e' piu'.
            ev.error = true;
            ev.errorText = got == 0 ? QStringLiteral("device disconnected") : errnoText(errno);
            return ev;
        }
    }
    if ((re & (POLLERR | POLLHUP | POLLNVAL)) && ev.readCount == 0) {
        ev.error = true;
        ev.errorText = QStringLiteral("device disconnected");
        return ev;
    }
    if (d->writing && (re & POLLOUT)) {
        const ssize_t w = ::write(d->fd, d->out.constData() + d->outPos, size_t(d->out.size() - d->outPos));
        if (w > 0) {
            d->outPos += int(w);
        } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            ev.error = true;
            ev.errorText = errnoText(errno);
            return ev;
        }
        if (d->outPos >= d->out.size()) {
            d->writing = false;
            ev.writeFinished = true;
        }
    }
    return ev;
}
