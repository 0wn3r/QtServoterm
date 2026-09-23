/*
* This file is part of the stmbl project.
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

#include "PlaybackWindow.h"
#include "Oscilloscope.h"
#include "XYOscilloscope.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QScrollBar>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QKeyEvent>
#include <QSignalBlocker>

#include <cmath>

namespace STMBL_Servoterm {

// the drive's rt loop runs at 5 kHz and term0 samples every send_step ticks
static const double RT_PERIOD_S = 0.0002;
// what term.c's nrt_init sets, for a file whose header could not say
static const double FIRMWARE_DEFAULT_SEND_STEP = 50.0;
static const int PLAY_TICK_MS = 30;

bool ScopeRecording::load(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    path = filePath;
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        pins[channel] = QStringLiteral("?");
        gain[channel] = SCOPE_DEFAULT_GAIN;
        offset[channel] = SCOPE_DEFAULT_OFFSET;
    }
    values.clear();
    scalingChanges.clear();

    static const QRegularExpression stepRe(QStringLiteral("^#\\s*term0\\.send_step\\s*=\\s*([-0-9.eE+]+)"));
    static const QRegularExpression chRe(QStringLiteral("^#\\s*ch(\\d+)\\s+(\\S+)\\s+gain\\s+(\\S+)\\s+offset\\s+(\\S+)"));
    static const QRegularExpression dateRe(QStringLiteral("^#\\s*(\\d{4}-\\d\\d-\\d\\dT\\S+)"));
    static const QRegularExpression changeRe(QStringLiteral("^#\\s*(at sample .*)$"));

    QTextStream in(&file);
    QString line;
    int badRows = 0;
    while (in.readLineInto(&line))
    {
        if (line.startsWith('#'))
        {
            QRegularExpressionMatch m;
            if ((m = stepRe.match(line)).hasMatch())
                sendStep = m.captured(1).toDouble();
            else if ((m = chRe.match(line)).hasMatch())
            {
                const int channel = m.captured(1).toInt() - 1;
                if (channel >= 0 && channel < SCOPE_CHANNEL_COUNT)
                {
                    pins[channel] = m.captured(2);
                    bool ok = false;
                    const double g = m.captured(3).toDouble(&ok);
                    if (ok && g != 0.0)
                        gain[channel] = g;
                    const double o = m.captured(4).toDouble(&ok);
                    if (ok)
                        offset[channel] = o;
                }
            }
            else if ((m = changeRe.match(line)).hasMatch())
                scalingChanges.append(m.captured(1));
            else if (date.isEmpty() && (m = dateRe.match(line)).hasMatch())
                date = m.captured(1);
            continue;
        }
        if (line.isEmpty() || line.startsWith(QLatin1String("sample")))
            continue;
        const QStringList fields = line.split(',');
        if (fields.size() < SCOPE_CHANNEL_COUNT + 1)
        {
            badRows++;
            continue;
        }
        for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
            values.append(fields.at(channel + 1).toFloat());
    }
    if (values.isEmpty())
    {
        if (error)
            *error = badRows ? QString("no readable rows (%1 malformed)").arg(badRows)
                             : QStringLiteral("no samples in the file");
        return false;
    }
    return true;
}

PlaybackWindow::PlaybackWindow(const ScopeRecording &recording, QWidget *parent) :
    QWidget(parent, Qt::Window),
    _rec(recording),
    _oscilloscope(new Oscilloscope),
    _xyOscilloscope(new XYOscilloscope),
    _scrollBar(new QScrollBar(Qt::Horizontal)),
    _positionLabel(new QLabel),
    _cursorLabel(new QLabel),
    _playButton(new QPushButton(tr("Play"))),
    _speedBox(new QComboBox),
    _zoomBox(new QComboBox),
    _xyCheck(new QCheckBox(tr("x/y scope"))),
    _playTimer(new QTimer(this)),
    _playPosition(0.0),
    _lastTickMs(0),
    _cursorPixel(-1)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Playback - %1").arg(QFileInfo(_rec.path).fileName()));
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        _channelEnabled[channel] = true;
        // start at the scaling the file was recorded with, so the traces and
        // the clipped markers look exactly as they did live
        _displayGain[channel] = _rec.gain[channel];
        _displayOffset[channel] = _rec.offset[channel];
        _oscilloscope->setChannelGain(channel, _displayGain[channel]);
        _oscilloscope->setChannelOffset(channel, _displayOffset[channel]);
        _xyOscilloscope->setChannelGain(channel, _displayGain[channel]);
        _xyOscilloscope->setChannelOffset(channel, _displayOffset[channel]);
    }
    _xyOscilloscope->setVisible(false);
    _playTimer->setInterval(PLAY_TICK_MS);
    _playButton->setCheckable(true);
    _playButton->setToolTip(tr("play from the left edge of the view (space)"));

    // x real time; at one sample per pixel real time scrolls 5000/send_step
    // pixels a second, so the default is well below it
    static const double SPEEDS[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0};
    for (double speed : SPEEDS)
        _speedBox->addItem(QString("%1x").arg(speed), speed);
    _speedBox->setCurrentIndex(3);
    _speedBox->setToolTip(tr("playback speed, relative to real time"));

    static const int ZOOMS[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
    for (int zoom : ZOOMS)
        _zoomBox->addItem(QString("%1:1").arg(zoom), zoom);
    _zoomBox->setToolTip(tr("recorded samples per pixel. above 1:1 every n-th sample is drawn, so\n"
                            "anything shorter than n samples can fall between pixels."));

    _positionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _cursorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QHBoxLayout * const outer = new QHBoxLayout(this);
    QVBoxLayout * const vbox = new QVBoxLayout;
    outer->addLayout(vbox, 1);
    {
        QHBoxLayout * const hbox = new QHBoxLayout;
        hbox->addWidget(_oscilloscope, 1);
        hbox->addWidget(_xyOscilloscope);
        vbox->addLayout(hbox, 1);
    }
    vbox->addWidget(_scrollBar);
    {
        QHBoxLayout * const hbox = new QHBoxLayout;
        hbox->addWidget(_playButton);
        hbox->addWidget(new QLabel(tr("speed")));
        hbox->addWidget(_speedBox);
        hbox->addWidget(new QLabel(tr("zoom")));
        hbox->addWidget(_zoomBox);
        hbox->addWidget(_xyCheck);
        hbox->addSpacing(12);
        hbox->addWidget(_positionLabel, 1);
        vbox->addLayout(hbox);
    }
    vbox->addWidget(_cursorLabel);

    QWidget * const panel = new QWidget;
    {
        // the same fold away spine as the live view
        QToolButton * const toggle = new QToolButton;
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setFixedWidth(14);
        toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        toggle->setToolTip(tr("show or hide the scope controls"));
        connect(toggle, &QToolButton::toggled, this, [panel, toggle] (bool shown) {
            panel->setVisible(shown);
            toggle->setArrowType(shown ? Qt::RightArrow : Qt::LeftArrow);
        });
        outer->addWidget(toggle);
    }
    {
        QGridLayout * const grid = new QGridLayout(panel);
        grid->setContentsMargins(0, 0, 0, 0);
        QLabel * const gainLabel = new QLabel(tr("gain"));
        QLabel * const offsetLabel = new QLabel(tr("offset"));
        gainLabel->setToolTip(tr("display only: window is +-127/gain around -offset, as term0.gain<n> would give"));
        offsetLabel->setToolTip(tr("display only: the window centres on -offset"));
        grid->addWidget(new QLabel(tr("ch")), 0, 0);
        grid->addWidget(gainLabel, 0, 1);
        grid->addWidget(offsetLabel, 0, 2);
        for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
        {
            const int row = channel + 1;
            QCheckBox * const cb = new QCheckBox(QString::number(channel + 1));
            cb->setChecked(true);
            QPalette pal = cb->palette();
            pal.setColor(QPalette::WindowText, SCOPE_CHANNEL_COLORS[channel]);
            cb->setPalette(pal);
            connect(cb, &QCheckBox::toggled, this, [this, channel] (bool enabled) {
                _channelEnabled[channel] = enabled;
                _oscilloscope->setChannelEnabled(channel, enabled);
            });
            grid->addWidget(cb, row, 0);

            QComboBox * const gainBox = new QComboBox;
            gainBox->setEditable(true);
            gainBox->setInsertPolicy(QComboBox::NoInsert);
            gainBox->setMinimumWidth(72);
            for (int decade = -2; decade <= 3; decade++)
            {
                const double scale = std::pow(10.0, decade);
                gainBox->addItem(QString::number(1.0*scale, 'g', 6));
                gainBox->addItem(QString::number(2.0*scale, 'g', 6));
                gainBox->addItem(QString::number(5.0*scale, 'g', 6));
            }
            gainBox->setCurrentText(QString::number(_displayGain[channel], 'g', 6));
            gainBox->setToolTip(tr("recorded at gain %1: window +-%2, resolution %3")
                                    .arg(_rec.gain[channel], 0, 'g', 6)
                                    .arg(127.0/_rec.gain[channel]).arg(1.0/_rec.gain[channel]));
            connect(gainBox, &QComboBox::currentTextChanged, this, [this, channel] (const QString &text) {
                bool ok = false;
                const double gain = text.toDouble(&ok);
                if (!ok || gain == 0.0)
                    return;
                _displayGain[channel] = gain;
                _oscilloscope->setChannelGain(channel, gain);
                _xyOscilloscope->setChannelGain(channel, gain);
                slot_Redraw();
            });
            grid->addWidget(gainBox, row, 1);

            QDoubleSpinBox * const offsetBox = new QDoubleSpinBox;
            offsetBox->setRange(-1000000.0, 1000000.0);
            offsetBox->setDecimals(2);
            offsetBox->setSingleStep(1.0);
            offsetBox->setMinimumWidth(84);
            offsetBox->setValue(_displayOffset[channel]);
            offsetBox->setToolTip(tr("recorded at offset %1").arg(_rec.offset[channel], 0, 'g', 6));
            connect(offsetBox, &QDoubleSpinBox::valueChanged, this, [this, channel] (double offset) {
                _displayOffset[channel] = offset;
                _oscilloscope->setChannelOffset(channel, offset);
                _xyOscilloscope->setChannelOffset(channel, offset);
                slot_Redraw();
            });
            grid->addWidget(offsetBox, row, 2);
        }
        int row = SCOPE_CHANNEL_COUNT + 1;
        {
            QComboBox * const refBox = new QComboBox;
            refBox->setToolTip(tr("y axis units follow this channel"));
            refBox->addItem(QStringLiteral("\u2013"), -1);
            for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
                refBox->addItem(QString::number(channel + 1), channel);
            connect(refBox, &QComboBox::currentIndexChanged, this, [this, refBox] (int index) {
                _oscilloscope->setReferenceChannel(refBox->itemData(index).toInt());
            });
            grid->addWidget(new QLabel(tr("axis")), row, 0);
            grid->addWidget(refBox, row, 1);
            row++;
        }
        {
            QComboBox * const xBox = new QComboBox;
            QComboBox * const yBox = new QComboBox;
            for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
            {
                xBox->addItem(QString::number(channel + 1));
                yBox->addItem(QString::number(channel + 1));
            }
            xBox->setCurrentIndex(0);
            yBox->setCurrentIndex(1);
            xBox->setToolTip(tr("x/y scope horizontal channel"));
            yBox->setToolTip(tr("x/y scope vertical channel"));
            connect(xBox, &QComboBox::currentIndexChanged, this, [this] (int channel) {
                _xyOscilloscope->setXChannel(channel);
                slot_Redraw();
            });
            connect(yBox, &QComboBox::currentIndexChanged, this, [this] (int channel) {
                _xyOscilloscope->setYChannel(channel);
                slot_Redraw();
            });
            grid->addWidget(new QLabel(tr("x/y")), row, 0);
            grid->addWidget(xBox, row, 1);
            grid->addWidget(yBox, row, 2);
            row++;
        }
        {
            QGroupBox * const box = new QGroupBox(tr("measured"));
            QVBoxLayout * const boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(6, 4, 6, 4);
            boxLayout->setSpacing(1);
            for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
            {
                QLabel * const label = new QLabel(QString("%1: %2").arg(channel + 1).arg(_rec.pins[channel]));
                QPalette pal = label->palette();
                pal.setColor(QPalette::WindowText, SCOPE_CHANNEL_COLORS[channel]);
                label->setPalette(pal);
                label->setTextInteractionFlags(Qt::TextSelectableByMouse);
                boxLayout->addWidget(label);
            }
            grid->addWidget(box, row, 0, 1, 3);
            row++;
        }
        {
            // what the file says about itself, so a recording from last week
            // can be read without remembering how it was taken
            const int n = _rec.sampleCount();
            QStringList info;
            info.append(QFileInfo(_rec.path).fileName());
            if (!_rec.date.isEmpty())
                info.append(tr("recorded %1").arg(_rec.date));
            if (_rec.sendStep > 0.0)
                info.append(tr("send_step %1, %2 samples/s").arg(_rec.sendStep).arg(1.0/_SamplePeriod(), 0, 'g', 6));
            else
                info.append(tr("send_step unknown, timing assumes %1").arg(FIRMWARE_DEFAULT_SEND_STEP));
            info.append(tr("%1 samples, %2").arg(n).arg(_TimeText(n)));
            QLabel * const infoLabel = new QLabel(info.join('\n'));
            infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            if (!_rec.scalingChanges.isEmpty())
            {
                // the values are engineering units either side of a change,
                // but the resolution and the clip rails are not
                infoLabel->setText(infoLabel->text() + '\n' + tr("%n scaling change(s) mid recording", "", _rec.scalingChanges.size()));
                infoLabel->setToolTip(_rec.scalingChanges.join('\n'));
            }
            QGroupBox * const box = new QGroupBox(tr("recording"));
            QVBoxLayout * const boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(6, 4, 6, 4);
            boxLayout->addWidget(infoLabel);
            grid->addWidget(box, row, 0, 1, 3);
            row++;
        }
        grid->setRowStretch(row, 1);
    }
    outer->addWidget(panel);

    connect(_scrollBar, &QScrollBar::valueChanged, this, [this] (int value) {
        if (!_playTimer->isActive() || static_cast<int>(_playPosition) != value)
            _playPosition = value; // the user dragged it, so play on from there
        slot_Redraw();
    });
    connect(_oscilloscope, &Oscilloscope::plotWidthChanged, this, [this] (int) {
        _UpdateScrollRange();
        slot_Redraw();
    });
    connect(_oscilloscope, &Oscilloscope::cursorSampleChanged, this, &PlaybackWindow::slot_CursorSampleChanged);
    connect(_zoomBox, &QComboBox::currentIndexChanged, this, [this] (int) {
        _UpdateScrollRange();
        slot_Redraw();
    });
    connect(_xyCheck, &QCheckBox::toggled, this, [this] (bool shown) {
        _xyOscilloscope->setVisible(shown);
        slot_Redraw();
    });
    connect(_playButton, &QPushButton::toggled, this, &PlaybackWindow::slot_PlayToggled);
    connect(_playTimer, &QTimer::timeout, this, &PlaybackWindow::slot_PlayTick);

    resize(1100, 520);
    _UpdateScrollRange();
    slot_Redraw();
    slot_CursorSampleChanged(-1);
}

int PlaybackWindow::_SamplesPerPixel() const
{
    return qMax(1, _zoomBox->currentData().toInt());
}

double PlaybackWindow::_SamplePeriod() const
{
    return RT_PERIOD_S*(_rec.sendStep > 0.0 ? _rec.sendStep : FIRMWARE_DEFAULT_SEND_STEP);
}

QString PlaybackWindow::_TimeText(double sample) const
{
    const double t = sample*_SamplePeriod();
    if (t < 1.0)
        return QString("%1 ms").arg(t*1000.0, 0, 'f', 1);
    return QString("%1 s").arg(t, 0, 'f', 3);
}

void PlaybackWindow::_UpdateScrollRange()
{
    const int n = _rec.sampleCount();
    const int span = _oscilloscope->plotWidth()*_SamplesPerPixel();
    const QSignalBlocker blocker(_scrollBar);
    _scrollBar->setRange(0, qMax(0, n - span));
    _scrollBar->setPageStep(qMax(1, span));
    _scrollBar->setSingleStep(qMax(1, span/20));
}

void PlaybackWindow::slot_Redraw()
{
    const int n = _rec.sampleCount();
    const int step = _SamplesPerPixel();
    const int start = _scrollBar->value();
    const int width = _oscilloscope->plotWidth();

    QVector< QVector<float> > samples;
    samples.reserve(width);
    _shownSamples.resize(0);
    QVector<float> sample(SCOPE_CHANNEL_COUNT);
    for (int x = 0; x < width; x++)
    {
        const qint64 index = start + static_cast<qint64>(x)*step;
        if (index >= n)
            break;
        for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
        {
            // what the drive would have sent at this gain and offset, rails
            // included, so clipping reads the same as it does live
            const double normalised = (_rec.value(index, channel) + _displayOffset[channel])*_displayGain[channel]/128.0;
            sample[channel] = static_cast<float>(qBound(static_cast<double>(SCOPE_CLAMP_LOW), normalised, static_cast<double>(SCOPE_CLAMP_HIGH)));
        }
        samples.append(sample);
        _shownSamples.append(static_cast<int>(index));
    }
    _oscilloscope->setSamples(samples);
    if (_xyOscilloscope->isVisible())
    {
        _xyOscilloscope->resetScanning();
        for (const QVector<float> &s : samples)
            _xyOscilloscope->addChannelsSample(s);
    }

    const int last = _shownSamples.isEmpty() ? start : _shownSamples.last();
    _positionLabel->setText(tr("%1 to %2 of %3   (samples %4 to %5 of %6)")
                                .arg(_TimeText(start)).arg(_TimeText(last)).arg(_TimeText(n))
                                .arg(start).arg(last).arg(n));
    // the pointer stays put while the trace moves under it
    slot_CursorSampleChanged(_cursorPixel);
}

void PlaybackWindow::slot_CursorSampleChanged(int sample)
{
    _cursorPixel = sample;
    if (sample < 0 || sample >= _shownSamples.size())
    {
        _cursorLabel->setText(tr("cursor: point at the trace for time and values"));
        return;
    }
    const int index = _shownSamples.at(sample);
    QStringList values;
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        if (_channelEnabled[channel])
            values.append(QString("%1: %2").arg(channel + 1).arg(_rec.value(index, channel), 0, 'g', 6));
    }
    // the recorded value, not the one re-quantised for display
    _cursorLabel->setText(tr("cursor: sample %1, %2    %3").arg(index).arg(_TimeText(index)).arg(values.join(QStringLiteral("   "))));
}

void PlaybackWindow::slot_PlayToggled(bool playing)
{
    _playButton->setText(playing ? tr("Pause") : tr("Play"));
    if (playing)
    {
        if (_scrollBar->value() >= _scrollBar->maximum())
            _scrollBar->setValue(0); // at the end, so start over
        _playPosition = _scrollBar->value();
        _lastTickMs = QDateTime::currentMSecsSinceEpoch();
        _playTimer->start();
    }
    else
    {
        _playTimer->stop();
    }
}

void PlaybackWindow::slot_PlayTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = (now - _lastTickMs)/1000.0;
    _lastTickMs = now;
    const double speed = _speedBox->currentData().toDouble();
    _playPosition += dt*speed/_SamplePeriod();
    if (_playPosition >= _scrollBar->maximum())
    {
        _playPosition = _scrollBar->maximum();
        _scrollBar->setValue(_scrollBar->maximum());
        _playButton->setChecked(false);
        return;
    }
    _scrollBar->setValue(static_cast<int>(_playPosition));
}

void PlaybackWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
        case Qt::Key_Space:
            _playButton->toggle();
            return;
        case Qt::Key_Left:
            _scrollBar->triggerAction(QAbstractSlider::SliderSingleStepSub);
            return;
        case Qt::Key_Right:
            _scrollBar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
            return;
        case Qt::Key_PageUp:
            _scrollBar->triggerAction(QAbstractSlider::SliderPageStepSub);
            return;
        case Qt::Key_PageDown:
            _scrollBar->triggerAction(QAbstractSlider::SliderPageStepAdd);
            return;
        case Qt::Key_Home:
            _scrollBar->setValue(0);
            return;
        case Qt::Key_End:
            _scrollBar->setValue(_scrollBar->maximum());
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(event);
}

} // namespace STMBL_Servoterm
