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

#ifndef STMBL_SERVOTERM_SCOPEDATADEMUX_H
#define STMBL_SERVOTERM_SCOPEDATADEMUX_H

#include <QObject>
#include <QVector>
#include <QByteArray>

namespace STMBL_Servoterm {

class ScopeDataDemux : public QObject
{
    Q_OBJECT
public:
    ScopeDataDemux(QObject *parent = nullptr);
    QString addData(const QByteArray &data);
    // forget any half received frame or capture, for a fresh connection
    void reset();
signals:
    void scopePacketReceived(const QVector<float> &packet);
    void scopeResetReceived();
    // the drive's seq jumped, so this many live samples never arrived. it is
    // emitted before the NaN packets that stand in for (up to 64 of) them.
    void scopeSamplesLost(int count);
    // kind 0 is the live stream, kind 1 announces a capture upload
    void scopeHeaderReceived(int kind, double period, int count, int pre);
    // which wire format the live samples are arriving in, on a change
    void scopeFormatChanged(bool isFloat);
    // one QVector<float> of SCOPE_CHANNEL_COUNT values per capture sample,
    // NaN where an index never arrived. sample pre is the trigger.
    void captureReceived(const QVector< QVector<float> > &samples, double period, int pre);
protected:
    void _StartByte(char byte, QString &txt);
    void _HandleFrame();
    void _HandleFloatSample(const unsigned char *payload);
    void _HandleHeader(const unsigned char *payload);
    void _HandleCaptureSample(const unsigned char *payload);
    void _FinishCapture();
    void _SetFormat(int format);
    enum State
    {
        SCOPEDATADEMUX_STATE_IDLE = 0,
        // the legacy 0xFF packet, whose payload may legitimately hold 0xFE
        SCOPEDATADEMUX_STATE_READING_PACKET,
        // a 0xFD/0xFC/0xFB frame, whose payload never holds a marker
        SCOPEDATADEMUX_STATE_READING_FRAME
    } _state;
    QVector<float> _packet;
    quint8 _frameMarker;
    int _frameLength;
    bool _frameBad;
    QByteArray _frame;
    // seq of the last float sample, or -1 when the next one starts afresh
    int _lastSeq;
    // 0 unknown, 1 legacy bytes, 2 float
    int _format;
    bool _capturing;
    QVector< QVector<float> > _capture;
    double _capturePeriod;
    int _capturePre;
};

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_SCOPEDATADEMUX_H
