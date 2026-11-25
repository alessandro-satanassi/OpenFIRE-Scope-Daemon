#ifndef OFD_CLIPPLAYER_H
#define OFD_CLIPPLAYER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "dongle/EncodedFrame.h"

class DongleManager;

// EN: Test clip (--play-clip): frames already encoded in the scope format (tools/make_clip.py), sent in a
//     loop to every paired scope. Checks serial, pairing, session, decoding and bandwidth before the encoder.
//     File .ofclip: "OFCLIP1\0", frames (u16 LE), fps (u16 LE), then for every frame 15 slices of
//     q (u8), length (u16 LE), data.
// IT: Clip di prova (--play-clip): frame gia' codificati nel formato del mirino (tools/make_clip.py), mandati
//     in ciclo a ogni mirino abbinato. Verifica seriale, abbinamento, sessione, decodifica e banda prima
//     dell'encoder. File .ofclip: "OFCLIP1\0", frame (u16 LE), fps (u16 LE), poi per ogni frame 15 fasce di
//     q (u8), lunghezza (u16 LE), dati.
class ClipPlayer : public QObject
{
    Q_OBJECT

public:
    explicit ClipPlayer(DongleManager *manager, QObject *parent = nullptr);

    bool load(const QString &path, QString *error);
    void start();
    void stop();

    int frameCount() const { return int(m_frames.size()); }
    int fps() const { return m_fps; }

private:
    void tick();

    DongleManager *m_manager = nullptr;
    QVector<EncodedFramePtr> m_frames;
    int m_fps = 30;
    int m_next = 0;
    QTimer m_timer;
};

#endif // OFD_CLIPPLAYER_H
