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

// W3DSmudge.cpp ////////////////////////////////////////////////////////////////////////////////
// Smudge System implementation
// Author: Mark Wilczynski, June 2003
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "WWLib/always.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Common/GameMemory.h"
#include "GameClient/View.h"
#include "GameClient/Display.h"
#include "WW3D2/texture.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/sortingrenderer.h"


SmudgeManager *TheSmudgeManager=nullptr;

W3DSmudgeManager::W3DSmudgeManager()
	: m_smudgeGroup(nullptr),
	  m_posBuffer(nullptr),
	  m_RGBABuffer(nullptr),
	  m_sizeBuffer(nullptr),
	  m_backgroundTexture(nullptr),
	  m_backgroundFormat((UnsignedInt)D3DFMT_UNKNOWN),
	  m_indexBuffer(nullptr),
	  m_backBufferWidth(0),
	  m_backBufferHeight(0)
{
	// These were left uninitialised. init() calls ReAcquireResources, which opens with
	// ReleaseResources and releases both pointers before anything has ever assigned them.
}

W3DSmudgeManager::~W3DSmudgeManager()
{
	ReleaseResources();
}

void W3DSmudgeManager::init()
{
	SmudgeManager::init();
	ReAcquireResources();
}

void W3DSmudgeManager::reset ()
{
	SmudgeManager::reset();	//base
}

void W3DSmudgeManager::ReleaseResources()
{
	REF_PTR_RELEASE(m_backgroundTexture);
	REF_PTR_RELEASE(m_indexBuffer);
}


#define SMUDGE_DRAW_SIZE	500	//draw at most 50 smudges per call. Tweak value to improve CPU/GPU parallelism.

static_assert(SMUDGE_DRAW_SIZE * 5 < 0x10000, "Vertex index exceeds 16-bit limit");


/*
** The scratch copy of the scene that the haze samples and distorts.
**
** It is built as a raw D3D render-target texture rather than through TextureClass's usual
** (width, height, WW3DFormat) constructor, and that is the whole reason heat haze can work
** under HDR. The scene's colour format is floating point then, and WW3DFormat has no
** floating-point member to name it -- so the old constructor could not ask for a matching
** texture, and the copy into it was a format conversion the runtime declines to make. The
** wrapping constructor takes a texture that already exists, so the format never has to be
** expressed in an enum that cannot express it.
**
** D3DUSAGE_RENDERTARGET is required by the copy, not by any drawing: StretchRect will only
** write into a render target. This mirrors the water's refraction grab exactly -- see
** W3DShaderManager::initRefraction, which had the same problem and the same answer.
**
** Everything downstream is unchanged. The draw still binds this through Set_Texture and is
** still routed like any other effect geometry, so the shader that samples it is the same
** ps_3_0 that would have sampled the 8-bit version.
*/
void W3DSmudgeManager::createBackgroundTexture()
{
	REF_PTR_RELEASE(m_backgroundTexture);
	m_backgroundFormat = (UnsignedInt)D3DFMT_UNKNOWN;

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	// Dimensions off the back buffer, format off the scene. The two are the same size --
	// the HDR target is built to match -- but only the scene knows whether it is floating
	// point, and it is the scene this has to be copy-compatible with.
	SurfaceClass *surface = DX8Wrapper::_Get_DX8_Back_Buffer();
	if (surface == nullptr)
		return;
	SurfaceClass::SurfaceDescription surface_desc;
	surface->Get_Description(surface_desc);
	REF_PTR_RELEASE(surface);

	const D3DFORMAT format = W3DShaderManager::getSceneColorFormat();

	IDirect3DTexture8 *tex = nullptr;
	if (FAILED(dev->CreateTexture(surface_desc.Width, surface_desc.Height, 1,
			D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &tex)) || tex == nullptr)
	{
		DEBUG_LOG(("SMUDGE: could not create the %dx%d background copy (format %d)\n",
			surface_desc.Width, surface_desc.Height, (Int)format));
		return;
	}

	m_backgroundTexture = MSGNEW("TextureClass") TextureClass(tex);
	tex->Release();	//TextureClass took its own reference

	m_backgroundFormat = (UnsignedInt)format;
	m_backBufferWidth = surface_desc.Width;
	m_backBufferHeight = surface_desc.Height;
}

