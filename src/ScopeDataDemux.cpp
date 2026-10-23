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

#include "ScopeDataDemux.h"
#include "D250.h"
#include "globals.h"

#include <limits>

namespace STMBL_Servoterm {

// term protocol v1 markers. every byte from 0xFB up starts something, and the
// new frames keep their payload in 1..250, so none of them can occur inside one.
static const quint8 MARKER_CAPTURE_SAMPLE = 0xFB;
static const quint8 MARKER_HEADER = 0xFC;
static const quint8 MARKER_FLOAT_SAMPLE = 0xFD;
static const quint8 MARKER_RESET = 0xFE;
static const quint8 MARKER_LEGACY_SAMPLE = 0xFF;

static const int FLOAT_SAMPLE_LENGTH = D250_U8_DIGITS + SCOPE_CHANNEL_COUNT*D250_F32_DIGITS;
static const int HEADER_FIELDS = 6;
static const int HEADER_LENGTH = HEADER_FIELDS*D250_F32_DIGITS;
static const int CAPTURE_SAMPLE_LENGTH = D250_U16_DIGITS + SCOPE_CHANNEL_COUNT*D250_F32_DIGITS;

// a long dropout should show as a hole, not as a screen full of nothing
static const int MAX_GAP_SAMPLES = 64;
// a u16 index tops out here, so no honest header asks for more
static const int MAX_CAPTURE_COUNT = D250_BASE*D250_BASE;

enum Format
{
    FORMAT_UNKNOWN = 0,
    FORMAT_LEGACY,
    FORMAT_FLOAT
};

ScopeDataDemux::ScopeDataDemux(QObject *parent) :
    QObject(parent),
    _state(SCOPEDATADEMUX_STATE_IDLE),
    _frameMarker(0),
    _frameLength(0),
    _frameBad(false),
    _lastSeq(-1),
    _format(FORMAT_UNKNOWN),
    _capturing(false),
    _capturePeriod(0.0),
    _capturePre(0)
{
}

void ScopeDataDemux::reset()
{
    _state = SCOPEDATADEMUX_STATE_IDLE;
    _packet.resize(0);
    _frame.resize(0);
    _lastSeq = -1;
    _format = FORMAT_UNKNOWN;
    _capturing = false;
    _capture.clear();
}

QString ScopeDataDemux::addData(const QByteArray &data)
{
    QString txt;
    for (QByteArray::const_iterator it = data.begin(); it != data.end(); ++it)
    {
        if(_state == SCOPEDATADEMUX_STATE_READING_PACKET)
        {
            // legacy payload is any byte 1..254, 0xFE included, so a marker
            // cannot interrupt it. unchanged from the 8-bit protocol.
            _packet.append((static_cast<int>(static_cast<quint8>(*it)) - 128) / 128.0);
            if(_packet.size() == SCOPE_CHANNEL_COUNT)
            {
                // save the packet
                QVector<float> packet = _packet;
                // reset the state
                _state = SCOPEDATADEMUX_STATE_IDLE;
                _packet.resize(0);
                _lastSeq = -1; // a legacy sample breaks any float seq run
                _SetFormat(FORMAT_LEGACY);
                // dispatch the packet
                emit scopePacketReceived(packet);
            }
        }
        else if (_state == SCOPEDATADEMUX_STATE_READING_FRAME)
        {
            const quint8 byte = static_cast<quint8>(*it);
            if (byte >= MARKER_CAPTURE_SAMPLE)
            {
                // the frame was cut short (the drive reset or dropped bytes).
                // markers never occur in this payload, so this byte really
                // does start something new.
                _state = SCOPEDATADEMUX_STATE_IDLE;
                _frame.resize(0);
                _StartByte(*it, txt);
            }
            else
            {
                // 0 is not a digit (251 and up are markers, handled above),
                // so the frame is corrupt. it is still swallowed to its full
                // length, so the rest of it does not spill into the console.
                if (byte == 0 || byte > D250_BASE)
                    _frameBad = true;
                _frame.append(*it);
                if (_frame.size() == _frameLength)
                {
                    _state = SCOPEDATADEMUX_STATE_IDLE;
                    if (!_frameBad)
                        _HandleFrame();
                    _frame.resize(0);
                }
            }
        }
        else
        {
            _StartByte(*it, txt);
        }
    }
    return txt;
}

void ScopeDataDemux::_StartByte(char byte, QString &txt)
{
    switch (static_cast<quint8>(byte))
    {
        case MARKER_LEGACY_SAMPLE:
        _state = SCOPEDATADEMUX_STATE_READING_PACKET;
        _packet.resize(0);
        break;

        case MARKER_RESET:
        // only a marker between packets: inside a legacy packet it is data
        emit scopeResetReceived();
        break;

        case MARKER_FLOAT_SAMPLE:
        case MARKER_HEADER:
        case MARKER_CAPTURE_SAMPLE:
        _state = SCOPEDATADEMUX_STATE_READING_FRAME;
        _frameMarker = static_cast<quint8>(byte);
        _frameLength = (_frameMarker == MARKER_FLOAT_SAMPLE) ? FLOAT_SAMPLE_LENGTH
                     : (_frameMarker == MARKER_HEADER) ? HEADER_LENGTH
                     : CAPTURE_SAMPLE_LENGTH;
        _frame.resize(0);
        _frameBad = false;
        break;

        default:
        txt.append(QChar::fromLatin1(byte));
        break;
    }
}

void ScopeDataDemux::_HandleFrame()
{
    const unsigned char * const payload = reinterpret_cast<const unsigned char *>(_frame.constData());
    switch (_frameMarker)
    {
        case MARKER_FLOAT_SAMPLE:
        _HandleFloatSample(payload);
        break;

        case MARKER_HEADER:
        _HandleHeader(payload);
        break;

        case MARKER_CAPTURE_SAMPLE:
        _HandleCaptureSample(payload);
        break;

        default:
        break;
    }
}

static bool DecodeChannels(const unsigned char *payload, QVector<float> &channels)
{
    channels.resize(SCOPE_CHANNEL_COUNT);
    for (int channel = 0; channel < SCOPE_CHANNEL_COUNT; channel++)
    {
        float f = 0.0f;
        if (!D250DecodeF32(payload + channel*D250_F32_DIGITS, f))
            return false;
        // the drive sends (wave + offset)*gain, the number the byte path
        // rounds before adding 128, so /128 lands on the same normalised
        // scale. it is not clipped, so it can run past +-1.
        channels[channel] = f/128.0f;
    }
    return true;
}

void ScopeDataDemux::_HandleFloatSample(const unsigned char *payload)
{
    uint64_t seq = 0;
    QVector<float> packet;
    if (!D250Decode(payload, D250_U8_DIGITS, seq) || !DecodeChannels(payload + D250_U8_DIGITS, packet))
        return;

    // seq counts every sample the drive took, sent or not, so a jump of n
    // means n - 1 never made it. a zero jump is a repeat, not a loss.
    if (_lastSeq >= 0)
    {
        const int jump = (static_cast<int>(seq) - _lastSeq + D250_BASE) % D250_BASE;
        if (jump > 1)
        {
            emit scopeSamplesLost(jump - 1);
            // stand-ins, so the trace shows a hole where the time went
            // instead of splicing the two sides together
            const QVector<float> gap(SCOPE_CHANNEL_COUNT, std::numeric_limits<float>::quiet_NaN());
            const int gapSamples = qMin(jump - 1, MAX_GAP_SAMPLES);
            for (int i = 0; i < gapSamples; i++)
                emit scopePacketReceived(gap);
        }
    }
    _lastSeq = static_cast<int>(seq);
    _SetFormat(FORMAT_FLOAT);
    emit scopePacketReceived(packet);
}

void ScopeDataDemux::_HandleHeader(const unsigned char *payload)
{
    float fields[HEADER_FIELDS];
    for (int i = 0; i < HEADER_FIELDS; i++)
    {
        if (!D250DecodeF32(payload + i*D250_F32_DIGITS, fields[i]))
            return;
    }
    // fields: proto, kind, period [s], channels, count, pre
    // compared rather than cast, since a NaN or huge float has no int
    const int kind = (fields[1] == 0.0f) ? 0 : (fields[1] == 1.0f) ? 1 : -1;
    const double period = fields[2];
    const int count = (fields[4] >= 0.0f && fields[4] <= MAX_CAPTURE_COUNT) ? static_cast<int>(fields[4]) : 0;
    const int pre = (fields[5] >= 0.0f && fields[5] <= count) ? static_cast<int>(fields[5]) : 0;

    // any header ends a capture upload: normally the live header that follows
    // it, but a new capture header means the old one will never be completed
    if (_capturing)
        _FinishCapture();
    // the stream has restarted, so the next seq is not a continuation
    _lastSeq = -1;

    emit scopeHeaderReceived(kind, period, count, pre);

    if (kind == 1 && count > 0)
    {
        _capturing = true;
        _capturePeriod = period;
        _capturePre = pre;
        _capture.fill(QVector<float>(SCOPE_CHANNEL_COUNT, std::numeric_limits<float>::quiet_NaN()), count);
    }
}

void ScopeDataDemux::_HandleCaptureSample(const unsigned char *payload)
{
    uint64_t index = 0;
    QVector<float> sample;
    if (!_capturing || !D250Decode(payload, D250_U16_DIGITS, index) || !DecodeChannels(payload + D250_U16_DIGITS, sample))
        return;
    if (index >= static_cast<uint64_t>(_capture.size()))
        return;
    _capture[static_cast<int>(index)] = sample;
    if (static_cast<int>(index) == _capture.size() - 1)
        _FinishCapture();
}

void ScopeDataDemux::_FinishCapture()
{
    _capturing = false;
    const QVector< QVector<float> > samples = _capture;
    _capture.clear();
    emit captureReceived(samples, _capturePeriod, _capturePre);
}

void ScopeDataDemux::_SetFormat(int format)
{
    if (_format == format)
        return;
    _format = format;
    emit scopeFormatChanged(format == FORMAT_FLOAT);
}

} // namespace STMBL_Servoterm
