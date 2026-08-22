/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// FILE: HazardFieldDecalClientUpdate.cpp ///////////////////////////////////////////////////////////
// Desc:   Paints a terrain-conforming decal under a hazard field (anthrax, radiation, poison).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// TheShadowNames, which the Style field parses against, is only defined for the one
// translation unit that asks for it. RadiusDecal.cpp does the same.
#define DEFINE_SHADOW_NAMES

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/Module/HazardFieldDecalClientUpdate.h"
#include "GameClient/Shadow.h"
#include "GameLogic/Object.h"

//-------------------------------------------------------------------------------------------------
HazardFieldDecalClientUpdateModuleData::HazardFieldDecalClientUpdateModuleData() :
	m_textureName(AsciiString::TheEmptyString),
	m_style(SHADOW_ALPHA_DECAL),
	m_color(0xffffffff),
	m_radius(0.0f),
	m_startRadiusFraction(1.0f),
	m_opacity(1.0f),
	m_growthTime(0),
	m_fadeInTime(0),
	m_fadeOutTime(0),
	m_lifetime(0),
	m_rotationRate(0.0f),
	m_pulseTime(0),
	m_pulseAmount(0.0f),
	m_pulsePhase(0.0f),
	m_breatheTime(0),
	m_breatheAmount(0.0f),
	m_motionRampTime(0),
	m_motionStartScale(1.0f)
{
}