/*
** Rebuild the copy if the scene has changed colour format underneath it.
**
** HDR can be initialised after this manager is -- and can switch itself off at runtime, on
** the fallback path in initHdr -- so the format this was built for is not something that can
** be decided once. A mismatch is not a cosmetic problem: StretchRect refuses to convert
** between a floating-point surface and an 8-bit one, so the copy would quietly stop
** happening and the haze would distort whatever frame was last in the texture.
*/
void W3DSmudgeManager::refreshBackgroundTexture()
{
	const D3DFORMAT sceneFormat = W3DShaderManager::getSceneColorFormat();
	if (m_backgroundTexture != nullptr && m_backgroundFormat == (UnsignedInt)sceneFormat)
		return;

	DEBUG_LOG(("SMUDGE: scene colour format is now %d, rebuilding the background copy (was %d)\n",
		(Int)sceneFormat, (Int)m_backgroundFormat));
	createBackgroundTexture();
	// The capability answer was reached against the old format; ask again.
	m_hardwareSupportStatus = SMUDGE_SUPPORT_UNKNOWN;
}

/*
** Copy the scene as it stands into the background texture.
**
** StretchRect, not SurfaceClass::Copy. Copy goes through the D3D8-era CopyRects path, which
** cannot read a multisampled surface and cannot touch a floating-point one; StretchRect
** resolves multisampling on the way through and handles both formats, which is what the
** water's grab already relies on.
*/
Bool W3DSmudgeManager::captureBackground(SurfaceClass *sceneSurface)
{
	if (sceneSurface == nullptr || m_backgroundTexture == nullptr)
		return FALSE;

	IDirect3DSurface8 *src = sceneSurface->Peek_D3D_Surface();
	if (src == nullptr)
		return FALSE;

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return FALSE;

	IDirect3DTexture8 *tex = m_backgroundTexture->Peek_D3D_Texture();
	if (tex == nullptr)
		return FALSE;

	IDirect3DSurface8 *dst = nullptr;
	if (FAILED(tex->GetSurfaceLevel(0, &dst)) || dst == nullptr)
		return FALSE;

	const HRESULT hr = DX8Wrapper::Copy_DX8_Surface(src, dst) ? S_OK : E_FAIL;
	dst->Release();

	if (FAILED(hr))
	{
		static Bool s_reported = FALSE;
		if (!s_reported)
		{
			DEBUG_LOG(("SMUDGE: background copy failed (0x%08X) -- the haze has nothing to distort\n",
				(unsigned)hr));
			s_reported = TRUE;
		}
		return FALSE;
	}
	return TRUE;
}

void W3DSmudgeManager::ReAcquireResources()
{
	ReleaseResources();

	createBackgroundTexture();

	m_indexBuffer=NEW_REF(DX8IndexBufferClass,(SMUDGE_DRAW_SIZE*4*3));	//allocate 4 triangles per smudge, each with 3 indices.

	// Fill up the IB with static vertex indices that will be used for all smudges.
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		//quad of 4 triangles:
		//	0-----3
		//  |\   /|
		//  |  4  |
		//	|/   \|
		//  1-----2
		Int vbCount=0;
		for (Int i=0; i<SMUDGE_DRAW_SIZE; i++)
		{
			//Top
			ib[0]=vbCount;
			ib[1]=vbCount+4;
			ib[2]=vbCount+3;
			//Right
			ib[3]=vbCount+3;
			ib[4]=vbCount+4;
			ib[5]=vbCount+2;
			//Bottom
			ib[6]=vbCount+2;
			ib[7]=vbCount+4;
			ib[8]=vbCount+1;
			//Left
			ib[9]=vbCount+1;
			ib[10]=vbCount+4;
			ib[11]=vbCount+0;

			vbCount += 5;
			ib+=12;
		}
	}
}

