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

// FILE: W3DSnow.h /////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "GameClient/View.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/assetmgr.h"



#define SNOW_BATCH_SIZE	2048	//we render at most this many particles per drawprimitive call.  This number * 6 must be less than 65536 to fit into index buffer.

W3DSnowManager::W3DSnowManager()
{
	m_indexBuffer=nullptr;
	m_snowTexture=nullptr;
}

W3DSnowManager::~W3DSnowManager()
{
	ReleaseResources();
}

void W3DSnowManager::init()
{
	SnowManager::init();
	ReAcquireResources();
}

/** Releases all W3D/D3D assets before a reset.. */
void W3DSnowManager::ReleaseResources()
{
	REF_PTR_RELEASE(m_snowTexture);

	REF_PTR_RELEASE(m_indexBuffer);
}

/** (Re)allocates all W3D/D3D assets after a reset.. */
Bool W3DSnowManager::ReAcquireResources()
{
	ReleaseResources();

	if (!TheWeatherSetting->m_snowEnabled)
		return TRUE;	//no need for resources if snow is disabled.

	{
		m_indexBuffer=NEW_REF(DX8IndexBufferClass,(SNOW_BATCH_SIZE *6));	//allocate 2 triangles per flake, each with 3 indices.

		// Fill up the IB with static vertex indices that will be used for all smudges.
		{
			DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
			UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
			//quad of 4 triangles:
			//	0-----3
			//  |\   /|
			//  |  X  |
			//	|/   \|
			//  1-----2
			Int vbCount=0;
			for (Int i=0; i<SNOW_BATCH_SIZE; i++)
			{
				//Top
				ib[0]=vbCount+3;
				ib[1]=vbCount;
				ib[2]=vbCount+2;
				//Bottom
				ib[3]=vbCount+2;
				ib[4]=vbCount;
				ib[5]=vbCount+1;

				vbCount += 4;
				ib+=6;
			}
		}
	}

	m_snowTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWeatherSetting->m_snowTexture.str());

	return TRUE;
}

void W3DSnowManager::updateIniSettings()
{
	//Call base class
	SnowManager::updateIniSettings();

	if (m_snowTexture && stricmp(m_snowTexture->Get_Texture_Name(),TheWeatherSetting->m_snowTexture.str()) != 0)
	{
		REF_PTR_RELEASE(m_snowTexture);
		m_snowTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWeatherSetting->m_snowTexture.str());
	}
}

void W3DSnowManager::reset()
{
	SnowManager::reset();
}

void W3DSnowManager::update()
{
	// TheSuperHackers @tweak The snow render update is now decoupled from the logic step.
	m_time += WW3D::Get_Logic_Frame_Time_Seconds();

	//find current time offset, adjusting for overflow
	m_time=fmod(m_time,m_fullTimePeriod);
}

#define MAXIMUM_CAMERA_DISTANCE 100000	//maximum distance of camera position from world origin.
#define ISPOW2(x)  (x && (x & (x-1)) == 0)	//is a number a power of 2?
#define MODPOW2(x,y) ((x) & (y-1))		//mod '%' operator for powers of 2.

// Helper function to stuff a FLOAT into a DWORD argument
inline DWORD FtoDW( FLOAT f ) { return *((DWORD*)&f); }

// renderSubBox was here, and with it the point-sprite snow drawer.
//
// It built one vertex per flake and let D3DRS_POINTSPRITEENABLE expand each one to a quad
// in the rasteriser, subdividing the visible cube against the frustum so that each batch
// was a DrawPrimitive of a few hundred points. It was the largest fixed-function draw
// family left in the game -- 4800 draws per 600-frame window on usa_lightsout with no
// override at all, 12320 with snow forced on, 100% fixed function on both the vertex and
// the pixel side -- and it stayed invisible for four phases because civ_buildings, the
// replay everything was verified against, has no snow.
//
// There is no programmable equivalent: a vertex shader is handed one vertex and must emit
// one, so the expansion cannot happen in it, and D3D11 closes that hole with a geometry
// shader rather than with a render state. Every backend after D3D9 therefore reported
// PointSprites false and took renderAsQuads, which is the same snowfall built out of real
// quads on the CPU and drawn through DX8Wrapper::Draw_Triangles like everything else.
//
// Deleting the drawer rather than the capability check, because Phase 9 looked at it and
// found a bug: under D3D9 the point-sprite path painted the flakes **black**, and the quad
// path painted them white and correct on both backends. Nobody saw it because the only
// shipped map with snow is a night map. The quad path is measured backend-consistent --
// 145262 pixels of snow footprint, and quads-against-quads across the two backends added
// about 23k to a 718k baseline -- and renderAsQuads derives its world size from the point
// size and the projection, so the retune the two drawers needed is already in it.

