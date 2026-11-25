#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "udp/NativeUdp.h"

#include <mutex>

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80   // EN: older MinGW headers / IT: header MinGW piu' vecchi
#endif

namespace {
QString wsaText(int err)
{
    wchar_t *buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   DWORD(err), 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString text = buf ? QString::fromWCharArray(buf).trimmed() : QStringLiteral("error %1").arg(err);
    if (buf) LocalFree(buf);
    return text;
}

void startWinsock()
{
    // EN: once per process; never WSACleanup, Qt uses Winsock too. / IT: una volta per processo; mai WSACleanup, anche Qt usa Winsock.
    static std::once_flag once;
    std::call_once(once, []() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
} // namespace

NativeUdp::NativeUdp()
    : m_socket(qintptr(INVALID_SOCKET))
{
    startWinsock();
}

NativeUdp::~NativeUdp()
{
    close();
}

bool NativeUdp::isOpen() const
{
    return SOCKET(m_socket) != INVALID_SOCKET;
}

NativeUdp::BindResult NativeUdp::bind(quint16 port, QString *detail)
{
    close();
    // EN: WSA_FLAG_OVERLAPPED is needed for SO_RCVTIMEO to work (Microsoft docs); the socket stays blocking.
    // IT: WSA_FLAG_OVERLAPPED serve perche' SO_RCVTIMEO funzioni (documentazione Microsoft); il socket resta bloccante.
    SOCKET s = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) {
        if (detail) *detail = wsaText(WSAGetLastError());
        return BindResult::Error;
    }

    // EN: nobody else can bind this port, not even with SO_REUSEADDR. / IT: nessun altro puo' legarsi a questa porta, neanche con SO_REUSEADDR.
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&on), sizeof(on));

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (detail) *detail = wsaText(err);
        closesocket(s);
        // EN: WSAEACCES: taken by a socket with SO_EXCLUSIVEADDRUSE. / IT: WSAEACCES: presa da un socket con SO_EXCLUSIVEADDRUSE.
        return err == WSAEADDRINUSE || err == WSAEACCES ? BindResult::InUse : BindResult::Error;
    }

    int size = WantedBufferBytes;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&size), sizeof(size));
    int got = 0;
    int len = sizeof(got);
    if (getsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char *>(&got), &len) != 0) got = 0;
    m_bufferBytes = got;

    DWORD timeout = ReceiveTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));

    sockaddr_in bound;
    int blen = sizeof(bound);
    m_port = getsockname(s, reinterpret_cast<sockaddr *>(&bound), &blen) == 0 ? ntohs(bound.sin_port) : port;
    m_socket = qintptr(s);
    return BindResult::Ok;
}

void NativeUdp::close()
{
    if (SOCKET(m_socket) == INVALID_SOCKET) return;
    closesocket(SOCKET(m_socket));
    m_socket = qintptr(INVALID_SOCKET);
    m_port = 0;
    m_bufferBytes = 0;
}

int NativeUdp::receive(uint8_t *buf, int cap, QString *detail)
{
    if (SOCKET(m_socket) == INVALID_SOCKET) return -1;
    const int n = recv(SOCKET(m_socket), reinterpret_cast<char *>(buf), cap, 0);
    if (n != SOCKET_ERROR) return n;
    const int err = WSAGetLastError();
    // EN: WSAEMSGSIZE: datagram larger than the buffer, counted as not valid by the caller (never with 64 KB).
    //     WSAECONNRESET: ICMP from an earlier send, harmless for a receiver.
    // IT: WSAEMSGSIZE: datagramma piu' grande del buffer, contato come non valido da chi chiama (mai con 64 KB).
    //     WSAECONNRESET: ICMP di un invio precedente, innocuo per un ricevitore.
    if (err == WSAETIMEDOUT || err == WSAEINTR || err == WSAECONNRESET) return 0;
    if (err == WSAEMSGSIZE) return cap;
    if (detail) *detail = wsaText(err);
    return -1;
}
