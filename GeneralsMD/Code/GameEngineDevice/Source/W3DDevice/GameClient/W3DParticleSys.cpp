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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DParticleSys.cpp
// W3D Particle System implementation
// Author: Michael S. Booth, November 2001

#include "Common/GlobalData.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"


//------------------------------------------------------------------------------ Performance Timers
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------


W3DParticleSystemManager::W3DParticleSystemManager()
{
	m_pointGroup = nullptr;
	m_streakLine = nullptr;
	m_posBuffer = nullptr;
	m_RGBABuffer = nullptr;
	m_sizeBuffer = nullptr;
	m_angleBuffer = nullptr;
	m_readyToRender = false;
#ifdef RTS_DEBUG
	m_lastShadowParticleCount = 0;
#endif

	m_onScreenParticleCount = 0;

	m_pointGroup = NEW PointGroupClass();
	//m_streakLine = nullptr;
	m_streakLine = NEW StreakLineClass();

	m_posBuffer = NEW_REF( ShareBufferClass<Vector3>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_posBuffer") );
	m_RGBABuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_RGBABuffer") );
	m_sizeBuffer = NEW_REF( ShareBufferClass<float>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_sizeBuffer") );
	m_angleBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_angleBuffer") );
}

W3DParticleSystemManager::~W3DParticleSystemManager()
{
	delete m_pointGroup;

//	W3DDisplay::m_3DScene->Remove_Render_Object( m_streakLine );

	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
	}

	REF_PTR_RELEASE(m_posBuffer);
	REF_PTR_RELEASE(m_RGBABuffer);
	REF_PTR_RELEASE(m_sizeBuffer);
	REF_PTR_RELEASE(m_angleBuffer);
}

/**
 * Hack because DoParticles is called from Flush(), which is called
 * multiple times per frame.  We only want to render once.
 * @todo Clean up the flag/Flush hack.
 */
void W3DParticleSystemManager::queueParticleRender()
{
	m_readyToRender = true;
}

/**
 * Nasty hack to render particles last. Called directly by WW3D::Flush()
 */
void DoParticles( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticles(rinfo);
}

/**
 * Nasty hack's counterpart for the shadow map. Called directly by the scene's depth pass.
 */
void DoParticleShadows( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticleShadows(rinfo);
}

/**
 * Submit the shadow-casting particle systems into the sun's shadow map.
 *
 * A second pass over the frame's particles, and deliberately not a re-use of doParticles.
 * Three things differ and none of them is a parameter:
 *
 * - It must not touch m_readyToRender. That latch is set once per frame and cleared by
 *   whoever draws first; the depth pass runs before the visible one, so consuming it here
 *   would leave the visible pass with no particles at all.
 * - It culls against the sun's box rather than the camera's visible box. A smoke column
 *   standing off the edge of the screen still casts a shadow that reaches into it, and
 *   the camera cull would have dropped it -- the same mistake the mesh depth pass had to
 *   be corrected for.
 * - It skips everything the shadow map cannot use: systems classified as light rather
 *   than matter, streaks, smudges, and sprites too small to survive a texel.
 */
void W3DParticleSystemManager::doParticleShadows(RenderInfoClass &rinfo)
{
	if (!TheGlobalData->m_useParticleShadows)
		return;
	// The pass and the frustum both have to be real: Render_Sun_Depth checks these too,
	// but a whole frame's worth of array-filling ahead of the check is worth avoiding.
	if (!DX8Wrapper::Is_Shadow_Depth_Pass() || !DX8Wrapper::Has_Sun_Cull_Box())
		return;
	if (m_pointGroup == nullptr)
		return;

	// Sprites below this cast nothing. A shadow texel is between roughly half and two
	// world units across depending on the zoom, so a sprite a couple of units wide is
	// already at the resolution floor -- what it would contribute is not a shadow but a
	// speckle, and there are far more of these (sparks, grit, embers) than there are of
	// anything worth drawing.
	const Real MIN_SHADOW_PARTICLE_SIZE = 3.0f;

	unsigned systemsSeen = 0, systemsCast = 0, particlesSubmitted = 0;

	ParticleSystemManager::ParticleSystemList &particleSysList = TheParticleSystemManager->getAllParticleSystems();
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys)
			continue;

		++systemsSeen;

		// Drawable-based systems already cast through the mesh renderer; casting them
		// again from here would double every shadow they have.
		if (sys->isUsingDrawables())
			continue;

		if (!sys->castsShadows())
			continue;

		Int count = 0;
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();

		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
		{
			const Real psize = p->getSize();
			if (psize < MIN_SHADOW_PARTICLE_SIZE)
				continue;

			const Coord3D *pos = p->getPosition();
			// Half the quad's diagonal, so a sprite clipped by the box edge still gets in.
			const Vector3 centre(pos->x, pos->y, pos->z);
			if (DX8Wrapper::Cull_Sphere_By_Sun(centre, psize * 0.71f))
				continue;

			posArray[count] = centre;
			sizeArray[count] = psize;

			// Only the alpha is read by the sprite depth shader, but the whole colour goes
			// across: the vertex format carries it either way and splitting it out would
			// mean a second packing path for no saving.
			const RGBColor *color = p->getColor();
			RGBAArray[count].X = color->red;
			RGBAArray[count].Y = color->green;
			RGBAArray[count].Z = color->blue;
			RGBAArray[count].W = p->getAlpha();

			angleArray[count] = (uint8)(p->getAngle() * 255.0f / (2.0f * PI));

#ifdef RTS_DEBUG
			DX8Wrapper::Debug_Note_Particle_Shadow_Sprite(psize, RGBAArray[count].W);
#endif

			if (++count == MAX_POINTS_PER_GROUP)
				break;
		}

		if (count == 0)
			continue;

		TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );

		m_pointGroup->Set_Texture( texture );
		texture->Release_Ref();	//release reference since it's held by pointGroup

		// The shader is set for the render state it leaves on the device, not for any
		// blending: the depth pass forces blending off and binds its own shaders. But the
		// routing reads those states to decide what kind of draw this is, and a point
		// group carrying the last system's shader would be classified as the last system.
		switch( sys->getShaderType() )
		{
			case ParticleSystemInfo::ALPHA:
				m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
				break;
			case ParticleSystemInfo::ALPHA_TEST:
				m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
				break;
			default:
				// castsShadows() admits no other blend mode; if that ever changes, the
				// sprite cannot be drawn as something it is not.
				continue;
		}

		m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
		m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, nullptr, m_sizeBuffer, m_angleBuffer, nullptr, count );
		m_pointGroup->Set_Point_Frame( 0 );

		// One layer even for a volume particle system. The extra layers exist to give the
		// sprite visible depth from the camera; through the sun they would be the same
		// silhouette rasterised several times over, at the same cost and to no effect.
		m_pointGroup->Render_Sun_Depth();

		++systemsCast;
		particlesSubmitted += (unsigned)count;
	}

