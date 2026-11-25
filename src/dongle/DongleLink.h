#ifndef OFD_DONGLELINK_H
#define OFD_DONGLELINK_H

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>
#include <atomic>

#include "dongle/EncodedFrame.h"
#include "serial/NativeSerial.h"

// EN: Link to one dongle: one thread per open port, never waiting for other ports. It opens the port, asks
//     WHOAMI, reads the frames of the dongle, measures the USB bandwidth once while the dongle is free and
//     writes one USB frame at a time: control messages first, then the bandwidth test or the video slices.
//     START is a barrier: the frame being written ends, the old slices are dropped, then START and the new video.
//     It is also the writer of the slot: it keeps the bandwidth credit (0.8 * C per second, at most 30 KB) and
//     the estimate of C, sends PAUSE every 200 ms while the slot is paused and sends again the last frame really
//     sent after 100 ms without new frames, only with a live source. A new frame always comes first.
//     The object lives in the main thread; its signals reach the main thread queued.
// IT: Collegamento a un dongle: un thread per porta aperta, che non aspetta mai le altre porte. Apre la porta,
//     chiede WHOAMI, legge le trame del dongle, misura una volta la banda USB mentre il dongle e' libero e
//     scrive una trama USB alla volta: prima i messaggi di controllo, poi la prova di banda o le fasce video.
//     START e' una barriera: la trama in scrittura finisce, le fasce vecchie si scartano, poi START e il video nuovo.
//     E' anche lo scrittore dello slot: tiene il credito di banda (0,8 * C al secondo, al massimo 30 KB) e la
//     stima di C, manda PAUSE ogni 200 ms mentre lo slot e' in pausa e rimanda l'ultimo frame davvero inviato
//     dopo 100 ms senza frame nuovi, solo con la sorgente viva. Un frame nuovo passa sempre prima.
//     L'oggetto vive nel thread principale; i suoi segnali arrivano al thread principale accodati.
class DongleLink : public QThread
{
    Q_OBJECT

public:
    enum ScopeKind { ScopeBoot = 1, ScopeUnpaired = 2, ScopePaired = 3 };

    DongleLink(const QString &location, QThread::Priority priority, QObject *parent = nullptr);
    ~DongleLink() override;   // EN: stops and waits for the thread / IT: ferma e aspetta il thread

    void requestStop();

    // EN: thread safe, from the main thread. / IT: sicure tra i thread, dal thread principale.
    // EN: state of the source of the slot, from the encoder (or the test clip). / IT: stato della sorgente dello slot, dall'encoder (o dalla clip di prova).
    enum class Source { None, Live, Paused };

    void requestRescan(const QByteArray &skipList);   // "36,40"
    void setVideoEnabled(bool on);                    // EN: owner of its slot / IT: proprietario del suo slot
    // EN: a new generation ends the frame in transmission and drops the one waiting and the copy for the resend.
    // IT: una generazione nuova finisce il frame in trasmissione e scarta quello in attesa e la copia per il reinvio.
    void setSource(Source source, quint32 generation);
    void submitFrame(const EncodedFramePtr &frame);   // EN: replaces the frame waiting / IT: sostituisce il frame in attesa

    // EN: estimated USB capacity C in KB/s, read by the encoder. / IT: capacita' USB stimata C in KB/s, letta dall'encoder.
    double capacityKBs() const { return m_capacity.load(); }
    static constexpr double DefaultCapacityKBs = 450.0;

    // EN: counters read by the main thread once a second. / IT: contatori letti dal thread principale una volta al secondo.
    quint64 framesSent() const { return m_framesSent.load(); }
    quint64 videoBytesSent() const { return m_videoBytesSent.load(); }
    quint64 qSum() const { return m_qSum.load(); }
    quint64 resends() const { return m_resends.load(); }

signals:
    void opened(int result, const QString &detail);   // NativeSerial::OpenResult
    void noAnswer();
    void scopeState(int kind, int player, const QString &firmware, int channel);
    void stat(quint32 rx, quint32 tx, quint32 drop, quint32 qfull);
    void bandwidthMeasured(double kBps);   // EN: 0 = not measured, keep the default / IT: 0 = non misurata, resta il predefinito
    void lost(const QString &reason);

protected:
    void run() override;

private:
    enum class Writing { None, Control, Test, Video };

