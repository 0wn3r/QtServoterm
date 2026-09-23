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

#include "Oscilloscope.h"

#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>

namespace STMBL_Servoterm {

Oscilloscope::Oscilloscope(QWidget *parent) : QWidget(parent), _scopeX(0), _cursorSample(-1), _referenceChannel(-1), _plotLeft(0), _lastPlotWidth(-1), _fixedWindow(false)
{
    setMinimumSize(600, 256);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setAutoFillBackground(true);
    setPalette(pal);
    setMouseTracking(true); // so the cursor readout follows without a click
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        _channelEnabled[channel] = true;
        _channelGain[channel] = SCOPE_DEFAULT_GAIN;
        _channelOffset[channel] = SCOPE_DEFAULT_OFFSET;
    }
    _RecalcPlotLeft();
}

QString Oscilloscope::_AxisLabel(double normalised) const
{
    if (_referenceChannel < 0 || _referenceChannel >= SCOPE_CHANNEL_COUNT)
        return QString::number(normalised, 'f', 2);
    const double gain = (_channelGain[_referenceChannel] != 0.0) ? _channelGain[_referenceChannel] : 1.0;
    return QString::number(normalised*128.0/gain - _channelOffset[_referenceChannel], 'g', 4);
}

void Oscilloscope::_RecalcPlotLeft()
{
    const QFontMetrics fm = fontMetrics();
    int widest = 0;
    for (int i = -4; i <= 4; i++)
        widest = qMax(widest, fm.horizontalAdvance(_AxisLabel(i/4.0)));
    _plotLeft = widest + 6;
}

void Oscilloscope::_ReflowPlot()
{
    // a wider margin means a narrower plot, so the ring has to be trimmed to
    // match or samples would sit past the right hand edge
    _RecalcPlotLeft();
    const int w = _PlotWidth();
    if (_channelsSamples.size() > w)
        _channelsSamples.resize(w);
    if (_scopeX >= w)
        _scopeX = 0;
    if (_cursorSample >= _channelsSamples.size())
        _SetCursorSample(-1);
    update();
    if (w != _lastPlotWidth)
    {
        _lastPlotWidth = w;
        emit plotWidthChanged(w);
    }
}

int Oscilloscope::plotWidth() const
{
    return _PlotWidth();
}

void Oscilloscope::setSamples(const QVector< QVector<float> > &samples)
{
    _fixedWindow = true;
    _channelsSamples = samples;
    if (_channelsSamples.size() > _PlotWidth())
        _channelsSamples.resize(_PlotWidth());
    _scopeX = 0;
    if (_cursorSample >= _channelsSamples.size())
        _SetCursorSample(-1);
    update();
}

void Oscilloscope::_SetCursorSample(int sample)
{
    if (sample == _cursorSample)
        return;
    _cursorSample = sample;
    emit cursorSampleChanged(sample);
}

int Oscilloscope::_PlotLeft() const
{
    return _plotLeft;
}

void Oscilloscope::setReferenceChannel(int channel)
{
    const int wanted = (channel >= 0 && channel < SCOPE_CHANNEL_COUNT) ? channel : -1;
    if (_referenceChannel == wanted)
        return;
    _referenceChannel = wanted;
    _ReflowPlot();
}

int Oscilloscope::_PlotWidth() const
{
    return qMax(1, width() - _PlotLeft());
}

void Oscilloscope::setChannelGain(int channel, double gain)
{
    if (channel < 0 || channel >= SCOPE_CHANNEL_COUNT || gain == 0.0 || _channelGain[channel] == gain)
        return;
    _channelGain[channel] = gain;
    if (channel == _referenceChannel)
        _ReflowPlot();
    else
        update();
}

void Oscilloscope::setChannelOffset(int channel, double offset)
{
    if (channel < 0 || channel >= SCOPE_CHANNEL_COUNT || _channelOffset[channel] == offset)
        return;
    _channelOffset[channel] = offset;
    if (channel == _referenceChannel)
        _ReflowPlot();
    else
        update();
}

void Oscilloscope::setChannelEnabled(int channel, bool enabled)
{
    if (channel < 0 || channel >= SCOPE_CHANNEL_COUNT || _channelEnabled[channel] == enabled)
        return;
    _channelEnabled[channel] = enabled;
    update();
}