void W3DSnowManager::render(RenderInfoClass &rinfo)
{
	// Snow particles.
	DeclaredTechniqueClass declareEffect(MESH_TECHNIQUE_EFFECT, "snow");
	if (!TheWeatherSetting->m_snowEnabled || !m_isVisible)
		return;

	//make sure the noise table is powers of 2 in dimensions.
	WWASSERT(ISPOW2(SNOW_NOISE_X) && ISPOW2(SNOW_NOISE_Y));

	//CameraClass &camera=rinfo.Camera;

	const Coord3D &cPos=TheTacticalView->get3DCameraPosition();
	Vector3 camPos(cPos.x,cPos.y,cPos.z);

	//Number of emitters from cube center to edge of visible extent.
	Int mumEmittersInHalf=(Int)floor(m_boxDimensions / m_emitterSpacing * 0.5f);

	//Find origin of visible cube surrounding camera.
	Int cubeCenterX=(Int)floor(camPos.X/m_emitterSpacing);
	Int cubeCenterY=(Int)floor(camPos.Y/m_emitterSpacing);

	//Find extents of visible cube surrounding camera.
	Int cubeOriginX=cubeCenterX - mumEmittersInHalf;	//top/left extents.
	Int cubeOriginY=cubeCenterY - mumEmittersInHalf;
	Int cubeDimX=cubeCenterX + mumEmittersInHalf;		//bottom/right extents.
	Int cubeDimY=cubeCenterY + mumEmittersInHalf;

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//Particles move outside the visible box as a result of local sine movement
	//so adjust bounding box to include them.
	bbox.Extent.X += m_amplitude+m_quadSize;
	bbox.Extent.Y += m_amplitude+m_quadSize;

	//Clip our visible snow rendering box
	if ((cubeOriginX * m_emitterSpacing ) < (bbox.Center.X - bbox.Extent.X))
		cubeOriginX = (Int)floor ((bbox.Center.X - bbox.Extent.X)/m_emitterSpacing);

	if ((cubeOriginY * m_emitterSpacing ) < (bbox.Center.Y - bbox.Extent.Y))
		cubeOriginY = (Int)floor ((bbox.Center.Y - bbox.Extent.Y)/m_emitterSpacing);

	if ((cubeDimX * m_emitterSpacing ) > (bbox.Center.X + bbox.Extent.X))
		cubeDimX = (Int)floor ((bbox.Center.X + bbox.Extent.X)/m_emitterSpacing);

	if ((cubeDimY * m_emitterSpacing ) > (bbox.Center.Y + bbox.Extent.Y))
		cubeDimY = (Int)floor ((bbox.Center.Y + bbox.Extent.Y)/m_emitterSpacing);

	if ((cubeDimY - cubeOriginY) < 0 || (cubeDimX-cubeOriginX) < 0)
		return;	//entire snow box is culled by either x or y screen boundary.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-cubeOriginY)*(cubeDimX-cubeOriginX);

	if (totalPart <= 0)
		return;	//nothing to render.

	//Height at the top of the cube with camera at center.
	m_snowCeiling = camPos.Z + m_boxDimensions/2.0f;

	//Offset to allow cube extents to move with camera.
	Real cameraOffset = fmod (camPos.Z,m_boxDimensions);
	m_heightTraveled=m_time*m_velocity+cameraOffset;	//height that snow flake traveled this frame.

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,identity);

	DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	//make sure we have all the resources we need
	if (!m_indexBuffer)
		ReAcquireResources();

	DX8Wrapper::Set_Texture(0,m_snowTexture);

	renderAsQuads(rinfo,cubeOriginX,cubeOriginY,cubeDimX,cubeDimY);
}

/** The snow drawer. Was the fallback for hardware without point sprites; since
 * Phase 10 deleted the point-sprite drawer it is the only one. */
