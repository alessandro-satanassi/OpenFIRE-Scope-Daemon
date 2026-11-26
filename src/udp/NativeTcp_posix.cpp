#include "udp/NativeTcp.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
QString errnoText(int err)
{
    return QString::fromLocal8Bit(std::strerror(err));
}

// EN: closed by us: reset instead of TIME_WAIT, so the port is free again at once.
// IT: chiuso da noi: reset invece di TIME_WAIT, cosi' la porta torna subito libera.
void abortClose(int fd)
{
    linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    ::close(fd);
}
} // namespace

NativeTcp::NativeTcp()
    : m_listener(-1)
    , m_client(-1)
{
}

NativeTcp::~NativeTcp()
{
    close();
}

NativeTcp::BindResult NativeTcp::listen(quint16 port, QString *detail)
{
    close();
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (detail) *detail = errnoText(errno);
        return BindResult::Error;
    }
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);

    // EN: SO_REUSEADDR for TCP only lets the port be taken again over old connections in TIME_WAIT: a second
    //     listener on 127.0.0.1 and this port still fails.
    // IT: SO_REUSEADDR per il TCP permette solo di riprendere la porta sopra vecchie connessioni in TIME_WAIT:
    //     un secondo ascolto su 127.0.0.1 e questa porta fallisce comunque.
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(fd, 4) != 0) {
        const int err = errno;
        if (detail) *detail = errnoText(err);
        ::close(fd);
        return err == EADDRINUSE || err == EACCES ? BindResult::InUse : BindResult::Error;
    }

    // EN: non-blocking listener: a connection that goes away between select() and accept() must not leave the
    //     thread stuck in accept() (it could no longer stop).
    // IT: ascolto non bloccante: una connessione che sparisce tra select() e accept() non deve lasciare il
    //     thread fermo in accept() (non potrebbe piu' fermarsi).
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);

    sockaddr_in bound;
    socklen_t blen = sizeof(bound);
    m_port = ::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &blen) == 0 ? ntohs(bound.sin_port) : port;
    m_listener = fd;
    return BindResult::Ok;
}

void NativeTcp::dropClient()
{
    if (m_client < 0) return;
    abortClose(int(m_client));
    m_client = -1;
}

void NativeTcp::close()
{
    dropClient();
    if (m_listener < 0) return;
    ::close(int(m_listener));
    m_listener = -1;
    m_port = 0;
}

int NativeTcp::receive(uint8_t *buf, int cap, QString *detail)
{
    if (m_listener < 0) return -1;
    const int lfd = int(m_listener);
    const int cfd = int(m_client);
    fd_set set;
    FD_ZERO(&set);
    FD_SET(lfd, &set);
    if (cfd >= 0) FD_SET(cfd, &set);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = WaitTimeoutMs * 1000;
    const int r = ::select((cfd > lfd ? cfd : lfd) + 1, &set, nullptr, nullptr, &tv);
    if (r < 0) {
        const int err = errno;
        if (err == EINTR) return 0;
        if (detail) *detail = errnoText(err);
        return -1;
    }
    if (r == 0) return 0;

    if (FD_ISSET(lfd, &set)) {
        const int fd = ::accept(lfd, nullptr, nullptr);
        if (fd >= 0) {
            ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
            dropClient();   // EN: the new connection replaces the old one / IT: la connessione nuova sostituisce la vecchia
            m_client = fd;
            ++m_serial;
            return 0;
        }
        const int err = errno;
        // EN: connection already gone before accept, or out of descriptors for a moment: not an error of the listener.
        // IT: connessione gia' andata via prima di accept, o descrittori finiti per un momento: non e' un errore dell'ascolto.
        if (err != EINTR && err != EAGAIN && err != EWOULDBLOCK && err != ECONNABORTED && err != EMFILE && err != ENFILE
            && err != ENOBUFS && err != ENOMEM && err != EPROTO) {
            if (detail) *detail = errnoText(err);
            return -1;
        }
        return 0;
    }

    if (cfd >= 0 && FD_ISSET(cfd, &set)) {
        const ssize_t n = ::recv(cfd, buf, size_t(cap), 0);
        if (n > 0) return int(n);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        // EN: closed by the game (0) or broken: the client is gone, the listener stays.
        // IT: chiusa dal gioco (0) o interrotta: il client non c'e' piu', l'ascolto resta.
        ::close(cfd);
        m_client = -1;
    }
    return 0;
}
