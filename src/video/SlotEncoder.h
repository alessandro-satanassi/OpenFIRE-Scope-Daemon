/*  OpenFIRE Scope Daemon: video from the PC to the OpenFIRE scopes.
    Copyright (C) 2026  Alessandro Satanassi, Team OpenFIRE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef OFD_SLOTENCODER_H
#define OFD_SLOTENCODER_H

#include <QMutex>
#include <QThread>
#include <atomic>

#include "video/RateControl.h"

class DongleLink;
class SlotInput;

// EN: Encoder thread of one slot, running only while the slot has a paired scope (a link). Cycle: waits with
//     the high resolution timer until 1/f from the last encoded frame, takes the latest frame of the slot and,
//     if it is new, encodes it (RateControl) and gives it whole to the writer of the link, in place of the one
//     waiting. It also tells the writer the state of the source (live, paused, none) with its generation:
//     every change of state is a new generation. Never waits for the serial writer.
// IT: Thread dell'encoder di uno slot, attivo solo mentre lo slot ha un mirino abbinato (un collegamento). Ciclo:
//     aspetta con il timer ad alta risoluzione fino a 1/f dall'ultimo frame codificato, prende l'ultimo frame
//     dello slot e, se e' nuovo, lo codifica (RateControl) e lo passa intero allo scrittore del collegamento, al
//     posto di quello in attesa. Comunica anche allo scrittore lo stato della sorgente (viva, in pausa, nessuna)
//     con la sua generazione: ogni cambio di stato e' una generazione nuova. Non aspetta mai lo scrittore seriale.
class SlotEncoder : public QThread
{
    Q_OBJECT

public:
    SlotEncoder(SlotInput *input, QObject *parent = nullptr);
    ~SlotEncoder() override;   // EN: stops and waits for the thread / IT: ferma e aspetta il thread

    // EN: main thread. The values are used from the next start of the thread. / IT: thread principale. I valori valgono dal prossimo avvio del thread.
    void setConfig(const EncoderConfig &config, QThread::Priority priority);
    // EN: main thread: a different link stops the thread and starts it again on it; null stops it.
    //     When it returns, the thread no longer uses the previous link.
    // IT: thread principale: un collegamento diverso ferma il thread e lo riavvia su quello; nullo lo ferma.
    //     Quando torna, il thread non usa piu' il collegamento precedente.
    void setLink(DongleLink *link);

    // EN: statistics of the last second, for the window. / IT: statistiche dell'ultimo secondo, per la finestra.
    double encodeMs() const { return m_encodeMs.load(); }   // EN: mean time of a frame / IT: tempo medio di un frame
    double targetFps() const { return m_targetFps.load(); }

protected:
    void run() override;

private:
    SlotInput *const m_input;
    QMutex m_configMutex;
    EncoderConfig m_config;
    QThread::Priority m_priority = QThread::NormalPriority;
    DongleLink *m_link = nullptr;   // EN: changed only while the thread is stopped / IT: cambia solo a thread fermo
    std::atomic<bool> m_stop{ false };
    std::atomic<double> m_encodeMs{ 0.0 };
    std::atomic<double> m_targetFps{ 0.0 };
};

#endif // OFD_SLOTENCODER_H
