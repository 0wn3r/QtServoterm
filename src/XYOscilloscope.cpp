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

#include "XYOscilloscope.h"

#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

namespace STMBL_Servoterm {

static const int MINIMUM_PLOT_SIZE = 256;

XYOscilloscope::XYOscilloscope(QWidget *parent) :
    QWidget(parent),
    _plot(QSize(MINIMUM_PLOT_SIZE, MINIMUM_PLOT_SIZE), QImage::Format_RGB32),
    _timer(new QTimer(this)),
    _xChannel(0),
    _yChannel(1),
    _leftMargin(0),
    _bottomMargin(0),
    _cursor(-1, -1),
    _xClipped(false),
    _yClipped(false)
{
    _timer->setInterval(50);
    connect(_timer, &QTimer::timeout, this, &XYOscilloscope::slot_FadeTimeout);
    setMouseTracking(true);
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        _channelGain[channel] = SCOPE_DEFAULT_GAIN;
        _channelOffset[channel] = SCOPE_DEFAULT_OFFSET;
    }
    _RecalcMargins();
    setMinimumSize(MINIMUM_PLOT_SIZE + _leftMargin, MINIMUM_PLOT_SIZE + _bottomMargin);
    _plot.fill(Qt::white);
}

QString XYOscilloscope::_AxisLabel(int channel, double normalised) const
{
    if (channel < 0 || channel >= SCOPE_CHANNEL_COUNT)
        return QString::number(normalised, 'f', 2);
    const double gain = (_channelGain[channel] != 0.0) ? _channelGain[channel] : 1.0;
    return QString::number(normalised*128.0/gain - _channelOffset[channel], 'g', 4);
}

void XYOscilloscope::_RecalcMargins()
{
    const QFontMetrics fm = fontMetrics();
    int widest = 0;
    for (int i = -2; i <= 2; i++)
        widest = qMax(widest, fm.horizontalAdvance(_AxisLabel(_yChannel, i/2.0)));
    _leftMargin = widest + 6;
    _bottomMargin = fm.height() + 4;
}

QRect XYOscilloscope::_PlotRect() const
{
    // square, so a circular locus stays circular whatever the widget does
    const int side = qMax(1, qMin(width() - _leftMargin, height() - _bottomMargin));
    return QRect(_leftMargin, 0, side, side);
}

void XYOscilloscope::setXChannel(int channel)
{
    const int wanted = qBound(0, channel, SCOPE_CHANNEL_COUNT - 1);
    if (_xChannel == wanted)
        return;
    _xChannel = wanted;
    resetScanning();
}

void XYOscilloscope::setYChannel(int channel)
{
    const int wanted = qBound(0, channel, SCOPE_CHANNEL_COUNT - 1);
    if (_yChannel == wanted)
        return;
    _yChannel = wanted;
    _RecalcMargins();
    resetScanning();
}

void XYOscilloscope::setChannelGain(int channel, double gain)
{
    if (channel < 0 || channel >= SCOPE_CHANNEL_COUNT || gain == 0.0 || _channelGain[channel] == gain)
        return;
    _channelGain[channel] = gain;
    // the trail was drawn under the old scaling, so its labels would lie
    if (channel == _xChannel || channel == _yChannel)
        resetScanning();
    if (channel == _yChannel)
        _RecalcMargins();
    update();
}

void XYOscilloscope::setChannelOffset(int channel, double offset)
{
    if (channel < 0 || channel >= SCOPE_CHANNEL_COUNT || _channelOffset[channel] == offset)
        return;
    _channelOffset[channel] = offset;
    if (channel == _xChannel || channel == _yChannel)
        resetScanning();
    if (channel == _yChannel)
        _RecalcMargins();
    update();
}

