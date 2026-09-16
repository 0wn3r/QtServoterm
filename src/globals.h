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

#ifndef STMBL_SERVOTERM_GLOBALS_H
#define STMBL_SERVOTERM_GLOBALS_H

#include <QColor>

namespace STMBL_Servoterm {

static const int SCOPE_CHANNEL_COUNT = 8;

static const QColor SCOPE_CHANNEL_COLORS[SCOPE_CHANNEL_COUNT] =
{
    Qt::black,
    Qt::red,
    Qt::blue,
    Qt::green,
    QColor(255, 128, 0),
    QColor(128, 128, 64),
    QColor(128, 64, 128),
    QColor(64, 128, 128)
};

} // namespace STMBL_Servoterm

#endif // STMBL_SERVOTERM_GLOBALS_H
