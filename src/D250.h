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

#ifndef STMBL_SERVOTERM_D250_H
#define STMBL_SERVOTERM_D250_H

// the term protocol v1 payload encoding, kept free of qt so it can be tested
// on its own. a frame's payload bytes are all in 1..250, so the marker bytes
// 0xFB..0xFF can never turn up inside one: every number is written as base 250
// digits, most significant first, each stored as digit + 1.

#include <cstdint>
#include <cstring>

namespace STMBL_Servoterm {

static const int D250_BASE = 250;
static const int D250_F32_DIGITS = 5; // 250^5 > 2^32
static const int D250_U16_DIGITS = 2;
static const int D250_U8_DIGITS = 1;

// false on a byte outside 1..250, which no encoder could have produced
inline bool D250Decode(const unsigned char *bytes, int digits, uint64_t &value)
{
    value = 0;
    for (int i = 0; i < digits; i++)
    {
        if (bytes[i] < 1 || bytes[i] > D250_BASE)
            return false;
        value = value*D250_BASE + (bytes[i] - 1);
    }
    return true;
}

// the float's bits travel as an integer, so NaN, inf and -0 survive the trip.
// five digits can hold more than 32 bits, so anything above that is garbage.
inline bool D250DecodeF32(const unsigned char *bytes, float &value)
{
    uint64_t raw = 0;
    if (!D250Decode(bytes, D250_F32_DIGITS, raw) || raw > 0xFFFFFFFFull)
        return false;
    const uint32_t bits = static_cast<uint32_t>(raw);
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_D250_H
