#include "udp/Discovery.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include "OpenFIRE_Scope_Protocol.h"
#include "app/Log.h"
#include "app/Settings.h"

namespace {
constexpr int kRetryMs = 5000;
constexpr int kMaxRequest = 512;

// EN: the local commands, see Daemon::handleCommand. / IT: i comandi locali, vedi Daemon::handleCommand.
bool isCommand(const QByteArray &text)
{
    return text == "RESUME" || text == "SUSPEND" || text.startsWith("SUSPEND;") || text == "CAPTURE;ON" || text == "CAPTURE;OFF";
}
} // namespace

Discovery::Discovery(Settings *settings, std::function<QByteArray()> answer, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_answer(std::move(answer))
{
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &Discovery::retry);
}

Discovery::~Discovery()
{
    stop();
}

QUdpSocket *Discovery::makeSocket(quint16 port, QString *detail)
{
    auto *socket = new QUdpSocket(this);
    // EN: DontShareAddress: exclusive (SO_EXCLUSIVEADDRUSE on Windows, no SO_REUSEADDR elsewhere).
    // IT: DontShareAddress: esclusiva (SO_EXCLUSIVEADDRUSE su Windows, niente SO_REUSEADDR altrove).
    if (!socket->bind(QHostAddress::LocalHost, port, QAbstractSocket::DontShareAddress)) {
        if (detail) *detail = socket->errorString();
        delete socket;
        return nullptr;
    }
    return socket;
}

void Discovery::adopt(QUdpSocket *socket)
{
    delete m_socket;
    m_socket = socket;
    connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::read);
    openTcp(m_socket->localPort());
}

bool Discovery::bindTo(quint16 port, QString *detail)
{
    QUdpSocket *socket = makeSocket(port, detail);
    if (!socket) return false;
    adopt(socket);
    return true;
}

void Discovery::start()
{
    if (m_running) return;
    m_running = true;
    m_wanted = m_settings->discoveryPort();
    m_warning.clear();
    QString detail;
    if (bindTo(m_wanted, &detail)) {
        Log::info(tr("Discovery on UDP port %1").arg(m_wanted));
    } else {
        m_warning = tr("Discovery port %1 is not available (%2): the games cannot find the scopes").arg(m_wanted).arg(detail);
        Log::warn(m_warning);
        m_retry.start();
    }
    emit changed();
}

void Discovery::stop()
{
    if (!m_running) return;
    m_running = false;
    m_retry.stop();
    dropNew();
    delete m_socket;
    m_socket = nullptr;
    closeTcp();
}

void Discovery::retry()
{
    if (!m_running || m_socket) {
        m_retry.stop();
        return;
    }
    if (bindTo(m_wanted, nullptr)) {
        m_retry.stop();
        m_warning.clear();
        Log::info(tr("Discovery on UDP port %1").arg(m_wanted));
        emit changed();
    }
}

bool Discovery::portChanging() const
{
    return m_running && m_target != 0 && m_target != m_wanted;
}

void Discovery::releaseOld(quint16 wanted)
{
    m_target = wanted;
    if (!portChanging() || !m_socket) return;
    m_released = m_socket->localPort();
    delete m_socket;
    m_socket = nullptr;
    closeTcp();
}

bool Discovery::bindNew(QString *failure)
{
    if (!portChanging()) return true;
    const quint16 wanted = m_target;
    QString detail;
    m_pending = makeSocket(wanted, &detail);
    if (!m_pending && failure) *failure = tr("discovery port %1 is not available (%2)").arg(wanted).arg(detail);
    return m_pending != nullptr;
}

void Discovery::commit()
{
    if (!portChanging()) return;
    m_wanted = m_target;
    m_released = 0;
    adopt(m_pending);
    m_pending = nullptr;
    m_retry.stop();
    m_warning.clear();
    Log::info(tr("Discovery on UDP port %1").arg(m_wanted));
    emit changed();
}

void Discovery::dropNew()
{
    delete m_pending;
    m_pending = nullptr;
}

