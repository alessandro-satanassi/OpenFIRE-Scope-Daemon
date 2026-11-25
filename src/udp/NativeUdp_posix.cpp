#include "udp/NativeUdp.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
QString errnoText(int err)
{
    return QString::fromLocal8Bit(std::strerror(err));
}
} // namespace

NativeUdp::NativeUdp()
    : m_socket(-1)
{
}

NativeUdp::~NativeUdp()
{
    close();
}

bool NativeUdp::isOpen() const
{
    return m_socket >= 0;
}

NativeUdp::BindResult NativeUdp::bind(quint16 port, QString *detail)
{
    close();
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (detail) *detail = errnoText(errno);
        return BindResult::Error;
    }
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);

    // EN: no SO_REUSEADDR: the port stays ours. / IT: niente SO_REUSEADDR: la porta resta nostra.
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        const int err = errno;
        if (detail) *detail = errnoText(err);
        ::close(fd);
        return err == EADDRINUSE || err == EACCES ? BindResult::InUse : BindResult::Error;
    }

    int size = WantedBufferBytes;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    int got = 0;
    socklen_t len = sizeof(got);
    if (::getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &got, &len) != 0) got = 0;
#ifdef __linux__
    got /= 2;   // EN: Linux doubles the value for its bookkeeping / IT: Linux raddoppia il valore per la sua contabilita'
#endif
    m_bufferBytes = got;

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = ReceiveTimeoutMs * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in bound;
    socklen_t blen = sizeof(bound);
    m_port = ::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &blen) == 0 ? ntohs(bound.sin_port) : port;
    m_socket = fd;
    return BindResult::Ok;
}

void NativeUdp::close()
{
    if (m_socket < 0) return;
    ::close(int(m_socket));
    m_socket = -1;
    m_port = 0;
    m_bufferBytes = 0;
}

int NativeUdp::receive(uint8_t *buf, int cap, QString *detail)
{
    if (m_socket < 0) return -1;
    const ssize_t n = ::recv(int(m_socket), buf, size_t(cap), 0);
    if (n >= 0) return int(n);
    const int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR || err == ECONNREFUSED) return 0;
    if (detail) *detail = errnoText(err);
    return -1;
}
