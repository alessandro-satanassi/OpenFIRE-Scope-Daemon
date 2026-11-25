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

#include "ui/PreviewWidget.h"

#include <QPainter>
#include <QPainterPath>

PreviewWidget::PreviewWidget(int player, QWidget *parent)
    : QWidget(parent)
    , m_player(player)
{
    m_label = QStringLiteral("P%1 · …").arg(m_player);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void PreviewWidget::setLabel(const QString &label)
{
    if (label == m_label) return;
    m_label = label;
    update();
}

void PreviewWidget::setFrame(const QImage &frame)
{
    m_frame = frame;
    update();
}

void PreviewWidget::setPaused(bool paused)
{
    if (paused == m_paused) return;
    m_paused = paused;
    update();
}

void PreviewWidget::setCircle(bool on)
{
    if (on == m_circle) return;
    m_circle = on;
    update();
}

int PreviewWidget::labelHeight() const
{
    return fontMetrics().height() + 6;
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect box(0, 0, Side, Side);

    p.fillRect(box, Qt::black);
    if (!m_frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(box, m_frame);
        if (m_circle) {
            // EN: darken what is outside the round display. / IT: scurisce cio' che e' fuori dal display rotondo.
            QPainterPath outside;
            outside.addRect(box);
            QPainterPath circle;
            circle.addEllipse(QRectF(box));
            p.setRenderHint(QPainter::Antialiasing);
            p.fillPath(outside.subtracted(circle), QColor(0, 0, 0, 170));
        }
    } else {
        p.setPen(QColor(0x9E, 0x9E, 0x9E));
        p.drawText(box, Qt::AlignCenter, m_paused ? tr("Paused") : tr("No frame"));
    }
    if (m_circle) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x60, 0x60, 0x60), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
    }

    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(0, Side, Side, labelHeight()), Qt::AlignCenter, m_label);
}