void XYOscilloscope::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint p = event->position().toPoint();
    const QPoint wanted = _PlotRect().contains(p) ? p : QPoint(-1, -1);
    if (wanted != _cursor)
    {
        _cursor = wanted;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void XYOscilloscope::leaveEvent(QEvent *event)
{
    if (_cursor != QPoint(-1, -1))
    {
        _cursor = QPoint(-1, -1);
        update();
    }
    QWidget::leaveEvent(event);
}

void XYOscilloscope::addChannelsSample(const QVector<float> &channelsSample)
{
    if (_xChannel >= channelsSample.size() || _yChannel >= channelsSample.size())
        return;
    const float xv = channelsSample.at(_xChannel);
    const float yv = channelsSample.at(_yChannel);
    _xClipped = (xv <= SCOPE_CLAMP_LOW || xv >= SCOPE_CLAMP_HIGH);
    _yClipped = (yv <= SCOPE_CLAMP_LOW || yv >= SCOPE_CLAMP_HIGH);

    // update the off-screen buffer
    QPoint pt(128+xv*128, 128-yv*128);
    // a point that reached the rail is pinned, not measured, so it flattens
    // the locus against the edge -- exactly the shape a gain mismatch makes
    _plot.setPixel(pt, QColor((_xClipped || _yClipped) ? Qt::red : Qt::blue).rgba());
    _points.insert(pt);

    // calculate what it would affect in screen coordinates
    update(_ImageRectToWidgetRect(QRect(pt, QSize(1, 1))));

    // make sure fading is re-enabled
    if (!_timer->isActive())
        _timer->start();
}

void XYOscilloscope::resetScanning()
{
    _plot.fill(Qt::white);
    _points.clear();
    _timer->stop();
    _xClipped = false;
    _yClipped = false;
    update();
}

// apparently the API changed
#if QT_VERSION > QT_VERSION_CHECK(5, 10, 0)
    #define SIZE_IN_BYTES_METHOD sizeInBytes
#else
    #define SIZE_IN_BYTES_METHOD byteCount
#endif

void XYOscilloscope::slot_FadeTimeout()
{
    QRect region; // could be done with QRegion too
    const QRgb whiteVal = QColor(Qt::white).rgba();
    for (QSet<QPoint>::iterator it = _points.begin(); it != _points.end(); /* */)
    {
        QRgb val = _plot.pixel(*it);
        // raise every component that is short of full, so any trace colour
        // fades to white at the same rate. for blue this is the original
        // 0x00010100, and it lets clipped points be drawn in red as well.
        val = qRgb(qMin(255, qRed(val) + 1), qMin(255, qGreen(val) + 1), qMin(255, qBlue(val) + 1));
        _plot.setPixel(*it, val);
        region = region.united(QRect(*it, QSize(1, 1)));
        if (val == whiteVal)
        {
            it = _points.erase(it);
        }
        else // not fully faded yet
        {
            ++it;
        }
    }

    // if we are done, then disable the timer for performance
    if (_points.isEmpty())
        _timer->stop();

    // redraw
    update(_ImageRectToWidgetRect(region));
}

void XYOscilloscope::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QRect plot = _PlotRect();
    painter.drawImage(plot, _plot);
    painter.setPen(Qt::gray);
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
    // 3/4 size circle, the reference for judging the locus
    painter.drawEllipse(QRect(plot.x() + plot.width()/8, plot.y() + plot.height()/8,
                              plot.width()*3/4 - 1, plot.height()*3/4 - 1));

    const QFontMetrics fm = fontMetrics();
    painter.setPen(QColor(110, 110, 110));
    for (int i = -2; i <= 2; i++)
    {
        const double v = i/2.0;
        const int y = plot.y() + static_cast<int>(plot.height()/2 - (plot.height()/2)*v);
        const int x = plot.x() + static_cast<int>(plot.width()/2 + (plot.width()/2)*v);
        if (i != 0)
        {
            painter.setPen(QColor(230, 230, 230));
            painter.drawLine(plot.x(), y, plot.right(), y);
            painter.drawLine(x, plot.y(), x, plot.bottom());
        }
        painter.setPen(SCOPE_CHANNEL_COLORS[_yChannel]);
        const QString yLabel = _AxisLabel(_yChannel, v);
        painter.drawText(plot.x() - 4 - fm.horizontalAdvance(yLabel),
                         qBound(fm.ascent(), y + fm.ascent()/2, height() - 1), yLabel);
        painter.setPen(SCOPE_CHANNEL_COLORS[_xChannel]);
        const QString xLabel = _AxisLabel(_xChannel, v);
        painter.drawText(qBound(0, x - fm.horizontalAdvance(xLabel)/2, width() - fm.horizontalAdvance(xLabel)),
                         plot.bottom() + 2 + fm.ascent(), xLabel);
    }

    if (_xClipped || _yClipped)
    {
        painter.setPen(Qt::red);
        painter.drawText(plot.x() + 4, plot.y() + fm.ascent() + 2,
                         _xClipped ? (_yClipped ? QStringLiteral("x,y clipped") : QStringLiteral("x clipped"))
                                   : QStringLiteral("y clipped"));
    }

    if (plot.contains(_cursor))
    {
        painter.setPen(Qt::darkGray);
        painter.drawLine(_cursor.x(), plot.y(), _cursor.x(), plot.bottom());
        painter.drawLine(plot.x(), _cursor.y(), plot.right(), _cursor.y());
        const double nx = static_cast<double>(_cursor.x() - plot.x())/(plot.width()/2.0) - 1.0;
        const double ny = 1.0 - static_cast<double>(_cursor.y() - plot.y())/(plot.height()/2.0);
        const QString text = QString("%1, %2").arg(_AxisLabel(_xChannel, nx)).arg(_AxisLabel(_yChannel, ny));
        const int tw = fm.horizontalAdvance(text);
        const int tx = qBound(plot.x(), _cursor.x() + 6, qMax(plot.x(), plot.right() - tw - 2));
        const int ty = qBound(plot.y() + fm.height(), _cursor.y() - 4, plot.bottom() - 2);
        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawRect(tx - 2, ty - fm.ascent() - 1, tw + 4, fm.height());
        painter.setBrush(Qt::NoBrush);
        painter.setPen(Qt::black);
        painter.drawText(tx, ty, text);
    }
}

void XYOscilloscope::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

QRect XYOscilloscope::_ImageRectToWidgetRect(const QRect &r) const
{
    const QRect plot = _PlotRect();
    const qreal xScalar = static_cast<qreal>( plot.width())/MINIMUM_PLOT_SIZE;
    const qreal yScalar = static_cast<qreal>(plot.height())/MINIMUM_PLOT_SIZE;
    const QTransform trans = QTransform::fromScale(xScalar, yScalar);
    QRect widgetPixelRect = trans.mapRect(r);
    widgetPixelRect.translate(plot.topLeft());
    return widgetPixelRect;
}

} // namespace STMBL_Servoterm