#ifdef RTS_DEBUG
	DX8Wrapper::Debug_Note_Particle_Shadow_Submit(systemsSeen, systemsCast, particlesSubmitted);
	m_lastShadowParticleCount = (Int)particlesSubmitted;
#endif
}

void W3DParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{

	if (m_readyToRender == false)
		return;

	// external mechanism must tell us when it's OK to render again...
	m_readyToRender = false;

	//reset each frame
	/// @todo lorenzen sez: this should be debug only:
	m_onScreenParticleCount = 0;

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//@todo lorenzen sez: put these in registers for sure
	Real bcX = bbox.Center.X;
	Real bcY = bbox.Center.Y;
	Real bcZ = bbox.Center.Z;
	Real beX = bbox.Extent.X;
	Real beY = bbox.Extent.Y;
	Real beZ = bbox.Extent.Z;

	unsigned int personalities[MAX_POINTS_PER_GROUP];


	m_fieldParticleCount = 0;

	const Bool drawSmudge = TheSmudgeManager && TheSmudgeManager->getHardwareSupport() && TheGlobalData->m_useHeatEffects;

	if (drawSmudge)
	{
		TheSmudgeManager->resetDraw();
	}

	ParticleSystemManager::ParticleSystemList &particleSysList = TheParticleSystemManager->getAllParticleSystems();
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys) {
			continue;
		}

		// only look at particle/point style systems
		if (sys->isUsingDrawables())
			continue;

		//temporary hack that checks if texture name starts with "SMUD" - if so, we can assume it's a smudge type
		if (/*sys->isUsingSmudge()*/ *((DWORD *)sys->getParticleTypeName().str()) == 0x44554D53)
		{
			if (drawSmudge)
			{
				for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
				{
					const Coord3D *pos = p->getPosition();
					Real psize = p->getSize();

					//Cull particle to edges of screen and terrain.
					if (WWMath::Fabs( pos->x - bcX ) > ( beX + psize ) )
						continue;

					if (WWMath::Fabs( pos->y - bcY ) > ( beY + psize ) )
						continue;

					if (WWMath::Fabs( pos->z - bcZ ) > ( beZ + psize ) )
						continue;

					if (Smudge *smudge = TheSmudgeManager->findSmudge(p))
					{
						// The particle is in view. Draw the smudge!
						smudge->m_draw = true;
					}
				}
			}
			continue;
		}

		/// @todo lorenzen sez: declare these outside the sys loop, and put some in registers
		// initialize them here still, of course
		// build W3D particle buffer
		Int count = 0;
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();
		const Coord3D *pos;
		const RGBColor *color;
		Real psize;



		//set-up all the per-particle
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
		{
			pos = p->getPosition();
			psize = p->getSize();

			//Cull particle to edges of screen and terrain.
			if (WWMath::Fabs(pos->x - bcX) > (beX + psize))
				continue;

			if (WWMath::Fabs(pos->y - bcY) > (beY + psize))
				continue;

			if (WWMath::Fabs(pos->z - bcZ) > (beZ + psize))
				continue;

			m_fieldParticleCount += ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE );

			//@todo lorenzen sez: use pointer arithmetic for these arrays
			personalities[count] = p->getPersonality();

			posArray[count].X = pos->x;
			posArray[count].Y = pos->y;
			posArray[count].Z = pos->z;

			sizeArray[count] = psize;

			color = p->getColor();
			RGBAArray[count].X = color->red;
			RGBAArray[count].Y = color->green;
			RGBAArray[count].Z = color->blue;
			RGBAArray[count].W = p->getAlpha();

			angleArray[count] = (uint8)(p->getAngle() * 255.0f / (2.0f * PI));

			if (++count == MAX_POINTS_PER_GROUP)
				break;
		}

		if ( count == 0 )
			continue;	//this system has no particles to render

		TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );

		if ( m_streakLine && sys->isUsingStreak() && (count >= 2) )
		{
			m_streakLine->Reset_Line();

			m_streakLine->Set_Texture( texture );
			texture->Release_Ref();//release reference since it's held by streakline
			switch( sys->getShaderType() )
			{
				case ParticleSystemInfo::ADDITIVE:
					m_streakLine->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA:
					m_streakLine->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA_TEST:
					m_streakLine->Set_Shader( ShaderClass::_PresetATestSpriteShader );
					break;
				case ParticleSystemInfo::MULTIPLY:
					m_streakLine->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
					break;
			}

			//UPDATE THE STREAK'S ARRAYS
			m_streakLine->Set_LocsWidthsColors(
				count,
				m_posBuffer->Get_Array(),
				m_sizeBuffer->Get_Array(),
				m_RGBABuffer->Get_Array(),
				&personalities[0]
				);

			//WWASSERT( m_streakLine->Get_Num_Points() == count );

			// This is the happy place for this!
			RGBAArray[0].X = 0;//eliminates the scissor edge on the trailing edge of the streak
			RGBAArray[0].Y = 0;
			RGBAArray[0].Z = 0;
			RGBAArray[0].W = 0;


			//RENDER STREAK!
			m_streakLine->Render( rinfo );

		}
		else
		{

			WWASSERT( m_pointGroup );

			if ( m_pointGroup ) // this catches the particle and volumeparticle cases
			{
				// render all the systems' particles
				m_pointGroup->Set_Texture( texture );
				texture->Release_Ref();//release reference since it's held by pointGroup
				m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space

				switch( sys->getShaderType() )
				{
					case ParticleSystemInfo::ADDITIVE:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA_TEST:
						m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
						break;
					case ParticleSystemInfo::MULTIPLY:
						m_pointGroup->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
						break;
				}

				/// @todo Use both QUADS and TRIS for particles
				m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
				m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, nullptr, m_sizeBuffer, m_angleBuffer, nullptr, count );
				m_pointGroup->Set_Billboard(sys->shouldBillboard());

				/// @todo Support animated texture particles
				/// @todo lorenzen sez: unimplemented code wastes cpu cycles
				m_pointGroup->Set_Point_Frame( 0 );

				//RENDER IT!
				if( sys->getVolumeParticleDepth() > 1 )
				{
					m_pointGroup->RenderVolumeParticle( rinfo, sys->getVolumeParticleDepth() );
				}
				else
					m_pointGroup->Render( rinfo );

			}
		}


		/// @todo lorenzen sez: this should be debug only:
		//add particle count to total
		m_onScreenParticleCount += count;

	/*
		// draw the wind vector for this particle system on the screen
		UnsignedInt width = TheDisplay->getWidth();
		UnsignedInt height = TheDisplay->getHeight();
		Coord3D worldStart, worldEnd;
		ICoord2D pixelStart, pixelEnd;
		sys->getPosition( &worldStart );
		worldEnd.x = Cos( sys->getWindAngle() ) * 50.0f + worldStart.x;
		worldEnd.y = Sin( sys->getWindAngle() ) * 50.0f + worldStart.y;
		worldEnd.z = worldStart.z;
		TheTacticalView->worldToScreen( &worldStart, &pixelStart );
		TheTacticalView->worldToScreen( &worldEnd, &pixelEnd );
		Color colorStart = GameMakeColor( 255, 255, 255, 255 );
		Color colorEnd = GameMakeColor( 255, 128, 128, 255 );
		TheDisplay->drawLine( pixelStart.x, pixelStart.y, pixelEnd.x, pixelEnd.y, 1.0f, colorStart, colorEnd );
	*/


	}

		/// @todo lorenzen sez: this should be debug only:
	TheParticleSystemManager->setOnScreenParticleCount(m_onScreenParticleCount);

	//Draw any particles belonging to weather effects
	if (TheSnowManager)
		((W3DSnowManager *)TheSnowManager)->render(rinfo);

	//Now process screen smudges which are particles that distort the background behind them.
	if(TheSmudgeManager)
	{
		((W3DSmudgeManager *)TheSmudgeManager)->render(rinfo);
	}
}
