#ifndef OFD_DISCOVERY_H
#define OFD_DISCOVERY_H

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>

class QUdpSocket;
class Settings;

// EN: Discovery on 127.0.0.1:47810 (QUdpSocket in the main thread): to "WHOAMI" it answers at once to the
//     sender with the text given by the daemon, from the state in memory, never touching the serial ports.
//     Busy port: no discovery, the icon turns yellow and it is tried again every 5 s. A port changed in the
//     settings that is busy: the old one stays. "SUSPEND;T=n" and "RESUME" (OpenFIRE App) go to the
//     command handler, without an answer.
// IT: Discovery su 127.0.0.1:47810 (QUdpSocket nel thread principale): a "WHOAMI" risponde subito al
//     mittente con il testo dato dal demone, dallo stato in memoria, senza mai toccare le seriali.
//     Porta occupata: niente discovery, l'icona diventa gialla e si riprova ogni 5 s. Una porta cambiata
//     nelle impostazioni e occupata: resta la vecchia. "SUSPEND;T=n" e "RESUME" (App OpenFIRE) vanno al
//     gestore dei comandi, senza risposta.
class Discovery : public QObject
{
    Q_OBJECT

public:
    Discovery(Settings *settings, std::function<QByteArray()> answer, QObject *parent = nullptr);
    ~Discovery() override;

    using CommandHandler = std::function<void(const QByteArray &command)>;
    void setCommandHandler(CommandHandler handler) { m_command = std::move(handler); }

    void start();
    void stop();
    // EN: port change from the settings, all or nothing together with the video ports (see Daemon::applySettings):
    //     release the old port, bind the new one without using it, then commit, or drop the new one and restore.
    // IT: cambio di porta dalle impostazioni, tutto o niente insieme alle porte video (vedi Daemon::applySettings):
    //     si rilascia la vecchia, si lega la nuova senza usarla, poi si conferma, o si scarta la nuova e si ripristina.
    bool portChanging() const;
    void releaseOld(quint16 wanted);
    bool bindNew(QString *failure);
    void commit();
    void dropNew();
    void restoreOld();

    quint16 port() const;   // EN: 0 = not active / IT: 0 = non attiva
    QStringList warnings() const;

signals:
    void changed();

private:
    QUdpSocket *makeSocket(quint16 port, QString *detail);
    void adopt(QUdpSocket *socket);
    bool bindTo(quint16 port, QString *detail);
    void read();
    void retry();

    Settings *m_settings = nullptr;
    std::function<QByteArray()> m_answer;
    CommandHandler m_command;
    QUdpSocket *m_socket = nullptr;
    quint16 m_wanted = 0;
    quint16 m_released = 0;           // EN: port released by releaseOld() / IT: porta rilasciata da releaseOld()
    quint16 m_target = 0;             // EN: port wanted by the change / IT: porta voluta dal cambio
    QUdpSocket *m_pending = nullptr;  // EN: new port bound by bindNew() / IT: porta nuova legata da bindNew()
    QString m_warning;
    QTimer m_retry;
    bool m_running = false;
};

#endif // OFD_DISCOVERY_H
