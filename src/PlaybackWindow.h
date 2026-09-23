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

#ifndef STMBL_SERVOTERM_PLAYBACKWINDOW_H
#define STMBL_SERVOTERM_PLAYBACKWINDOW_H

#include "globals.h"

#include <QWidget>
#include <QVector>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QScrollBar;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
class QTimer;
QT_END_NAMESPACE

namespace STMBL_Servoterm {

class Oscilloscope;
class XYOscilloscope;

// A recording as MainWindow writes it: a commented header naming send_step and
// each channel's pin, gain and offset, then "sample,ch1..ch8" rows in
// engineering units, with "# at sample N: chK gain|offset a -> b" lines where
// the scaling changed mid recording.
struct ScopeRecording
{
    QString path;
    QString date;
    double sendStep = 0.0;  // 0 when the header says unknown
    QString pins[SCOPE_CHANNEL_COUNT];
    double gain[SCOPE_CHANNEL_COUNT];
    double offset[SCOPE_CHANNEL_COUNT];
    QStringList scalingChanges;
    QVector<float> values;  // row major, SCOPE_CHANNEL_COUNT per sample
    int sampleCount() const { return values.size()/SCOPE_CHANNEL_COUNT; }
    float value(int sample, int channel) const { return values.at(sample*SCOPE_CHANNEL_COUNT + channel); }
    bool load(const QString &filePath, QString *error);
};

// Plays a recording back through the same scope, x/y scope and channel panel
// as the live view. Gain and offset here only rescale the display; nothing is
// sent to a drive.
class PlaybackWindow : public QWidget
{
    Q_OBJECT
public:
    PlaybackWindow(const ScopeRecording &recording, QWidget *parent = nullptr);
protected slots:
    void slot_Redraw();
    void slot_PlayToggled(bool playing);
    void slot_PlayTick();
    void slot_CursorSampleChanged(int sample);
protected:
    void keyPressEvent(QKeyEvent *event);
    void _UpdateScrollRange();
    int _SamplesPerPixel() const;
    // rt runs at 5 kHz and term0 sends every send_step ticks
    double _SamplePeriod() const;
    QString _TimeText(double sample) const;

    ScopeRecording _rec;
    Oscilloscope *_oscilloscope;
    XYOscilloscope *_xyOscilloscope;
    QScrollBar *_scrollBar;
    QLabel *_positionLabel;
    QLabel *_cursorLabel;
    QPushButton *_playButton;
    QComboBox *_speedBox;
    QComboBox *_zoomBox;
    QCheckBox *_xyCheck;
    QTimer *_playTimer;
    double _playPosition;
    qint64 _lastTickMs;
    bool _channelEnabled[SCOPE_CHANNEL_COUNT];
    double _displayGain[SCOPE_CHANNEL_COUNT];
    double _displayOffset[SCOPE_CHANNEL_COUNT];
    QVector<int> _shownSamples;  // recording index behind each plotted pixel
    int _cursorPixel;            // where the pointer is over the plot, -1 off it
};

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_PLAYBACKWINDOW_H