    void takeCommands(qint64 now);
    void handleContent(const uint8_t *content, int len, qint64 now);
    void pumpWrite(qint64 now);
    int nextEventIn(qint64 now) const;   // EN: ms until credit, resend or PAUSE need the thread / IT: ms finche' credito, reinvio o PAUSE servono al thread
    void startBandwidthTest(qint64 now);
    void finishBandwidthTest(double kBps);
    void dropVideo();
    void newGeneration(quint32 generation);
    bool startFrame(const EncodedFramePtr &frame, qint64 now);
    void writeFrame(const QByteArray &frame, Writing kind, qint64 now);
    void updateCredit(qint64 now);
    void congestion(qint64 now);

    const QString m_location;
    const QThread::Priority m_priority;
    NativeSerial m_serial;
    std::atomic<bool> m_stop{ false };

    // EN: commands from the main thread / IT: comandi dal thread principale
    QMutex m_cmdMutex;
    QList<QByteArray> m_cmdTexts;
    EncodedFramePtr m_cmdFrame;
    bool m_cmdVideoEnabled = false;
    bool m_cmdVideoChanged = false;
    Source m_cmdSource = Source::None;
    quint32 m_cmdGeneration = 0;
    bool m_cmdSourceChanged = false;

    // EN: state of the link thread only / IT: stato del solo thread del collegamento
    QList<QByteArray> m_control;      // EN: control frames waiting / IT: trame di controllo in attesa
    QList<QByteArray> m_tx;           // EN: slices of the frame in transmission / IT: fasce del frame in trasmissione
    int m_txIndex = 0;
    int m_txBytes = 0;
    int m_txQSum = 0;
    EncodedFramePtr m_pending;        // EN: next frame (at most one) / IT: prossimo frame (al massimo uno)
    EncodedFramePtr m_txFrame;        // EN: frame in transmission / IT: frame in trasmissione
    quint32 m_txGeneration = 0;
    EncodedFramePtr m_lastSent;       // EN: last frame sent whole in the current generation (resend) / IT: ultimo frame inviato intero nella generazione corrente (reinvio)
    qint64 m_lastFrameStartMs = 0;    // EN: last frame (new or resent) into transmission / IT: ultimo frame (nuovo o rimandato) passato in trasmissione
    bool m_videoEnabled = false;
    Source m_source = Source::None;
    quint32 m_generation = 0;
    qint64 m_nextPauseMs = 0;
    qint64 m_writeStartMs = 0;

    // EN: bandwidth credit (bytes) and capacity C (KB/s) / IT: credito di banda (byte) e capacita' C (KB/s)
    double m_credit = 0.0;
    qint64 m_creditMs = 0;
    std::atomic<double> m_capacity{ DefaultCapacityKBs };
    double m_capacityTop = DefaultCapacityKBs;   // EN: value of the test, or 450 / IT: valore della prova, o 450
    qint64 m_lastCongestionMs = -1000000;
    qint64 m_lastDecreaseMs = -1000000;
    qint64 m_lastRaiseMs = 0;
    bool m_haveStat = false;
    quint32 m_lastDrop = 0, m_lastQfull = 0;
    bool m_paired = false;
    int m_player = 0;
    bool m_inBoot = false;          // EN: dongle searching its channel / IT: dongle in ricerca del canale
    qint64 m_nextBootWhoami = 0;
    uint16_t m_frameNumber = 0;
    Writing m_writing = Writing::None;
    bool m_lastSliceWriting = false;

    bool m_bwDone = false;
    bool m_bwActive = false;
    int m_bwStats = 0;
    quint32 m_bwRx2 = 0;
    qint64 m_bwT2 = 0;
    qint64 m_bwDeadline = 0;
    QByteArray m_testFrame;

    std::atomic<quint64> m_framesSent{ 0 };
    std::atomic<quint64> m_videoBytesSent{ 0 };
    std::atomic<quint64> m_qSum{ 0 };
    std::atomic<quint64> m_resends{ 0 };
};

#endif // OFD_DONGLELINK_H