void W3DSnowManager::renderAsQuads(RenderInfoClass &rinfo, Int cubeOriginX, Int cubeOriginY, Int cubeDimX, Int cubeDimY)
{

	Matrix4x4 proj;
	Matrix3D view;
	Vector3 snowCenter;
	Vector3 snowCenterVS;

	CameraClass &camera=rinfo.Camera;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

	Vector2 quad_uvs[4] = {
		Vector2(0.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(1.0f, 0.0f)
	};


	// The size, derived from the point-sprite path rather than set beside it.
	//
	// These two drawers are meant to be the same snowfall and they were sized by two
	// unrelated settings that nothing kept in step: m_snowPointSize is a screen-space
	// point scaled by the device, m_snowQuadSize is half a world unit, and the same frame
	// came out 15% of pixels different between them. A backend without point sprites now
	// takes this path always, so "close enough" is no longer close enough.
	//
	// D3D9 scales a point sprite by Ss = Vh * Si / De with POINTSCALE_A and _B zero and
	// _C one, which is what this drawer sets. A world-space quad of side S facing the
	// camera at eye depth De covers (Vh/2) * S * P11 / De pixels, where P11 is the
	// projection's y scale. Equate the two and De cancels:
	//
	//     S = 2 * Si / P11
	//
	// so one world size reproduces the point sprite at every depth, and m_snowQuadSize
	// stays only as the fallback for a projection that cannot be read.
	Real quadSize = m_quadSize;
	const Real projY = proj[1][1];
	if (projY > 0.0f) quadSize = 2.0f * m_pointSize / projY;

	// D3DRS_POINTSIZE_MAX caps the sprite in *pixels*, so past a certain nearness the
	// point stops growing and the quad would not. Below this eye depth the equivalent
	// world size is the one that covers exactly m_maxPointSize pixels.
	Real clampDepth = 0.0f;
	if (projY > 0.0f && m_maxPointSize > 0.0f) {
		int rtWidth = 0, rtHeight = 0, rtBits = 0;
		bool rtWindowed = false;
		WW3D::Get_Render_Target_Resolution(rtWidth, rtHeight, rtBits, rtWindowed);
		if (rtHeight > 0) clampDepth = (Real)rtHeight * m_pointSize / m_maxPointSize;
	}

#ifdef RTS_DEBUG
	// Once, so a run says what it derived rather than leaving it to be re-derived. The
	// point-sprite path is not available on every backend, so this is often the only
	// number of the two that a given run has.
	static Bool reportedQuadSize = FALSE;
	if (!reportedQuadSize) {
		reportedQuadSize = TRUE;
		WWDEBUG_SAY(("SNOW: quad path, point size %.3f and projection y scale %.3f give a "
					 "world quad of %.3f (the INI's SnowQuadSize is %.3f and is now only a "
					 "fallback); the %.0f pixel point size cap bites closer than %.2f units.",
			m_pointSize, projY, quadSize, m_quadSize, m_maxPointSize, clampDepth));
	}
#endif

	//pre-multiple the offsets by particle size
	for (Int i=0; i<4; i++)
	{
		vertex_offsets[i] *= quadSize;
	}

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_VIEW,identity);

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	Int y=cubeOriginY;	//loop counter.
	Int cubeOriginXRemainder = cubeOriginX;	//loop counter - adjusted when not all particles fit into render buffer.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-cubeOriginY)*(cubeDimX-cubeOriginX);

	m_totalRendered += totalPart;

	while (totalPart)
	{
		Int batchSize=totalPart;

		if (batchSize > SNOW_BATCH_SIZE)
			batchSize = SNOW_BATCH_SIZE;

		Int numberInBatch=0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,batchSize*4);	//allocate 4 verts per flake
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();

			for (;y<cubeDimY; y++)
			{
				for (Int x=cubeOriginXRemainder; x<cubeDimX; x++)
				{
					if (numberInBatch >= batchSize)
					{	cubeOriginXRemainder = x;
						goto flush_particles;
					}

					//Get initial height from noise table.  We add a large value to make sure it's positive.  Then
					//modulate by table dimensions to find a value.
					Int noiseOffset=MODPOW2(x+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_X)+MODPOW2(y+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_Y)*SNOW_NOISE_X;
					if (noiseOffset > (SNOW_NOISE_X * SNOW_NOISE_Y))
						noiseOffset = 0;	//this should never happen but check to prevent buffer over/under flow.

					//find current height
					Real h0=m_snowCeiling-fmod(m_heightTraveled+m_startingHeights[noiseOffset],m_boxDimensions);

					//find world-space position of snow flake
					snowCenter.Set(x*m_emitterSpacing,y*m_emitterSpacing,h0);

					// Adjust position so snow flakes don't fall straight down -- in world
					// space, before the view transform, which is where the point-sprite
					// path does it. Applied to the view-space position instead, as this
					// did, the same sway ran along screen right and screen up rather than
					// along the ground plane: a five world unit offset in the wrong two
					// axes, which moves every flake rather than resizing it, and is the
					// larger half of the difference between the two drawers.
					snowCenter.X += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleX + (Real)x);
					snowCenter.Y += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleY + (Real)y);

					//Get view-space position
					Matrix3D::Transform_Vector(view,snowCenter,&snowCenterVS);

					// Near the camera the point sprite has stopped growing, so the quad
					// must too. Everywhere else this scale is 1 and costs a compare.
					Real flakeScale = 1.0f;
					const Real eyeDepth = -snowCenterVS.Z;
					if (clampDepth > 0.0f && eyeDepth > 0.0f && eyeDepth < clampDepth) {
						flakeScale = eyeDepth / clampDepth;
					}

					for (Int i=0; i<4; i++)
					{
						*(Vector3 *)verts=snowCenterVS + vertex_offsets[i] * flakeScale;
						verts->nx=0;	//keep AGP write-combining active
						verts->ny=0;
						verts->nz=0;
						verts->diffuse=0xffffffff;	//set to opaque
						verts->u1=quad_uvs[i].X;
						verts->v1=quad_uvs[i].Y;
						verts->u2=0;	//keep AGP write-combining active
						verts->v2=0;
						verts++;
					}

					numberInBatch++;
				}
				//getting here means we did not overflow the render buffer, so reset x origin to normal.
				cubeOriginXRemainder = cubeOriginX;	//reset to normal amount
			}
flush_particles:
			numberInBatch;	//need something at goto destination - stupid c compiler.
		}

		//Render any particles that may be queued up.
		if (numberInBatch)
		{
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
			DX8Wrapper::Draw_Triangles(	0,numberInBatch*2, 0, numberInBatch*4);
			totalPart -= numberInBatch;
		}
	}
}
