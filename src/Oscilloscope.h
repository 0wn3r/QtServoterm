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

#ifndef STMBL_SERVOTERM_OSCILLOSCOPE_H
#define STMBL_SERVOTERM_OSCILLOSCOPE_H

#include "globals.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QMouseEvent;
class QPainter;
QT_END_NAMESPACE

namespace STMBL_Servoterm {

class Oscilloscope : public QWidget
{
    Q_OBJECT
public:
    Oscilloscope(QWidget *parent = nullptr);
    // samples that fit across the plot at one sample per pixel
    int plotWidth() const;
public slots:
    // show a fixed window of samples instead of a scanning ring, for
    // playback: the first sample sits at the left edge and there is no
    // scan line. addChannelsSample() goes back to scanning.
    void setSamples(const QVector< QVector<float> > &samples);
    void addChannelsSample(const QVector<float> &channelsSample);
    void resetScanning();
    void setChannelEnabled(int channel, bool enabled);
    void setChannelGain(int channel, double gain);
    void setChannelOffset(int channel, double offset);
    // -1 labels the axis in normalised units, otherwise it follows this
    // channel's gain and offset into engineering units
    void setReferenceChannel(int channel);
signals:
    void plotWidthChanged(int width);
    // index into the shown samples, -1 when the pointer is off the trace
    void cursorSampleChanged(int sample);
protected:
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void leaveEvent(QEvent *event);
    void _SetScopeX(int newX);
    // the y axis labels live in a left margin, so a sample index and a widget
    // x differ by _PlotLeft() everywhere below
    int _PlotLeft() const;
    int _PlotWidth() const;
    QString _AxisLabel(double normalised) const;
    // the margin is as wide as the widest label it has to hold, so it moves
    // when the reference channel or its scaling does. cached, since _PlotLeft
    // is on the per sample path.
    void _RecalcPlotLeft();
    void _ReflowPlot();
    void _DrawGrid(QPainter &painter);
    void _DrawCursor(QPainter &painter);
    void _SetCursorSample(int sample);
    QVector< QVector<float> > _channelsSamples;
    int _scopeX;
    int _cursorSample;
    int _referenceChannel;
    int _plotLeft;
    int _lastPlotWidth;
    bool _fixedWindow;
    bool _channelEnabled[SCOPE_CHANNEL_COUNT];
    double _channelGain[SCOPE_CHANNEL_COUNT];
    double _channelOffset[SCOPE_CHANNEL_COUNT];
};

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_OSCILLOSCOPE_H
