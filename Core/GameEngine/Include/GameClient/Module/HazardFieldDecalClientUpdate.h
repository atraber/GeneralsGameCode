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

// FILE: HazardFieldDecalClientUpdate.h /////////////////////////////////////////////////////////////
// Desc:   Paints a terrain-conforming decal under a hazard field (anthrax, radiation, poison).
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/ClientUpdateModule.h"
#include "GameClient/Color.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;
class Shadow;
enum ShadowType CPP_11(: Int);

//-------------------------------------------------------------------------------------------------
/** What a hazard field leaves on the ground.
	*
	* The fields have always been sprites and nothing else: a scatter of ground-aligned
	* quads, one texture between anthrax, poison and radiation, additive so contamination
	* could only ever brighten the sand it fell on. Sprites are the wrong primitive for a
	* mark on the ground and fail in two ways that no amount of tuning reaches. Hundreds of
	* semi-transparent stamps laid over each other average towards their own mean, so the
	* more of them there are the flatter the field reads; and every stamp samples the same
	* texture, so any large-scale shape in it repeats across the whole field and announces
	* itself -- a ring, in the first build of this.
	*
	* A decal has neither problem. It is one piece of geometry carrying one texture, so its
	* detail survives, and the projected-decal path already tessellates over the heightmap
	* and follows the terrain's own triangle flip, so it cannot z-fight the ground or float
	* over a slope the way a flat quad does.
	*
	* This is a client update rather than an update module because it is purely a visual:
	* it must not reach the logic frame or the CRC. */
//-------------------------------------------------------------------------------------------------
class HazardFieldDecalClientUpdateModuleData : public ClientUpdateModuleData
{
public:
	AsciiString		m_textureName;			///< decal texture
	ShadowType		m_style;						///< SHADOW_ALPHA_DECAL or SHADOW_ADDITIVE_DECAL
	Color					m_color;						///< tint applied to the texture
	Real					m_radius;						///< world radius at full spread
	Real					m_startRadiusFraction;	///< fraction of m_radius the mark starts at
	Real					m_opacity;					///< peak opacity, 0..1
	UnsignedInt		m_growthTime;				///< frames to reach full radius
	UnsignedInt		m_fadeInTime;				///< frames to reach peak opacity
	UnsignedInt		m_fadeOutTime;			///< frames spent fading out at the end of life
	UnsignedInt		m_lifetime;					///< total visible life in frames; 0 = never fade out

	// Motion. A contaminated patch that holds perfectly still reads as a sticker; these
	// let a layer turn, breathe and pulse so the ground looks like it is still reacting.
	// Rotation turns the texture about the mark's own centre, so it churns the interior
	// without the patch appearing to move -- provided the layer's alpha is roughly
	// radially symmetric. A layer carrying the outline must not rotate.
	Real					m_rotationRate;			///< degrees per second the texture frame turns
	UnsignedInt		m_pulseTime;				///< frames per opacity cycle; 0 = steady
	Real					m_pulseAmount;			///< fraction of opacity swung by the pulse
	Real					m_pulsePhase;				///< 0..1 offset into the pulse, to set layers against each other
	UnsignedInt		m_breatheTime;			///< frames per size cycle; 0 = fixed size
	Real					m_breatheAmount;		///< fraction of radius swung by the breathe

	// Motion ramp. While the field is still spreading, the mark should be settling rather
	// than churning -- the drama belongs after the outflow, when the ground has been
	// contaminated a while and is visibly working. Motion starts at m_motionStartScale and
	// reaches full strength over m_motionRampTime.
	UnsignedInt		m_motionRampTime;		///< frames to reach full motion; 0 = full at once
	Real					m_motionStartScale;	///< motion multiplier at age zero

	HazardFieldDecalClientUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class HazardFieldDecalClientUpdate : public ClientUpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( HazardFieldDecalClientUpdate, "HazardFieldDecalClientUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( HazardFieldDecalClientUpdate, HazardFieldDecalClientUpdateModuleData )

public:

	HazardFieldDecalClientUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void clientUpdate() override;

protected:

	void createDecal();
	void releaseDecal();

	Shadow*			m_decal;					///< the projected decal, owned by TheProjectedShadowManager
	UnsignedInt	m_createFrame;		///< client frame the mark was laid down on
	Real				m_angle;					///< accumulated texture angle; integrated, since the rate varies
	Bool				m_triedToCreate;	///< so a failed create is not retried every frame
};
