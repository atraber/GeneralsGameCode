/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "Lib/BaseType.h"
#include "Common/GameType.h"

// TheSuperHackers @feature andytraber 08/09/2026 Dynamic day-night cycle with creeping shadows.
// Configurable via Options.ini DayNightCycleDuration (default 20 min, 0 disables).
// Respects 16h Day (2/3) vs 8h Night (1/3) cycle proportions and begins from the map's authored settings.

void DayNightCycle_Init();
void DayNightCycle_Reset();
void DayNightCycle_Update(UnsignedInt logicFrame);
void DayNightCycle_SetTimeOfDay(TimeOfDay tod);

Bool DayNightCycle_IsEnabled();
Real DayNightCycle_GetCurrentGameHour();
TimeOfDay DayNightCycle_GetCurrentTimeOfDay();
