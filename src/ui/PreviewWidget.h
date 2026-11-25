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

#ifndef OFD_PREVIEWWIDGET_H
#define OFD_PREVIEWWIDGET_H

#include <QImage>
#include <QString>
#include <QWidget>

// EN: One 240x240 box of the preview: the frame that enters the daemon (before the encoder), the label
//     "P1 · UDP · 31 fps" and, with "Circle", the pixels the scope does not show darkened.
//     Phase 1: no frames yet, the box shows "No frame".
// IT: Un riquadro 240x240 dell'anteprima: il frame che entra nel demone (prima dell'encoder), l'etichetta
//     "P1 · UDP · 31 fps" e, con "Cerchio", i pixel che il mirino non mostra scuriti.
//     Fase 1: ancora nessun frame, il riquadro mostra "Nessun frame".
class PreviewWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr int Side = 240;

    explicit PreviewWidget(int player, QWidget *parent = nullptr);

    void setLabel(const QString &label);
    void setFrame(const QImage &frame);   // EN: null image = no frame / IT: immagine nulla = nessun frame
    void setPaused(bool paused);
    void setCircle(bool on);

    QSize sizeHint() const override { return QSize(Side, Side + labelHeight()); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int labelHeight() const;

    int m_player = 0;
    QString m_label;
    QImage m_frame;
    bool m_paused = false;
    bool m_circle = false;
};

#endif // OFD_PREVIEWWIDGET_H