void Oscilloscope::addChannelsSample(const QVector<float> &channelsSample)
{
    if (channelsSample.size() != SCOPE_CHANNEL_COUNT) // sanity check
        return;
    if (_fixedWindow)
    {
        // leaving playback style display: start a fresh scan
        _fixedWindow = false;
        _channelsSamples.clear();
        _scopeX = 0;
        update();
    }

    // add/overwrite the appropriate sample
    const int h = height();
    if (_cursorSample >= 0)
    {
        // the readout box floats over the trace, so a strip repaint would eat
        // part of it. hovering is a deliberate act, so pay for a full repaint.
        update();
    }
    else
    {
        // HACK for some reason, updating less than a 4 pixel wide strip results in flickering, I need to investigate...
        update(_PlotLeft() + _scopeX - 1, 0, 4, h); // update affected lines
    }
    if (_scopeX < _channelsSamples.size())
        _channelsSamples[_scopeX] = channelsSample;
    else
        _channelsSamples.append(channelsSample);

    // update the next position to write to
    // NOTE: has the side effect of updating the region
    //       where we just layed down a sample
    if (_scopeX+1 >= _PlotWidth())
        _SetScopeX(0);
    else
        _SetScopeX(_scopeX + 1);
}

void Oscilloscope::resetScanning()
{
    _SetScopeX(0);
}

static void DrawSampleRange(const QVector< QVector<float> > &channelsSamples, int start, int end, int plotLeft, int h, QPainter &painter, const bool *channelEnabled)
{
    const int numSamples = end - start;
    if (numSamples <= 0)
        return;
    QPolygon points;
    QPolygon clipped;
    points.reserve(numSamples);
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        if (!channelEnabled[channel])
            continue;
        points.resize(0);
        clipped.resize(0);
        for (int sample = start; sample < end; sample++)
        {
            const float v = channelsSamples[sample].at(channel);
            const int x = plotLeft + sample;
            const int y = qBound(0, static_cast<int>(h/2 - static_cast<float>(h/2)*v), h-1);
            points.append(QPoint(x, y));
            // a channel whose gain is too high pins against the firmware's
            // byte clamp and draws as a perfectly convincing flat line. mark
            // those samples so a rail cannot be mistaken for a measurement.
            if (v <= SCOPE_CLAMP_LOW || v >= SCOPE_CLAMP_HIGH)
                clipped.append(QPoint(x, y));
        }
        painter.setPen(SCOPE_CHANNEL_COLORS[channel]);
        if (points.size() == 1)
            painter.drawPoint(points.at(0));
        else
            painter.drawPolyline(points);
        if (!clipped.isEmpty())
        {
            QPen pen(SCOPE_CHANNEL_COLORS[channel]);
            pen.setWidth(3);
            painter.setPen(pen);
            painter.drawPoints(clipped);
        }
    }
}

void Oscilloscope::_DrawGrid(QPainter &painter)
{
    const int h = height();
    const int w = width();
    const int plotLeft = _PlotLeft();
    const QFontMetrics fm = fontMetrics();

    // with no reference channel the axis is in normalised units, because the
    // eight channels each carry their own gain and offset and no single scale
    // can label them all. picking a reference borrows that channel's scale for
    // the axis; every channel's own value stays on the cursor readout.
    for (int i = -4; i <= 4; i++)
    {
        const double v = i/4.0;
        const int y = qBound(0, static_cast<int>(h/2 - (h/2)*v), h-1);
        painter.setPen((i == 0) ? QColor(160, 160, 160) : QColor(224, 224, 224));
        painter.drawLine(plotLeft, y, w-1, y);
        // in the reference channel's own colour, so it is obvious whose units
        // the axis is carrying
        painter.setPen((_referenceChannel >= 0) ? SCOPE_CHANNEL_COLORS[_referenceChannel] : QColor(110, 110, 110));
        const QString label = _AxisLabel(v);
        painter.drawText(plotLeft - 4 - fm.horizontalAdvance(label),
                         qBound(fm.ascent(), y + fm.ascent()/2, h-1),
                         label);
    }
}

