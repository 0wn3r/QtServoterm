/*
* This file is part of the stmbl project.
*
* Copyright (C) 2020 Forest Darling <fdarling@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef STMBL_SERVOTERM_XYOSCILLOSCOPE_H
#define STMBL_SERVOTERM_XYOSCILLOSCOPE_H

#include "globals.h"

#include <QWidget>
#include <QImage>
#include <QSet>
#include <QPoint>

QT_BEGIN_NAMESPACE
class QTimer;
class QMouseEvent;
QT_END_NAMESPACE

namespace STMBL_Servoterm {

class XYOscilloscope : public QWidget
{
    Q_OBJECT
public:
    XYOscilloscope(QWidget *parent = nullptr);
public slots:
    void addChannelsSample(const QVector<float> &channelsSample);
    void resetScanning();
    void setXChannel(int channel);
    void setYChannel(int channel);
    void setChannelGain(int channel, double gain);
    void setChannelOffset(int channel, double offset);
protected slots:
    void slot_FadeTimeout();
protected:
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void leaveEvent(QEvent *event);
    QRect _ImageRectToWidgetRect(const QRect &r) const;
    // the locus is judged by its shape, so the plot has to be square or a
    // stretched widget reads as a gain mismatch that is not there
    QRect _PlotRect() const;
    QString _AxisLabel(int channel, double normalised) const;
    void _RecalcMargins();
    QImage _plot;
    QTimer *_timer;
    QSet<QPoint> _points;
    int _xChannel;
    int _yChannel;
    int _leftMargin;
    int _bottomMargin;
    QPoint _cursor;
    bool _xClipped;
    bool _yClipped;
    double _channelGain[SCOPE_CHANNEL_COUNT];
    double _channelOffset[SCOPE_CHANNEL_COUNT];
};

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_XYOSCILLOSCOPE_H
