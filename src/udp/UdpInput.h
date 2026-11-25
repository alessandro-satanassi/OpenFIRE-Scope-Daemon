#ifndef OFD_UDPINPUT_H
#define OFD_UDPINPUT_H

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <memory>
#include <vector>

#include "app/Status.h"
#include "udp/SlotInput.h"

class NativeUdp;
class Settings;
class UdpReceiver;

// EN: Video from the games: the 4 slot inputs and their UDP receivers on 127.0.0.1, open from the start for
//     every slot, even without a dongle (the game can already send and the preview works). Main thread.
//     Port: the preferred one; if busy, 47815-47830, then one chosen by the system. A port changed in the
//     settings is applied at once; if the new one is busy the old one stays and the window says so.
// IT: Video dai giochi: i 4 ingressi degli slot e i loro ricevitori UDP su 127.0.0.1, aperti dall'avvio per
//     tutti gli slot, anche senza dongle (il gioco puo' gia' mandare e l'anteprima funziona). Thread principale.
//     Porta: quella preferita; se occupata, 47815-47830, poi una scelta dal sistema. Una porta cambiata nelle
//     impostazioni si applica subito; se la nuova e' occupata resta la vecchia e la finestra lo dice.
class UdpInput : public QObject
{
    Q_OBJECT

public:
    explicit UdpInput(Settings *settings, QObject *parent = nullptr);
    ~UdpInput() override;

    void start();
    void stop();
    // EN: port change from the settings, all or nothing together with the discovery (see Daemon::applySettings):
    //     the slots that change release their port, the new ports are bound without being used, then commit,
    //     or the new ones are dropped and the old ones restored. No spare ports during a change.
    // IT: cambio di porta dalle impostazioni, tutto o niente insieme alla discovery (vedi Daemon::applySettings):
    //     gli slot che cambiano rilasciano la loro porta, le nuove si legano senza usarle, poi si conferma, o si
    //     scartano le nuove e si ripristinano le vecchie. Niente porte di riserva durante un cambio.
    void releaseOld(const QVector<quint16> &wanted);   // EN: wanted port of P1..P4 / IT: porta voluta di P1..P4
    bool bindNew(QString *failure);
    void commit();
    void dropNew();
    void restoreOld();

    SlotInput *slot(int player) const;   // player 1..4
    InputFramePtr latestFrame(int player) const;
    void fillSlots(QVector<SlotStatus> *slotList) const;
    QStringList warnings() const;

signals:
    void changed();   // EN: a source or a port changed / IT: e' cambiata una sorgente o una porta

private:
    struct Slot {
        std::unique_ptr<SlotInput> input;
        std::unique_ptr<UdpReceiver> receiver;
        quint16 preferred = 0;
        QString portWarning;
        bool changing = false;                  // EN: during a port change / IT: durante un cambio di porta
        bool adopt = false;                     // EN: already on the wanted port, taken at commit / IT: gia' sulla porta voluta, presa al commit
        quint16 target = 0;                     // EN: port wanted by the change / IT: porta voluta dal cambio
        quint16 oldPort = 0;
        std::unique_ptr<NativeUdp> pending;     // EN: new port bound, not used yet / IT: porta nuova legata, non ancora usata
        SourceKind source = SourceKind::None;
        quint64 lastComplete = 0, lastIncomplete = 0, lastInvalid = 0;
        double fpsIn = 0.0, incompletePerSec = 0.0, invalidPerSec = 0.0;
    };

    void open(Slot &s);
    bool startReceiver(Slot &s, quint16 port, QString *detail, bool *inUse);
    void startReceiver(Slot &s, std::unique_ptr<NativeUdp> socket);
    void tick();

    Settings *m_settings = nullptr;
    std::vector<Slot> m_slots;   // EN: not copyable / IT: non copiabili
    QTimer m_timer;
    qint64 m_lastRateMs = 0;
    bool m_running = false;
};

#endif // OFD_UDPINPUT_H