void Oscilloscope::_DrawCursor(QPainter &painter)
{
    if (_cursorSample < 0 || _cursorSample >= _channelsSamples.size())
        return;
    const int h = height();
    const int plotLeft = _PlotLeft();
    const int x = plotLeft + _cursorSample;
    painter.setPen(Qt::darkGray);
    painter.drawLine(x, 0, x, h-1);

    const QVector<float> &sample = _channelsSamples.at(_cursorSample);
    const QFontMetrics fm = fontMetrics();
    QStringList labels;
    QVector<int> shown;
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        if (!_channelEnabled[channel] || channel >= sample.size())
            continue;
        const float v = sample.at(channel);
        const double gain = (_channelGain[channel] != 0.0) ? _channelGain[channel] : 1.0;
        // inverse of term.c: value = (byte - 128)/gain - offset, and the demux
        // already divided the byte by 128
        const double eng = static_cast<double>(v)*128.0/gain - _channelOffset[channel];
        QString text = QString("%1: %2").arg(channel + 1).arg(eng, 0, 'g', 4);
        if (v <= SCOPE_CLAMP_LOW || v >= SCOPE_CLAMP_HIGH)
            text += QStringLiteral(" clipped");
        labels.append(text);
        shown.append(channel);
    }
    if (labels.isEmpty())
        return;

    int textWidth = 0;
    for (const QString &text : labels)
        textWidth = qMax(textWidth, fm.horizontalAdvance(text));
    const int lineHeight = fm.height();
    const int boxWidth = textWidth + 10;
    const int boxHeight = lineHeight*labels.size() + 6;
    int boxX = x + 8;
    if (boxX + boxWidth > width())
        boxX = x - 8 - boxWidth;
    boxX = qBound(0, boxX, qMax(0, width() - boxWidth));
    const int boxY = qBound(0, h/2 - boxHeight/2, qMax(0, h - boxHeight));

    painter.setPen(Qt::darkGray);
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawRect(boxX, boxY, boxWidth, boxHeight);
    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i < labels.size(); i++)
    {
        painter.setPen(SCOPE_CHANNEL_COLORS[shown.at(i)]);
        painter.drawText(boxX + 5, boxY + 3 + lineHeight*i + fm.ascent(), labels.at(i));
    }
}

void Oscilloscope::mouseMoveEvent(QMouseEvent *event)
{
    const int sample = event->position().toPoint().x() - _PlotLeft();
    const int newSample = (sample >= 0 && sample < _channelsSamples.size()) ? sample : -1;
    if (newSample != _cursorSample)
    {
        _SetCursorSample(newSample);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void Oscilloscope::leaveEvent(QEvent *event)
{
    if (_cursorSample != -1)
    {
        _SetCursorSample(-1);
        update();
    }
    QWidget::leaveEvent(event);
}

void Oscilloscope::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    // painter.setRenderHint(QPainter::Antialiasing);
    const int h = height();
    const int plotLeft = _PlotLeft();
    // drawn full width every time: qt clips it to the update region, so a
    // strip repaint still restores the grid lines crossing that strip
    _DrawGrid(painter);

    // a very slight optimization
    if (!_channelsSamples.isEmpty())
    {
        const int  firstX = qMax(0, event->rect().x() - plotLeft - 1);
        const int   lastX = qMin(event->rect().x() + event->rect().width() - plotLeft, _channelsSamples.size());
        if (lastX > firstX)
        {
            const int middleX = qBound(firstX, _scopeX, lastX);
            DrawSampleRange(_channelsSamples,  firstX, middleX, plotLeft, h, painter, _channelEnabled);
            DrawSampleRange(_channelsSamples, middleX,   lastX, plotLeft, h, painter, _channelEnabled);
        }
    }
    if (!_fixedWindow)
    {
        painter.setPen(Qt::blue);
        painter.drawLine(plotLeft + _scopeX, 0, plotLeft + _scopeX, h-1);
    }
    _DrawCursor(painter);
}

void Oscilloscope::resizeEvent(QResizeEvent *event)
{
    _RecalcPlotLeft(); // the font can change out from under us with the style
    const int w = qMax(1, event->size().width() - _PlotLeft());

    // possibly reduce the data window length
    if (_channelsSamples.size() > w)
        _channelsSamples.resize(w);
    // make sure we're still inside the window
    if (_scopeX >= _channelsSamples.size())
        _SetScopeX(0);
    if (_cursorSample >= _channelsSamples.size())
        _SetCursorSample(-1);
    QWidget::resizeEvent(event);
    if (w != _lastPlotWidth)
    {
        _lastPlotWidth = w;
        emit plotWidthChanged(w);
    }
}

void Oscilloscope::_SetScopeX(int newX)
{
    const int h = height();
    const int plotLeft = _PlotLeft();
    const int oldX = _scopeX;
    _scopeX = newX;
    update(plotLeft + oldX, 0, 1, h);
    update(plotLeft + newX, 0, 1, h);
}

} // namespace STMBL_Servoterm
