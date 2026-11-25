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
//     The object lives in the main thread; its signals reach the main thread queued.
// IT: Collegamento a un dongle: un thread per porta aperta, che non aspetta mai le altre porte. Apre la porta,
//     chiede WHOAMI, legge le trame del dongle, misura una volta la banda USB mentre il dongle e' libero e
//     scrive una trama USB alla volta: prima i messaggi di controllo, poi la prova di banda o le fasce video.
//     START e' una barriera: la trama in scrittura finisce, le fasce vecchie si scartano, poi START e il video nuovo.
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
    void requestRescan(const QByteArray &skipList);   // "36,40"
    void setVideoEnabled(bool on);                    // EN: slot owner with a source / IT: proprietario dello slot con una sorgente
    void submitFrame(const EncodedFramePtr &frame);   // EN: replaces the frame waiting / IT: sostituisce il frame in attesa

    // EN: counters read by the main thread once a second. / IT: contatori letti dal thread principale una volta al secondo.
    quint64 framesSent() const { return m_framesSent.load(); }
    quint64 videoBytesSent() const { return m_videoBytesSent.load(); }
    quint64 qSum() const { return m_qSum.load(); }

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

    void takeCommands();
    void handleContent(const uint8_t *content, int len, qint64 now);
    void pumpWrite();
    void startBandwidthTest(qint64 now);
    void finishBandwidthTest(double kBps);
    void dropVideo();
    void writeFrame(const QByteArray &frame, Writing kind);

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

    // EN: state of the link thread only / IT: stato del solo thread del collegamento
    QList<QByteArray> m_control;      // EN: control frames waiting / IT: trame di controllo in attesa
    QList<QByteArray> m_tx;           // EN: slices of the frame in transmission / IT: fasce del frame in trasmissione
    int m_txIndex = 0;
    int m_txBytes = 0;
    int m_txQSum = 0;
    EncodedFramePtr m_pending;        // EN: next frame (at most one) / IT: prossimo frame (al massimo uno)
    bool m_videoEnabled = false;
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
};

#endif // OFD_DONGLELINK_H