/*Copies a portion of the current render target into a specified buffer*/
Int copyRect(unsigned char *buf, Int bufSize, int oX, int oY, int width, int height)
{
 	IDirect3DSurface8 *surface=nullptr;	///<previous render target
 	IDirect3DSurface8 *tempSurface=nullptr;
	Int result = 0;
	HRESULT hr = S_OK;

 	LPDIRECT3DDEVICE8 m_pDev=DX8Wrapper::_Get_D3D_Device8();

	if (!m_pDev)
		goto error;

 	surface = DX8Wrapper::Get_DX8_Render_Target_Surface(0);

	if (!surface)
		goto error;

 	D3DSURFACE_DESC desc;

 	surface->GetDesc(&desc);

	RECT srcRect;
	srcRect.left=oX;
	srcRect.top=oY;
	srcRect.right=oX+width;
	srcRect.bottom=oY+height;

	POINT dstPoint;
	dstPoint.x=0;
	dstPoint.y=0;

 	hr=DX8Wrapper::D3D9_CreateImageSurface_Helper(m_pDev, width, height, desc.Format, &tempSurface);

	if (hr != S_OK)
		goto error;

 	hr=DX8Wrapper::_Copy_DX8_Rects(surface,&srcRect,1,tempSurface,&dstPoint);

	if (hr != S_OK)
		goto error;

 	D3DLOCKED_RECT lrect;

 	hr=tempSurface->LockRect(&lrect,nullptr,D3DLOCK_READONLY);

	if (hr != S_OK)
		goto error;

	{
		unsigned int surfaceSize = DX8Wrapper::Get_Surface_Size(desc);

		if (surfaceSize < bufSize)
			bufSize = surfaceSize;
	}

	memcpy(buf,lrect.pBits,bufSize);
	result = bufSize;

	tempSurface->UnlockRect();

error:
	if (surface)
		surface->Release();
	if (tempSurface)
		tempSurface->Release();

	return result;
}

#define UNIQUE_COLOR	(0x12345678)
#define BLOCK_SIZE	(8)

