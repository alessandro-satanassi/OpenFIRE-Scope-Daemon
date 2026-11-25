#include "udp/Discovery.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include "OpenFIRE_Scope_Protocol.h"
#include "app/Log.h"
#include "app/Settings.h"

namespace {
constexpr int kRetryMs = 5000;
constexpr int kMaxRequest = 512;
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
        const bool command = text == "RESUME" || text == "SUSPEND" || text.startsWith("SUSPEND;");
        if (command && request.senderAddress().isLoopback() && m_command) m_command(text);
    }
}