void Discovery::restoreOld()
{
    const quint16 released = m_released;
    m_released = 0;
    if (!released) return;   // EN: it was not active: the retry goes on / IT: non era attiva: i tentativi proseguono
    QString detail;
    if (!bindTo(released, &detail)) {
        // EN: taken in the meantime by another program: tried again every 5 s. / IT: presa nel frattempo da un altro programma: si riprova ogni 5 s.
        m_warning = tr("Discovery port %1 is not available (%2): the games cannot find the scopes").arg(released).arg(detail);
        Log::warn(m_warning);
        m_retry.start();
    }
    emit changed();
}

quint16 Discovery::port() const
{
    return m_socket ? m_socket->localPort() : 0;
}

QStringList Discovery::warnings() const
{
    return m_warning.isEmpty() ? QStringList() : QStringList(m_warning);
}

void Discovery::read()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram request = m_socket->receiveDatagram(kMaxRequest);
        if (!request.isValid()) continue;
        const QByteArray text = request.data().trimmed();
        if (text == OF_CMD_WHOAMI) {
            m_socket->writeDatagram(m_answer(), request.senderAddress(), quint16(request.senderPort()));
            continue;
        }
        // EN: commands only from this PC (the socket is already bound to 127.0.0.1); anything else is ignored.
        // IT: comandi solo da questo PC (il socket e' gia' legato a 127.0.0.1); tutto il resto si ignora.
        if (isCommand(text) && request.senderAddress().isLoopback() && m_command) m_command(text);
    }
}

void Discovery::openTcp(quint16 port)
{
    closeTcp();
    auto *server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, port)) {
        Log::warn(tr("Discovery TCP port %1 is not available (%2): discovery only over UDP").arg(port).arg(server->errorString()));
        delete server;
        return;
    }
    m_tcp = server;
    connect(m_tcp, &QTcpServer::newConnection, this, &Discovery::acceptTcp);
}

void Discovery::closeTcp()
{
    if (!m_tcp) return;
    // EN: the port is released at once; the server and its connections are deleted later (never inside one of their signals).
    // IT: la porta si rilascia subito; il server e le sue connessioni si eliminano dopo (mai dentro un loro segnale).
    m_tcp->close();
    m_tcp->disconnect(this);
    m_tcp->deleteLater();
    m_tcp = nullptr;
}

void Discovery::acceptTcp()
{
    while (m_tcp && m_tcp->hasPendingConnections()) {
        QTcpSocket *client = m_tcp->nextPendingConnection();   // EN: child of the server / IT: figlio del server
        connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
        connect(client, &QTcpSocket::readyRead, this, [this, client]() { readTcp(client); });
    }
}

void Discovery::readTcp(QTcpSocket *client)
{
    if (client->bytesAvailable() > kMaxRequest) {
        client->abort();
        return;
    }
    // EN: as the UDP: every line is one request; a write without a newline is one request too. TCP may split a
    //     write: if the end is only the start of a request (e.g. "WHO"), it stays in the socket until the rest arrives.
    // IT: come l'UDP: ogni riga e' una richiesta; anche una scrittura senza a capo e' una richiesta. Il TCP puo'
    //     spezzare una scrittura: se la fine e' solo l'inizio di una richiesta (es. "WHO"), resta nel socket finche'
    //     arriva il resto.
    QByteArray data = client->peek(client->bytesAvailable());
    const int tail = data.lastIndexOf('\n') + 1;
    const QByteArray rest = data.mid(tail).trimmed();
    for (const char *request : { OF_CMD_WHOAMI, "RESUME", "SUSPEND", "CAPTURE;ON", "CAPTURE;OFF" }) {
        if (!rest.isEmpty() && rest.size() < int(qstrlen(request)) && QByteArray(request).startsWith(rest)) {
            data.truncate(tail);
            break;
        }
    }
    client->skip(data.size());
    for (const QByteArray &line : data.split('\n')) {
        const QByteArray text = line.trimmed();
        if (text.isEmpty()) continue;
        if (text == OF_CMD_WHOAMI) {
            client->write(m_answer());
            client->disconnectFromHost();   // EN: the end of the answer / IT: la fine della risposta
            return;
        }
        // EN: the server is bound to 127.0.0.1: the commands come only from this PC.
        // IT: il server e' legato a 127.0.0.1: i comandi vengono solo da questo PC.
        if (isCommand(text) && m_command) m_command(text);
    }
}
