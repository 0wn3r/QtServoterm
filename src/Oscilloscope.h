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
public slots:
    void addChannelsSample(const QVector<float> &channelsSample);
    void resetScanning();
    void setChannelEnabled(int channel, bool enabled);
    void setChannelGain(int channel, double gain);
    void setChannelOffset(int channel, double offset);
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
    void _DrawGrid(QPainter &painter);
    void _DrawCursor(QPainter &painter);
    QVector< QVector<float> > _channelsSamples;
    int _scopeX;
    int _cursorSample;
    bool _channelEnabled[SCOPE_CHANNEL_COUNT];
    double _channelGain[SCOPE_CHANNEL_COUNT];
    double _channelOffset[SCOPE_CHANNEL_COUNT];
};

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_OSCILLOSCOPE_H
