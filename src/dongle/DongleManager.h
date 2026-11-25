#ifndef OFD_DONGLEMANAGER_H
#define OFD_DONGLEMANAGER_H

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <map>
#include <memory>

#include "app/Settings.h"
#include "app/Status.h"
#include "dongle/EncodedFrame.h"

class DongleLink;
class Settings;

// EN: Dongle manager (main thread). Lists the serial ports every second, tries only the ones with the dongle
//     VID:PID, keeps the result of every port while it stays connected (key: name + VID + PID + serial number)
//     and assigns each paired dongle to its player slot. Nothing is saved on disk.
// IT: Gestore dei dongle (thread principale). Elenca le porte seriali ogni secondo, prova solo quelle con il
//     VID:PID dei dongle, tiene l'esito di ogni porta finche' resta collegata (chiave: nome + VID + PID +
//     numero di serie) e assegna ogni dongle abbinato allo slot del suo giocatore. Niente si salva su disco.
class DongleManager : public QObject
{
    Q_OBJECT

public:
    explicit DongleManager(Settings *settings, QObject *parent = nullptr);
    ~DongleManager() override;

    void start();
    void stop();
    void setSuspended(bool suspended);
    void retryPort(const QString &port);

    // EN: sends a frame to every paired scope that owns its slot (test clip). / IT: manda un frame a ogni mirino abbinato che possiede il suo slot (clip di prova).
    void submitFrameToAll(const EncodedFramePtr &frame);
    void setVideoSource(SourceKind source);   // EN: source shown in the table / IT: sorgente mostrata nella tabella

    void fillSlots(QVector<SlotStatus> *slots) const;
    QVector<DeviceStatus> devices() const;
    QStringList warnings() const;

signals:
    void changed();

private:
    enum class State { Waiting, Probing, Boot, Free, Paired, Duplicate, NoAnswer, Discarded, Busy, AccessDenied, Lost };

    struct Entry {
        QString key;
        QString port;            // EN: shown name: COM5, ttyACM0 / IT: nome mostrato: COM5, ttyACM0
        QString location;        // EN: what the native code opens / IT: cio' che apre il codice nativo
        QString serialNumber;
        State state = State::Waiting;
        qint64 retryAt = 0;
        int noAnswerCount = 0;
        QString detail;
        std::unique_ptr<DongleLink> link;
        quint64 linkId = 0;
        quint64 order = 0;       // EN: newer dongles have a larger number / IT: i dongle piu' nuovi hanno un numero maggiore
        int player = 0;
        QString firmware;
        int channel = 0;
        double capacityKBs = 0.0;
        bool rescanPending = false;
        qint64 rescanAt = 0;
        // EN: statistics / IT: statistiche
        bool haveStat = false;
        quint32 lastDrop = 0, lastQfull = 0;
        qint64 lastStatMs = 0;
        double dropPerSec = 0.0, qfullPerSec = 0.0;
        quint64 lastFrames = 0, lastBytes = 0, lastQSum = 0;
        double fpsOut = 0.0, kbPerFrame = 0.0, q = 0.0;
    };

    void scan();
    void sampleRates();
    void openLink(Entry &e);
    void closeLink(Entry &e);
    Entry *findByLink(quint64 linkId);
    void onOpened(quint64 linkId, int result, const QString &detail);
    void onNoAnswer(quint64 linkId);
    void onScopeState(quint64 linkId, int kind, int player, const QString &firmware, int channel);
    void onStat(quint64 linkId, quint32 rx, quint32 tx, quint32 drop, quint32 qfull);
    void onBandwidth(quint64 linkId, double kBps);
    void onLost(quint64 linkId, const QString &reason);
    void releaseSlot(Entry &e);
    void assignSlot(Entry &e);
    void updateVideoEnabled();
    void checkChannels();
    Entry *slotOwner(int player);
    const Entry *slotOwner(int player) const;
    qint64 now() const { return m_clock.elapsed(); }

    Settings *m_settings = nullptr;
    QTimer m_scanTimer;
    QTimer m_rateTimer;
    QElapsedTimer m_clock;
    std::map<QString, Entry> m_entries;
    QString m_owner[Settings::Players];   // EN: key of the entry that owns slot n / IT: chiave della voce proprietaria dello slot n
    quint64 m_nextLinkId = 1;
    quint64 m_nextOrder = 1;
    bool m_running = false;
    bool m_suspended = false;
    SourceKind m_source = SourceKind::None;
    QStringList m_channelWarnings;
};

#endif // OFD_DONGLEMANAGER_H
