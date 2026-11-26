#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "udp/NativeTcp.h"

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

// EN: closed by us: reset instead of TIME_WAIT, so the port is free again at once.
// IT: chiuso da noi: reset invece di TIME_WAIT, cosi' la porta torna subito libera.
void abortClose(SOCKET s)
{
    linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char *>(&l), sizeof(l));
    closesocket(s);
}
} // namespace

NativeTcp::NativeTcp()
    : m_listener(qintptr(INVALID_SOCKET))
    , m_client(qintptr(INVALID_SOCKET))
{
    startWinsock();
}

NativeTcp::~NativeTcp()
{
    close();
}

NativeTcp::BindResult NativeTcp::listen(quint16 port, QString *detail)
{
    close();
    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) {
        if (detail) *detail = wsaText(WSAGetLastError());
        return BindResult::Error;
    }

    // EN: no SO_EXCLUSIVEADDRUSE here: with it the port could stay blocked by old connections after a restart of
    //     the daemon. Without SO_REUSEADDR nobody can listen on this port while we do (as QTcpServer).
    // IT: niente SO_EXCLUSIVEADDRUSE qui: con esso la porta potrebbe restare bloccata da vecchie connessioni dopo un
    //     riavvio del demone. Senza SO_REUSEADDR nessuno puo' ascoltare su questa porta mentre lo facciamo noi (come QTcpServer).
    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR || ::listen(s, 4) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (detail) *detail = wsaText(err);
        closesocket(s);
        return err == WSAEADDRINUSE || err == WSAEACCES ? BindResult::InUse : BindResult::Error;
    }

    // EN: non-blocking listener: a connection that goes away between select() and accept() must not leave the
    //     thread stuck in accept() (it could no longer stop). The accepted socket inherits it: it is read only
    //     after select() says it has data.
    // IT: ascolto non bloccante: una connessione che sparisce tra select() e accept() non deve lasciare il
    //     thread fermo in accept() (non potrebbe piu' fermarsi). Il socket accettato lo eredita: si legge solo
    //     dopo che select() dice che ha dati.
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    sockaddr_in bound;
    int blen = sizeof(bound);
    m_port = getsockname(s, reinterpret_cast<sockaddr *>(&bound), &blen) == 0 ? ntohs(bound.sin_port) : port;
    m_listener = qintptr(s);
    return BindResult::Ok;
}

void NativeTcp::dropClient()
{
    if (SOCKET(m_client) == INVALID_SOCKET) return;
    abortClose(SOCKET(m_client));
    m_client = qintptr(INVALID_SOCKET);
}

void NativeTcp::close()
{
    dropClient();
    if (SOCKET(m_listener) == INVALID_SOCKET) return;
    closesocket(SOCKET(m_listener));
    m_listener = qintptr(INVALID_SOCKET);
    m_port = 0;
}

int NativeTcp::receive(uint8_t *buf, int cap, QString *detail)
{
    if (SOCKET(m_listener) == INVALID_SOCKET) return -1;
    const SOCKET ls = SOCKET(m_listener);
    const SOCKET cs = SOCKET(m_client);
    fd_set set;
    FD_ZERO(&set);
    FD_SET(ls, &set);
    if (cs != INVALID_SOCKET) FD_SET(cs, &set);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = WaitTimeoutMs * 1000;
    const int r = select(0, &set, nullptr, nullptr, &tv);
    if (r == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err == WSAEINTR) return 0;
        if (detail) *detail = wsaText(err);
        return -1;
    }
    if (r == 0) return 0;

    if (FD_ISSET(ls, &set)) {
        const SOCKET s = accept(ls, nullptr, nullptr);
        if (s != INVALID_SOCKET) {
            SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);
            dropClient();   // EN: the new connection replaces the old one / IT: la connessione nuova sostituisce la vecchia
            m_client = qintptr(s);
            ++m_serial;
            return 0;
        }
        const int err = WSAGetLastError();
        // EN: connection already gone before accept, or out of resources for a moment: not an error of the listener.
        // IT: connessione gia' andata via prima di accept, o risorse finite per un momento: non e' un errore dell'ascolto.
        if (err != WSAEINTR && err != WSAEWOULDBLOCK && err != WSAECONNRESET && err != WSAEMFILE && err != WSAENOBUFS) {
            if (detail) *detail = wsaText(err);
            return -1;
        }
        return 0;
    }

    if (cs != INVALID_SOCKET && FD_ISSET(cs, &set)) {
        const int n = recv(cs, reinterpret_cast<char *>(buf), cap, 0);
        if (n > 0) return n;
        if (n == SOCKET_ERROR && (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK)) return 0;
        // EN: closed by the game (0) or broken: the client is gone, the listener stays.
        // IT: chiusa dal gioco (0) o interrotta: il client non c'e' piu', l'ascolto resta.
        closesocket(cs);
        m_client = qintptr(INVALID_SOCKET);
    }
    return 0;
}