//-------------------------------------------------------------------------------------------------
/*static*/ void HazardFieldDecalClientUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "Texture",							INI::parseAsciiString,				nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_textureName ) },
		{ "Style",								INI::parseBitString32,				TheShadowNames,		offsetof( HazardFieldDecalClientUpdateModuleData, m_style ) },
		{ "Color",								INI::parseColorInt,						nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_color ) },
		{ "Radius",								INI::parseReal,								nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_radius ) },
		{ "StartRadiusFraction",	INI::parsePercentToReal,			nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_startRadiusFraction ) },
		{ "Opacity",							INI::parsePercentToReal,			nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_opacity ) },
		{ "GrowthTime",						INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_growthTime ) },
		{ "FadeInTime",						INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_fadeInTime ) },
		{ "FadeOutTime",					INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_fadeOutTime ) },
		{ "Lifetime",							INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_lifetime ) },
		{ "RotationRate",					INI::parseReal,								nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_rotationRate ) },
		{ "PulseTime",						INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_pulseTime ) },
		{ "PulseAmount",					INI::parsePercentToReal,			nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_pulseAmount ) },
		{ "PulsePhase",						INI::parsePercentToReal,			nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_pulsePhase ) },
		{ "BreatheTime",					INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_breatheTime ) },
		{ "BreatheAmount",				INI::parsePercentToReal,			nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_breatheAmount ) },
		{ "MotionRampTime",				INI::parseDurationUnsignedInt,nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_motionRampTime ) },
		{ "MotionStartScale",			INI::parsePercentToReal,			nullptr,					offsetof( HazardFieldDecalClientUpdateModuleData, m_motionStartScale ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HazardFieldDecalClientUpdate::HazardFieldDecalClientUpdate( Thing *thing, const ModuleData* moduleData ) :
	ClientUpdateModule( thing, moduleData ),
	m_decal(nullptr),
	m_createFrame(0),
	m_angle(0.0f),
	m_triedToCreate(false)
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HazardFieldDecalClientUpdate::~HazardFieldDecalClientUpdate()
{
	releaseDecal();
}

//-------------------------------------------------------------------------------------------------
void HazardFieldDecalClientUpdate::releaseDecal()
{
	if (m_decal)
	{
		m_decal->release();
		m_decal = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
void HazardFieldDecalClientUpdate::createDecal()
{
	const HazardFieldDecalClientUpdateModuleData* d = getHazardFieldDecalClientUpdateModuleData();

	// Latched whether or not the create succeeds. Without it a field whose texture is
	// missing asks the manager for a decal on every one of its 1800 frames.
	m_triedToCreate = true;

	if (d->m_textureName.isEmpty() || d->m_radius <= 0.0f)
		return;

	if (TheProjectedShadowManager == nullptr)
		return;

	Drawable* draw = getDrawable();
	if (draw == nullptr)
		return;

	Shadow::ShadowTypeInfo decalInfo;
	decalInfo.allowUpdates = FALSE;				// the texture itself never changes
	// The whole reason for using a decal at all: TRUE sends this down the path that
	// tessellates the mark over the heightmap instead of laying one flat quad at the
	// centre's ground height, which would cut straight through any slope.
	decalInfo.allowWorldAlign = TRUE;
	decalInfo.m_type = d->m_style;
	strlcpy(decalInfo.m_ShadowName, d->m_textureName.str(), ARRAY_SIZE(decalInfo.m_ShadowName));

	const Real startRadius = d->m_radius * d->m_startRadiusFraction;
	decalInfo.m_sizeX = startRadius * 2.0f;
	decalInfo.m_sizeY = startRadius * 2.0f;

	m_decal = TheProjectedShadowManager->addDecal(&decalInfo);
	if (m_decal == nullptr)
	{
		DEBUG_LOG(("HazardFieldDecalClientUpdate: could not add decal %s", d->m_textureName.str()));
		return;
	}

	const Coord3D* pos = draw->getPosition();
	m_decal->setPosition(pos->x, pos->y, pos->z);
	m_decal->setColor(d->m_color);
	// Every field of the same kind would otherwise be stamped at the same yaw, which is
	// exactly the repeating signature the sprites suffered from. One texture is fine as
	// long as each mark is turned differently.
	m_angle = GameClientRandomValueReal(0.0f, 2.0f * PI);
	m_decal->setAngle(m_angle);
	m_decal->setOpacity(0);
	m_createFrame = TheGameClient->getFrame();
}

//-------------------------------------------------------------------------------------------------
void HazardFieldDecalClientUpdate::clientUpdate()
{
	if (!m_triedToCreate)
		createDecal();

	if (m_decal == nullptr)
		return;

	const HazardFieldDecalClientUpdateModuleData* d = getHazardFieldDecalClientUpdateModuleData();
	const UnsignedInt now = TheGameClient->getFrame();
	const UnsignedInt age = (now >= m_createFrame) ? (now - m_createFrame) : 0;

	// Opacity: up over the fade-in, held, then down over the fade-out that ends on
	// m_lifetime. A field that outlives its lifetime simply stays gone.
	Real alpha = 1.0f;
	if (d->m_fadeInTime > 0 && age < d->m_fadeInTime)
	{
		alpha = (Real)age / (Real)d->m_fadeInTime;
	}
	if (d->m_lifetime > 0)
	{
		if (age >= d->m_lifetime)
		{
			alpha = 0.0f;
		}
		else if (d->m_fadeOutTime > 0 && age > d->m_lifetime - d->m_fadeOutTime)
		{
			const Real remaining = (Real)(d->m_lifetime - age);
			alpha = min(alpha, remaining / (Real)d->m_fadeOutTime);
		}
	}

	// How hard everything below moves. Eased rather than linear so the field does not
	// visibly click into gear at the end of the ramp.
	Real motion = 1.0f;
	if (d->m_motionRampTime > 0 && age < d->m_motionRampTime)
	{
		const Real t = (Real)age / (Real)d->m_motionRampTime;
		const Real eased = t * t * (3.0f - 2.0f * t);
		motion = d->m_motionStartScale + eased * (1.0f - d->m_motionStartScale);
	}

	// Pulse. Layers given different PulsePhase values swell and ebb against each other,
	// which is most of what makes the patch look like it is still reacting rather than
	// painted on.
	Real pulse = 1.0f;
	if (d->m_pulseTime > 0 && d->m_pulseAmount > 0.0f)
	{
		const Real theta = 2.0f * PI * (((Real)(age % d->m_pulseTime) / (Real)d->m_pulseTime) + d->m_pulsePhase);
		pulse = 1.0f - d->m_pulseAmount * motion * 0.5f * (1.0f - Sin(theta));
	}

	alpha = (alpha < 0.0f) ? 0.0f : ((alpha > 1.0f) ? 1.0f : alpha);
	m_decal->setOpacity(REAL_TO_INT(alpha * pulse * d->m_opacity * 255.0f));

	// Rotation turns the texture frame about the mark's centre. queueDecal rebuilds the
	// decal's UVs from m_localAngle every frame, so simply moving the angle animates the
	// interior; nothing has to be re-created. Degrees per second in, radians out, against
	// the client frame rate rather than wall clock so it keeps step with everything else.
	if (d->m_rotationRate != 0.0f)
	{
		m_angle += d->m_rotationRate * motion * (PI / 180.0f) / (Real)LOGICFRAMES_PER_SECOND;
		m_decal->setAngle(m_angle);
	}

	// Spread. The gas does not arrive everywhere at once and neither should the mark it
	// leaves; growing the decal is also what stops the mark's edge from being a hard
	// circle that appears in one frame.
	Real frac = 1.0f;
	if (d->m_growthTime > 0 && age < d->m_growthTime && d->m_startRadiusFraction < 1.0f)
	{
		const Real t = (Real)age / (Real)d->m_growthTime;
		frac = d->m_startRadiusFraction + t * (1.0f - d->m_startRadiusFraction);
	}

	// Breathing, on top of the growth. Slow and small: this is a mark creeping at its
	// edges, not a thing inflating.
	if (d->m_breatheTime > 0 && d->m_breatheAmount > 0.0f)
	{
		const Real theta = 2.0f * PI * (Real)(age % d->m_breatheTime) / (Real)d->m_breatheTime;
		frac *= 1.0f + d->m_breatheAmount * motion * 0.5f * Sin(theta);
	}

	if (frac != 1.0f)
	{
		const Real size = d->m_radius * frac * 2.0f;
		m_decal->setSize(size, size);
	}

	// Shroud, as the draw modules do it for their own shadows: a mark in fog of war is
	// something the local player has no business seeing.
	const Object* obj = getDrawable()->getObject();
	if (obj)
	{
		const ObjectShroudStatus ss = obj->getShroudedStatus(ThePlayerList->getLocalPlayer()->getPlayerIndex());
		m_decal->enableShadowInvisible(ss == OBJECTSHROUD_SHROUDED);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HazardFieldDecalClientUpdate::crc( Xfer *xfer )
{

	// extend base class
	ClientUpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	*
	* Nothing of the decal itself is saved. It is a mark on the ground with no state worth
	* keeping: on load the module finds m_triedToCreate false and lays a fresh one down,
	* which also re-rolls its angle. The only visible consequence is that a field saved
	* half-faded comes back at the start of its fade, and that is cheaper than persisting
	* a client-side handle into the shadow manager. */
// ------------------------------------------------------------------------------------------------
void HazardFieldDecalClientUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	ClientUpdateModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HazardFieldDecalClientUpdate::loadPostProcess()
{

	// extend base class
	ClientUpdateModule::loadPostProcess();

}