/*
** Can the heat-haze effect run on this device?
**
** What the effect needs is one thing: the scene drawn so far, readable as a texture while
** we carry on drawing into that same scene. render() gets it by copying the colour target
** into m_backgroundTexture and sampling the copy, so that copy-then-sample is what this
** tests, against a colour it just drew and can recognise.
**
** The test this replaces asked a different question, and one with no right answer. It drew
** a known colour, bound W3DShaderManager's render texture -- the surface the scene was
** being drawn into at that moment -- as the source for a second draw, and compared the two
** readbacks. That is sampling a texture while it is the active render target, which D3D9
** does not permit: the runtime drops the binding, the second draw samples black, and the
** comparison cannot match. So the first frame that reached this recorded SMUDGE_SUPPORT_NO,
** which is cached for the run and also gates whether the particle system bothers to collect
** smudges at all -- no heat haze anywhere, microwave tank included. The render path never
** did this; it has always sampled a copy. Only the test did.
*/
Bool W3DSmudgeManager::testHardwareSupport()
{
	if (m_hardwareSupportStatus == SMUDGE_SUPPORT_UNKNOWN)
	{	//we have not done the test yet.

		if (!m_backgroundTexture)
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		// With a floating-point scene the readback comparison below cannot run: it draws a
		// known 32-bit colour and compares the bytes, and those bytes mean nothing once the
		// target holds half-floats. What the comparison is really asking -- can this device
		// copy the colour target and sample the copy -- is answered for the HDR case by
		// initHdr, which has already checked the format for RENDERTARGET, FILTER and
		// post-pixel-shader blending. So test the one part that is genuinely new here, which
		// is whether the copy itself goes through, and say which test ran.
		if (W3DShaderManager::isHdrActive())
		{
			SurfaceClass *sceneSurface=DX8Wrapper::_Get_DX8_Render_Target();
			const Bool copied = sceneSurface && captureBackground(sceneSurface);
			REF_PTR_RELEASE(sceneSurface);

			if (copied)
			{
				DEBUG_LOG(("SMUDGE: supported -- floating-point scene, background copy succeeds\n"));
				m_hardwareSupportStatus = SMUDGE_SUPPORT_YES;
				return TRUE;
			}
			DEBUG_LOG(("SMUDGE: unsupported -- floating-point scene and the background copy failed\n"));
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

		ShaderClass shader=ShaderClass::_PresetOpaqueShader;
		shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
		shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
		DX8Wrapper::Set_Shader(shader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		struct _TRANS_LIT_TEX_VERTEX {
			Vector4 p;
			DWORD color;   // diffuse color
			float	u;
			float	v;
		} v[4];

		//bottom right
		v[0].p = Vector4( BLOCK_SIZE-0.5f, BLOCK_SIZE-0.5f, 0.0f, 1.0f );
		v[0].u = BLOCK_SIZE/(Real)TheDisplay->getWidth();
		v[0].v = BLOCK_SIZE/(Real)TheDisplay->getHeight();
		//top right
		v[1].p = Vector4( BLOCK_SIZE-0.5f, 0-0.5f, 0.0f, 1.0f );
		v[1].u = BLOCK_SIZE/(Real)TheDisplay->getWidth();
		v[1].v = 0;
		//bottom left
		v[2].p = Vector4(  0-0.5f, BLOCK_SIZE-0.5f, 0.0f, 1.0f );
		v[2].u = 0;
		v[2].v = BLOCK_SIZE/(Real)TheDisplay->getHeight();
		//top left
		v[3].p = Vector4(  0-0.5f,  0-0.5f, 0.0f, 1.0f );
		v[3].u = 0;
		v[3].v = 0;

		v[0].color = UNIQUE_COLOR;
		v[1].color = UNIQUE_COLOR;
		v[2].color = UNIQUE_COLOR;
		v[3].color = UNIQUE_COLOR;

		// Both draws in this function stay on fixed function on purpose. This is not the
		// render path -- it is the hardware probe that decides whether the smudge feature
		// works at all, and it works by drawing a known quad, reading the pixels back and
		// comparing them against a reference it computed itself. What it is testing is
		// that the device can render to a texture and that the texture can be read back;
		// binding a shader over it would make the answer depend on the shader, which is
		// the one thing the test must not measure. Two draws, once, at startup.
		//
		//draw polygons like this is very inefficient but for only 2 triangles, it's
		//not worth bothering with index/vertex buffers.
		DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

		// Drawn on the device, so it inherits whatever the wrapper last bound. See
		// Force_Fixed_Function_Pipeline.
		DX8Wrapper::Force_Fixed_Function_Pipeline();
		DX8Wrapper::Prepare_Direct_Draw("smudge");
		DX8Wrapper::Draw_DX8_Primitive_UP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

		DWORD refData[BLOCK_SIZE*BLOCK_SIZE];
		memset(refData,0,sizeof(refData));
		Int bufSize=copyRect((unsigned char *)refData,sizeof(refData),0,0,BLOCK_SIZE,BLOCK_SIZE);	//copy area we just rendered using solid color
		if (!bufSize)
		{
			DEBUG_LOG(("SMUDGE: unsupported -- cannot read back off the render target\n"));
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		// Take the copy render() takes, off whatever surface the scene is going to.
		SurfaceClass *sceneSurface=DX8Wrapper::_Get_DX8_Render_Target();
		const Bool copied = sceneSurface && captureBackground(sceneSurface);
		REF_PTR_RELEASE(sceneSurface);

		if (!copied)
		{
			DEBUG_LOG(("SMUDGE: unsupported -- the background copy failed\n"));
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		// ...and sample it, which is legal precisely because it is a copy and not the
		// surface being drawn into.
		DX8Wrapper::Set_DX8_Texture(0,m_backgroundTexture->Peek_D3D_Texture());
		// Point sampling and clamp: the comparison below is exact, so nothing may filter
		// the neighbouring texels of the 8x8 block into it.
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_MINFILTER,D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_MIPFILTER,D3DTEXF_NONE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP);

		DWORD testData[BLOCK_SIZE*BLOCK_SIZE];
		memset(testData,0xff,sizeof(testData));

		v[0].color = 0xffffffff;
		v[1].color = 0xffffffff;
		v[2].color = 0xffffffff;
		v[3].color = 0xffffffff;

		// Drawn on the device, so it inherits whatever the wrapper last bound. See
		// Force_Fixed_Function_Pipeline.
		DX8Wrapper::Force_Fixed_Function_Pipeline();
		DX8Wrapper::Prepare_Direct_Draw("smudge");
		DX8Wrapper::Draw_DX8_Primitive_UP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
		bufSize=copyRect((unsigned char *)testData,sizeof(testData),0,0,BLOCK_SIZE,BLOCK_SIZE);

		DX8Wrapper::Set_DX8_Texture(0,nullptr);

		if (!bufSize)
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		// Compare colour only. Alpha is not ours to predict: render-to-texture masks alpha
		// writes off for the soft water edge, so the alpha byte here is whatever the clear
		// left behind rather than anything either draw wrote.
		Bool matches = TRUE;
		for (Int i=0; i<(Int)(bufSize/sizeof(DWORD)); i++)
		{
			if ((testData[i] & 0x00ffffff) != (refData[i] & 0x00ffffff))
			{
				matches = FALSE;
				break;
			}
		}

		// Logged either way, once per run: a silent no here switches heat haze off for the
		// whole session, and that is not something to have to infer from its absence.
		if (matches)
		{
			DEBUG_LOG(("SMUDGE: supported -- scene copy reads back intact\n"));
			m_hardwareSupportStatus = SMUDGE_SUPPORT_YES;
			return TRUE;
		}
		DEBUG_LOG(("SMUDGE: unsupported -- scene copy read back wrong (ref=%08X test=%08X)\n",
			refData[0], testData[0]));
		m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
	}

	return (SMUDGE_SUPPORT_YES == m_hardwareSupportStatus);
}

void W3DSmudgeManager::render(RenderInfoClass &rinfo)
{
	FF_SITE("W3DSmudgeManager::render");
	// Heat-haze smudges: a screen distortion, never a surface.
	DeclaredTechniqueClass declareEffect(MESH_TECHNIQUE_EFFECT, "smudge");

	// Before the capability answer is used, not after: HDR can come up or fall back after
	// this manager was initialised, and the copy has to match the scene's format to happen
	// at all. This also re-opens the capability question when the format moves.
	refreshBackgroundTexture();

	//Verify that the card supports the effect.
	if (!testHardwareSupport())
		return;

	// The surface the scene is being drawn into, which is not the back buffer whenever a
	// screen filter has redirected the frame -- with bloom on, the scene goes to the
	// filter's render texture and the back buffer still holds the *previous* frame,
	// composited and with the interface drawn over it. Sourcing the distortion from there
	// is what broke the microwave tank: the haze showed last frame plus the command bar,
	// smeared, and slid across the screen whenever the camera moved.
	//
	// testHardwareSupport has always read GetRenderTarget(0) for its verification copy, so
	// the test and the thing it was meant to be testing disagreed; this is the side that
	// was wrong.
	SurfaceClass *sceneSurface = DX8Wrapper::_Get_DX8_Render_Target();

	if (!sceneSurface)
		return;

	if (!m_backgroundTexture)
	{
		REF_PTR_RELEASE(sceneSurface);
		return;
	}

	SurfaceClass::SurfaceDescription surface_desc;
	sceneSurface->Get_Description(surface_desc);

	CameraClass &camera=rinfo.Camera;
	Vector3 vsVert;
	Vector4 ssVert;
	Real uvSpanX,uvSpanY;
	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

#define THE_COLOR (0x00ffeedd)

	UnsignedInt vertexDiffuse[5]={THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR};

	Matrix4x4 proj;
	Matrix3D view;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	Real texClampX = (Real)TheTacticalView->getWidth()/(Real)surface_desc.Width;
	Real texClampY = (Real)TheTacticalView->getHeight()/(Real)surface_desc.Height;

	Real texScaleX = texClampX*0.5f;
	Real texScaleY = texClampY*0.5f;

	//Do a first pass over the smudges to determine how many are visible
	//and to fill in their world-space positions and screen uv coordinates.
	//TODO: Optimize out this extra pass!
	//TODO: Find size of screen rectangle that actually needs copying.

	SmudgeSet *set=m_usedSmudgeSetList.Head();	//first set that didn't fit into render batch.
	Int count = 0;

	if (set)
	{
		//there are possibly some smudges to render, so make sure background particles have finished drawing.
		SortingRendererClass::Flush();	//draw sorted translucent polys like particles.
	}

	while (set)
	{
		Smudge *smudge=set->getUsedSmudgeList().Head();

		for (; smudge; smudge = smudge->Succ())
		{
			if (!smudge->m_draw)
				continue;

			//Get view-space center
			Matrix3D::Transform_Vector(view,smudge->m_pos,&vsVert);

			//Get 5 view-space vertices
			Smudge::smudgeVertex *verts=smudge->m_verts;

			//Do center vertex outside 'for' loop since it's different.
			verts[4].pos = vsVert;

			Vector2 offset = smudge->m_offset;

			for (Int i=0; i<4; i++)
			{
				verts[i].pos = vsVert + vertex_offsets[i] * smudge->m_size;
				//Ge uv coordinates for each vertex
				ssVert = proj * verts[i].pos;
				Real oow = 1.0f/ssVert.W;
				ssVert *= oow;	//returned in camera space which is -1,-1 (bottom-left) to 1,1 (top-right)
				//convert camera space to uv space: 0,0 (top-left), 1,1 (bottom-right)
				verts[i].uv.Set((ssVert.X+1.0f)*texScaleX,(1.0f-ssVert.Y)*texScaleY);

				Vector2 &thisUV=verts[i].uv;

				// Zero coordinates that fall outside valid texel bounds
				if (thisUV.X < 0 || thisUV.X > texClampX)
					offset.X = 0;

				if (thisUV.Y < 0 || thisUV.Y > texClampY)
					offset.Y = 0;
			}

			//Finish center vertex
			//Ge uv coordinates by interpolating corner uv coordinates and applying desired offset.
			uvSpanX=verts[3].uv.X - verts[0].uv.X;
			uvSpanY=verts[1].uv.Y - verts[0].uv.Y;
			verts[4].uv.X=verts[0].uv.X+uvSpanX*(0.5f+offset.X);
			verts[4].uv.Y=verts[0].uv.Y+uvSpanY*(0.5f+offset.Y);

			count++;	//increment visible smudge count.
		}

		set=set->Succ();	//advance to next node.
	}

	m_smudgeCountLastFrame = count;

	if (!count)
	{
		REF_PTR_RELEASE(sceneSurface);
		return;	//nothing to render.
	}

	//Copy the area of the scene occupied by smudges into an alternate buffer.
	const Bool copied = captureBackground(sceneSurface);
	REF_PTR_RELEASE(sceneSurface);

	if (!copied)
		return;	//nothing to distort; drawing anyway would smear a stale frame.

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,identity);
	DX8Wrapper::Set_Transform(D3DTS_VIEW,identity);

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	//DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueSpriteShader);

	DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);

	DX8Wrapper::Set_Texture(0,m_backgroundTexture);
	//Need these states in case texture is non-power-of-2
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Apply_Render_State_Changes();

	//Disable reading texture alpha since it's undefined.
	//DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);

	Int smudgesRemaining=count;
	set=m_usedSmudgeSetList.Head();	//first smudge set that needs rendering.
	Smudge	*remainingSmudgeStart=set->getUsedSmudgeList().Head();	//first smudge that needs rendering.

	while (smudgesRemaining)	//keep drawing smudges until we run out.
	{
		//Now that we know how many smudges need rendering, allocate vertex buffer space and copy verts.
		count=smudgesRemaining;

		if (count > SMUDGE_DRAW_SIZE)
			count = SMUDGE_DRAW_SIZE;

		Int smudgesInRenderBatch=0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,count*5);	//allocate 5 verts per smudge.
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();

			while (set)
			{
				Smudge *smudge=remainingSmudgeStart;

				for (; smudge; smudge=smudge->Succ())
				{
					if (!smudge->m_draw)
						continue;

					Smudge::smudgeVertex *smVerts = smudge->m_verts;

					//Check if we exceeded maximum number of smudges allowed per draw call.
					if (smudgesInRenderBatch >= count)
					{
						remainingSmudgeStart = smudge;
						goto flushSmudges;
					}

					//Set center vertex opacity.
					vertexDiffuse[4] = ((Int)(smudge->m_opacity * 255.0f) << 24) | THE_COLOR;

					for (Int i=0; i<5; i++)
					{
						verts->x=smVerts->pos.X;
						verts->y=smVerts->pos.Y;
						verts->z=smVerts->pos.Z;
						verts->nx=0;	//keep AGP write-combining active
						verts->ny=0;
						verts->nz=0;
						verts->diffuse=vertexDiffuse[i];	//set to transparent
						verts->u1=smVerts->uv.X;
						verts->v1=smVerts->uv.Y;
						verts->u2=0;	//keep AGP write-combining active
						verts->v2=0;
						verts++;
						smVerts++;
					}

					smudgesInRenderBatch++;
				}

				set=set->Succ();	//advance to next node.

				if (set)	//start next batch at beginning of set.
					remainingSmudgeStart = set->getUsedSmudgeList().Head();
			}
		}

flushSmudges:
		DX8Wrapper::Set_Vertex_Buffer(vb_access);

		DX8Wrapper::Draw_Triangles(0,smudgesInRenderBatch*4, 0, smudgesInRenderBatch*5);

//Debug Code which draws outline around smudge
/*		DX8Wrapper::_Get_D3D_Device8()->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
		DX8Wrapper::_Get_D3D_Device8()->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);
		DX8Wrapper::Draw_Triangles(	0,smudgesInRenderBatch*4, 0, smudgesInRenderBatch*5);
		DX8Wrapper::_Get_D3D_Device8()->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
		DX8Wrapper::_Get_D3D_Device8()->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
*/
		smudgesRemaining -= smudgesInRenderBatch;
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);

}
