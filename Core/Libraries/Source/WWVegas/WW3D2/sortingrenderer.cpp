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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/sortingrenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "sortingrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "WW3D2/vertmaterial.h"
#include "texture.h"
#include "gfxstatewords.h"
#include <d3dx9math.h>
#include "statistics.h"
#include <WWDebug/wwprofile.h>
#include <algorithm>
#include <list>


bool SortingRendererClass::_EnableTriangleDraw=true;
static unsigned DEFAULT_SORTING_POLY_COUNT = 16384;	// (count * 3) must be less than 65536
static unsigned DEFAULT_SORTING_VERTEX_COUNT = 32768;	// count must be less than 65536

void SortingRendererClass::SetMinVertexBufferSize( unsigned val )
{
	DEFAULT_SORTING_VERTEX_COUNT = val;
	DEFAULT_SORTING_POLY_COUNT = val/2;	//typically have 2:1 vertex:triangle ratio.
}

struct ShortVectorIStruct
{
	unsigned short i;
	unsigned short j;
	unsigned short k;
};

struct TempIndexStruct
{
	ShortVectorIStruct tri;
	unsigned short idx;
	float z;
};

bool operator <(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z < r.z; }
bool operator <=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z <= r.z; }
bool operator >(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z > r.z; }
bool operator >=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z >= r.z; }
bool operator ==(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z == r.z; }
// ----------------------------------------------------------------------------
static
void InsertionSort(TempIndexStruct *begin, TempIndexStruct *end)
{
	for (TempIndexStruct *iter = begin + 1; iter < end; ++iter) {
		TempIndexStruct val = iter[0];
		TempIndexStruct *insert = iter;
		while (insert != begin && insert[-1] > val) {
			insert[0] = insert[-1];
			insert -= 1;
		}
		insert[0] = val;
	}
}

// ----------------------------------------------------------------------------
static
void Sort(TempIndexStruct *begin, TempIndexStruct *end)
{
	const int diff = end - begin;
	if (diff <= 16) {
		// Insertion sort has less overhead for small arrays
		InsertionSort(begin, end);
	} else {
		// Choose the median of begin, mid, and (end - 1) as the partitioning element.
		// Rearrange so that *(begin + 1) <= *begin <= *(end - 1).  These will be guard
		// elements.
		TempIndexStruct *mid = begin + diff/2;
		std::swap(mid[0], begin[1]);
		if (begin[1] > end[-1]) {
			std::swap(begin[1], end[-1]);
		}
		if (begin[0] > end[-1]) {
			std::swap(begin[0], end[-1]);
		}
		if (begin[1] > begin[0]) {
			std::swap(begin[1], begin[0]);
		}

		// *begin is now the partitioning element
		TempIndexStruct *begin1 = begin + 1;	// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *end1 = end - 1;			// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *left = begin + 1;
		TempIndexStruct *right = end - 1;
		for (;;) {
#if 0		// TODO: Temp fix until I find out who is passing me NaN.
			do ++left; while (left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right[0] > begin[0]);	// Scan down to find element <= than partition
#else
			do ++left; while (left < end1 && left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right > begin1 && right[0] > begin[0]);	// Scan down to find element <= than partition
#endif
			if (right < left) break;									// Pointers crossed.  Partitioning completed.
			std::swap(left[0], right[0]);							// Exchange elements.
		}
		std::swap(begin[0], right[0]);							// Insert partition element

		// Sort the smaller subarray first then the larger
		if (right - begin > end - (right + 1)) {
			Sort(right + 1, end);
			Sort(begin, right);
		} else {
			Sort(begin, right);
			Sort(right + 1, end);
		}
	}
}

// ----------------------------------------------------------------------------

class SortingNodeStruct
{
	W3DMPO_CODE(SortingNodeStruct)

public:
	RenderStateStruct sorting_state;

	// What the draw was, captured where it was queued.
	//
	// Sorted geometry is submitted at one point in the frame and drawn at another, so
	// none of the wrapper's per-draw routing state is still standing at flush time --
	// which is why Flush() has to clear the declaration rather than let the flush site's
	// own scope be inherited by everything in the queue. Carrying the three values on
	// the node is what lets a sorted draw be described by what queued it instead of by
	// nothing at all: without them every sorted effect fell to fixed function, 75994
	// draws a window on chinooks.rep and the single largest group left.
	MeshTechnique technique;
	bool mesh_has_solid_pass;
	bool mesh_renderer_draw;

	// And whether this draw was one of the airborne sprites that may fade where it meets
	// the scene, for the same reason and with the same failure mode as the three above.
	//
	// The soft fade is opt-in per draw site precisely because geometry that lies *on* the
	// ground -- roads, tank tracks, scorch marks, a waypoint line -- has its own depth
	// equal to the scene's, so a fade erases it outright instead of softening it. Reading
	// the wrapper's flag at flush time made that opt-in meaningless for anything deferred:
	// W3DSmudgeManager::render forces a flush from inside W3DParticleSystemManager::
	// doParticles' scope, so every node queued earlier in the frame -- the waypoint and
	// rally-point lines among them, queued back during terrain rendering -- was drawn as
	// if it were a smoke puff and faded to nothing along its whole length.
	//
	// It leaks the other way too: a particle queued into the pool and drawn by the later
	// flush found the scope already closed and lost the fade it was meant to have. One
	// value on the node answers both.
	bool soft_particles;
	float soft_particle_fade;

	Vector3 transformed_center;
	unsigned short start_index;			// First index used in the ib
	unsigned short polygon_count;			// Polygon count to process (3 indices = one polygon)
	unsigned short min_vertex_index;		// First index used in the vb
	unsigned short vertex_count;			// Number of vertices used in vb
};

typedef std::list<SortingNodeStruct*> SortingNodeStructList;
static SortingNodeStructList sorted_list;
static SortingNodeStructList unsorted_list;
static SortingNodeStructList clean_list;
static unsigned total_sorting_vertices;

static SortingNodeStruct* Get_Sorting_Struct()
{
	if (!clean_list.empty()) {
		SortingNodeStruct* state = clean_list.front();
		clean_list.pop_front();
		return state;
	}
	return W3DNEW SortingNodeStruct();
}

// ----------------------------------------------------------------------------
//
// Temporary arrays for the sorting system
//
// ----------------------------------------------------------------------------

static TempIndexStruct* temp_index_array;
static unsigned temp_index_array_count;

static TempIndexStruct* Get_Temp_Index_Array(unsigned count)
{
	if (count < DEFAULT_SORTING_POLY_COUNT)
		count = DEFAULT_SORTING_POLY_COUNT;
	if (count>temp_index_array_count) {
		delete[] temp_index_array;
		temp_index_array=W3DNEWARRAY TempIndexStruct[count];
		temp_index_array_count=count;
	}
	return temp_index_array;
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	const SphereClass& bounding_sphere,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	SNAPSHOT_SAY(("SortingRenderer::Insert(start_i: %d, polygons : %d, min_vi: %d, vertex_count: %d)",
		start_index,polygon_count,min_vertex_index,vertex_count));


	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();

	DX8Wrapper::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));


	state->start_index=start_index;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;

	// Captured here, restored in Apply_Render_State at flush time -- see the note on the
	// members. Whatever is standing now is what describes this draw; whatever is standing
	// when it is finally drawn describes something else entirely.
	state->technique = DX8Wrapper::Get_Mesh_Technique();
	state->mesh_has_solid_pass = DX8Wrapper::Get_Mesh_Has_Solid_Pass();
	state->mesh_renderer_draw = DX8Wrapper::Get_Mesh_Renderer_Draw();
	state->soft_particles = DX8Wrapper::Is_Soft_Particles();
	state->soft_particle_fade = DX8Wrapper::Get_Soft_Particle_Fade();

	if (bounding_sphere.Is_Valid())
	{
		D3DXMATRIX mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
		D3DXVECTOR3 vec=(D3DXVECTOR3&)bounding_sphere.Center;
		D3DXVECTOR4 transformed_vec;
		D3DXVec3Transform(
			&transformed_vec,
			&vec,
			&mtx);
		state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);

		Insert_To_Sorted_List(state);
	}
	else
	{
		// TheSuperHackers @perf stephanmeesters 04/07/2026 Nodes without bounding information do not require sorting.
		state->transformed_center = Vector3(0.0f, 0.0f, 0.0f);
		unsorted_list.push_back(state);
	}

#ifdef WWDEBUG
	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	unsigned short* indices=nullptr;
	SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
	WWASSERT(index_buffer);
	indices=index_buffer->index_buffer;
	WWASSERT(indices);
	indices+=state->start_index;
	indices+=state->sorting_state.iba_offset;

	for (int i=0;i<state->polygon_count;++i) {
		unsigned short idx1=indices[i*3]-state->min_vertex_index;
		unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
		unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
		WWASSERT(idx1<state->vertex_count);
		WWASSERT(idx2<state->vertex_count);
		WWASSERT(idx3<state->vertex_count);
	}
#endif // WWDEBUG
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system, with no bounding information.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Insert_Triangles(SphereClass(),start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
// Flush all sorting polygons.
//
// ----------------------------------------------------------------------------

void Release_Refs(SortingNodeStruct* state)
{
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(state->sorting_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(state->sorting_state.index_buffer);
	REF_PTR_RELEASE(state->sorting_state.material);
	for (i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i)
	{
		REF_PTR_RELEASE(state->sorting_state.Textures[i]);
	}
}

static unsigned overlapping_node_count;
static unsigned overlapping_polygon_count;
static unsigned overlapping_vertex_count;
static const unsigned MAX_OVERLAPPING_NODES=4096;
static SortingNodeStruct* overlapping_nodes[MAX_OVERLAPPING_NODES];

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorted_List(SortingNodeStruct *state)
{
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	for (SortingNodeStructList::iterator node = sorted_list.begin(); node != sorted_list.end(); ++node)
	{
		if (state->transformed_center.Z > (*node)->transformed_center.Z) {
			sorted_list.insert(node, state);
			return;
		}
	}

	sorted_list.push_back(state);
}

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct* state)
{
	if (overlapping_node_count>=MAX_OVERLAPPING_NODES) {
		Release_Refs(state);
		delete state;
		WWASSERT(0);
		return;
	}

	overlapping_nodes[overlapping_node_count]=state;
	overlapping_vertex_count+=state->vertex_count;
	overlapping_polygon_count+=state->polygon_count;
	overlapping_node_count++;
}

// ----------------------------------------------------------------------------
//static unsigned prevLight = 0xffffffff;

#ifdef RTS_DEBUG
// How often a sorted draw was being lit by somebody else's lights, and by how much.
// Counted where the restore happens, comparing what this node queued against what the
// tracked state was already holding -- which before the restore below was whichever draw
// happened to trigger the flush.
static unsigned s_sortedDraws = 0;
static unsigned s_sortedLightsDiffer = 0;
static unsigned s_sortedLightsEnableDiffer = 0;
static float s_sortedWorstDirDelta = 0.0f;
static float s_sortedWorstDiffuseDelta = 0.0f;

static float Max_Component_Delta(const D3DVECTOR& a, const D3DVECTOR& b)
{
	float d = WWMath::Fabs(a.x-b.x);
	float t = WWMath::Fabs(a.y-b.y); if (t>d) d=t;
	t = WWMath::Fabs(a.z-b.z); if (t>d) d=t;
	return d;
}

static float Max_Component_Delta(const D3DCOLORVALUE& a, const D3DCOLORVALUE& b)
{
	float d = WWMath::Fabs(a.r-b.r);
	float t = WWMath::Fabs(a.g-b.g); if (t>d) d=t;
	t = WWMath::Fabs(a.b-b.b); if (t>d) d=t;
	return d;
}

static void Census_Sorted_Lights(const RenderStateStruct& queued)
{
	++s_sortedDraws;
	bool differ = false;
	for (unsigned i=0;i<4;++i) {
		D3DLIGHT9 have;
		const bool had = DX8Wrapper::Peek_Light(i,have);
		if (had != queued.LightEnable[i]) { ++s_sortedLightsEnableDiffer; differ = true; continue; }
		if (!had) continue;
		const float dd = Max_Component_Delta(have.Direction, queued.Lights[i].Direction);
		const float cd = Max_Component_Delta(have.Diffuse, queued.Lights[i].Diffuse);
		if (dd > s_sortedWorstDirDelta) s_sortedWorstDirDelta = dd;
		if (cd > s_sortedWorstDiffuseDelta) s_sortedWorstDiffuseDelta = cd;
		if (dd > 0.001f || cd > 0.001f) differ = true;
	}
	if (differ) ++s_sortedLightsDiffer;
}

void SortingRendererClass::Debug_Report_Sorted_Lights()
{
	static unsigned frames = 0;
	if (++frames < 600) return;
	frames = 0;
	WWDEBUG_SAY(("SORTED LIGHTING over 600 frames: %u sorted draws, %u of them were queued "
		"with lights the tracked state was not already holding (%u differed in which lights "
		"were on at all). Worst direction component delta %.3f, worst diffuse %.3f. These are "
		"the draws the restore below changes; the total is the control.",
		s_sortedDraws, s_sortedLightsDiffer, s_sortedLightsEnableDiffer,
		s_sortedWorstDirDelta, s_sortedWorstDiffuseDelta));
	s_sortedDraws = 0;
	s_sortedLightsDiffer = 0;
	s_sortedLightsEnableDiffer = 0;
	s_sortedWorstDirDelta = 0.0f;
	s_sortedWorstDiffuseDelta = 0.0f;
}
#else
void SortingRendererClass::Debug_Report_Sorted_Lights() {}
#endif

static void Apply_Render_State(SortingNodeStruct* node)
{
	RenderStateStruct& render_state = node->sorting_state;

	// Put back what described this draw when it was queued. The wrapper's routing reads
	// all three, and at flush time they otherwise hold whatever the caller that happened
	// to trigger the flush left behind -- which is a different draw, in a different part
	// of the frame, and frequently not a mesh at all.
	DX8Wrapper::Set_Mesh_Technique(node->technique);
	DX8Wrapper::Set_Mesh_Has_Solid_Pass(node->mesh_has_solid_pass);
	DX8Wrapper::Set_Mesh_Renderer_Draw(node->mesh_renderer_draw);
	DX8Wrapper::Set_Soft_Particles(node->soft_particles, node->soft_particle_fade);

	DX8Wrapper::Set_Shader(render_state.shader);

	DX8Wrapper::Set_Material(render_state.material);

	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i)
	{
		DX8Wrapper::Set_Texture(i,render_state.Textures[i]);
	}

	// Through the tracked setter, not _Set_DX8_Transform. That one writes the device and
	// the wrapper's shadow of the device, and nothing else -- which is the whole of what
	// the fixed-function pipeline reads, so this was correct for as long as sorted
	// geometry only ever went there. The programmable path does not read the device: it
	// concatenates world*view*projection on the CPU out of the *tracked* render state, and
	// that was still holding whatever draw last set it through the normal path -- a
	// different object, elsewhere in the frame. So a sorted draw that routed to a shader
	// was transformed by another mesh's matrix and landed wherever that put it.
	//
	// It cost the rotor discs and the sorted light fixtures outright, and it is also what
	// took the mines: a mine renders normally until it cloaks, and cloaking makes it
	// translucent, which gives it a sort level and sends it through here -- so it vanished
	// a couple of seconds after being laid and looked for all the world like a stealth
	// bug. Nothing about the pixels was ever wrong. Forcing the shader to emit opaque
	// magenta produced no rotor either, which is what ruled out the blend and the alpha
	// and pointed here.
	DX8Wrapper::Set_Transform(D3DTS_WORLD,render_state.world);
	DX8Wrapper::Set_Transform(D3DTS_VIEW,render_state.view);


	// The lights, for the same reason and with the same history as the transforms above.
	//
	// What used to be here pushed render_state.Lights straight at the device with
	// Set_DX8_Light -- the device setter, not DX8Wrapper::Set_Light. That restored this
	// node's lights to the fixed-function transform-and-lighting stage, which draws
	// nothing, and never to the wrapper's tracked state, which is where the routing block
	// reads the vertex shader's LightDir and LightDiffuse constants from. So every sorted
	// draw was lit by whichever draw last set the tracked lights: a different object,
	// elsewhere in the frame, picked by whatever happened to trigger the flush.
	//
	// The enable flags are followed in the same order the queued copy filled them --
	// RenderStateStruct::operator= stops copying at the first disabled light -- so a light
	// this node did not queue is turned off rather than left holding somebody else's.
#ifdef RTS_DEBUG
	Census_Sorted_Lights(render_state);
#endif
	for (unsigned light=0; light<4; ++light) {
		if (render_state.LightEnable[light]) {
			DX8Wrapper::Set_Light(light,&render_state.Lights[light]);
		}
		else {
			DX8Wrapper::Set_Light(light,nullptr);
		}
	}
}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush_Sorting_Pool()
{
	if (!overlapping_node_count) return;

	SNAPSHOT_SAY(("SortingSystem - Flush"));

	// Fill dynamic index buffer with sorting index buffer vertices
	TempIndexStruct* tis=Get_Temp_Index_Array(overlapping_polygon_count);

	unsigned vertexAllocCount = overlapping_vertex_count;
	if (DynamicVBAccessClass::Get_Default_Vertex_Count() < DEFAULT_SORTING_VERTEX_COUNT)
		vertexAllocCount = DEFAULT_SORTING_VERTEX_COUNT;	//make sure that we force the DX8 dynamic vertex buffer to maximum size
	if (overlapping_vertex_count > vertexAllocCount)
		vertexAllocCount = overlapping_vertex_count;
	WWASSERT(DEFAULT_SORTING_VERTEX_COUNT == 1 || vertexAllocCount <= DEFAULT_SORTING_VERTEX_COUNT);
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertexAllocCount/*overlapping_vertex_count*/);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* dest_verts=(VertexFormatXYZNDUV2 *)lock.Get_Formatted_Vertex_Array();

		unsigned polygon_array_offset=0;
		unsigned vertex_array_offset=0;
		for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			VertexFormatXYZNDUV2* src_verts=nullptr;
			SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
			WWASSERT(vertex_buffer);
			src_verts=vertex_buffer->VertexBuffer;
			WWASSERT(src_verts);
			src_verts+=state->sorting_state.vba_offset;
			src_verts+=state->sorting_state.index_base_offset;
			src_verts+=state->min_vertex_index;

			// If you have a crash in here and "dest_verts" points to illegal memory area,
			// it is because D3D is in illegal state, and the only known cure is rebooting.
			// This illegal state is usually caused by Quake3-engine powered games such as MOHAA.
			memcpy(dest_verts, src_verts, sizeof(VertexFormatXYZNDUV2)*state->vertex_count);
			dest_verts += state->vertex_count;

			D3DXMATRIX d3d_mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
			const Matrix4x4& mtx=(const Matrix4x4&)d3d_mtx;

			unsigned short* indices=nullptr;
			SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
			WWASSERT(index_buffer);
			indices=index_buffer->index_buffer;
			WWASSERT(indices);
			indices+=state->start_index;
			indices+=state->sorting_state.iba_offset;

			if (mtx[0][2] == 0.0f && mtx[1][2] == 0.0f && mtx[3][2] == 0.0f && mtx[2][2] == 1.0f) {
				// The common case for particle systems.
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = idx1 + vertex_array_offset;
					tis_ptr->tri.j = idx2 + vertex_array_offset;
					tis_ptr->tri.k = idx3 + vertex_array_offset;
					tis_ptr->idx = node_id;
					tis_ptr->z = (v1->z + v2->z + v3->z)/3.0f;
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			} else {
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = idx1 + vertex_array_offset;
					tis_ptr->tri.j = idx2 + vertex_array_offset;
					tis_ptr->tri.k = idx3 + vertex_array_offset;
					tis_ptr->idx = node_id;
					tis_ptr->z = (mtx[0][2]*(v1->x + v2->x + v3->x) +
												mtx[1][2]*(v1->y + v2->y + v3->y) +
												mtx[2][2]*(v1->z + v2->z + v3->z))/3.0f + mtx[3][2];
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			}

			state->min_vertex_index=vertex_array_offset;

			polygon_array_offset+=state->polygon_count;
			vertex_array_offset+=state->vertex_count;
		}
	}

	Sort(tis, tis + overlapping_polygon_count);

	// TheSuperHackers @fix stephanmeesters 10/06/2026
	// Split rendering into chunks to prevent a crash when exceeding the 16-bit index buffer limit.
	constexpr const unsigned MAX_INDEX_CHUNK = 65535;
	unsigned chunkOffset = 0;
	while (chunkOffset < overlapping_polygon_count)
	{
		unsigned chunkCount = overlapping_polygon_count - chunkOffset;
		if (chunkCount * 3 > MAX_INDEX_CHUNK) {
			chunkCount = MAX_INDEX_CHUNK / 3;
		}
		const unsigned chunkEnd = chunkOffset + chunkCount;

		DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,chunkCount*3);
		{
			DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
			ShortVectorIStruct* sorted_polygon_index_array=(ShortVectorIStruct*)lock.Get_Index_Array();

			for (unsigned a=0;a<chunkCount;++a) {
				sorted_polygon_index_array[a]=tis[chunkOffset + a].tri;
			}
		}

		// Set index buffer and render!

		DX8Wrapper::Set_Index_Buffer(dyn_ib_access,0); // Override with this buffer (do something to prevent need for this!)
		DX8Wrapper::Set_Vertex_Buffer(dyn_vb_access); // Override with this buffer (do something to prevent need for this!)

		DX8Wrapper::Apply_Render_State_Changes();

		unsigned count_to_render=1;
		unsigned start_index=0;
		unsigned node_id=tis[chunkOffset].idx;
		for (unsigned i=chunkOffset + 1;i<chunkEnd;++i) {
			if (node_id!=tis[i].idx) {
				SortingNodeStruct* state=overlapping_nodes[node_id];
				Apply_Render_State(state);

				DX8Wrapper::Draw_Triangles(
					start_index*3,
					count_to_render,
					state->min_vertex_index,
					state->vertex_count);

				count_to_render=0;
				start_index=i - chunkOffset;
				node_id=tis[i].idx;
			}
			count_to_render++;	//keep track of number of polygons of same kind
		}

		// Render any remaining polygons...
		if (count_to_render) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			Apply_Render_State(state);

			DX8Wrapper::Draw_Triangles(
				start_index*3,
				count_to_render,
				state->min_vertex_index,
				state->vertex_count);
		}

		chunkOffset += chunkCount;
	}

	// Release all references and return nodes back to the clean list for the frame...
	for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		Release_Refs(state);
		clean_list.push_front(state);
	}
	overlapping_node_count=0;
	overlapping_polygon_count=0;
	overlapping_vertex_count=0;

	SNAPSHOT_SAY(("SortingSystem - Done flushing"));

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush()
{
	WWPROFILE("SortingRenderer::Flush");

	// Nothing flushed here is described by whatever declaration happens to be standing.
	//
	// Sorted geometry is queued at one point in the frame and drawn at another, and the
	// flush can be triggered from anywhere -- the smudge pass forces one so it can copy
	// the back buffer. A DeclaredTechniqueClass scope open at that moment would be
	// inherited by every deferred draw in the queue, none of which it describes. Measured
	// on chinooks.rep: 19412 draws a window of sorted building roof parts inheriting the
	// smudge pass's "effect" declaration.
	//
	// This scope is now the *floor* rather than the answer: each node restores what it
	// captured when it was queued (see Apply_Render_State), and this guarantees a node
	// that carried nothing gets nothing rather than the flush site's own state, and that
	// the last node's values do not outlive the flush.
	DeclaredTechniqueClass declareNothing(MESH_TECHNIQUE_UNCLASSIFIED, "sorting-flush");
	// The soft-particle declaration is the same kind of statement about the same draws, and
	// needs the same floor. W3DSmudgeManager::render forces a flush from inside the scope
	// W3DParticleSystemManager::doParticles opens, so without this every node queued
	// earlier in the frame is drawn as though it were one of that scope's sprites -- and a
	// ground-hugging one, whose depth matches the scene's, fades to nothing. That is what
	// erased the waypoint and rally-point lines: queued during terrain rendering, drawn
	// here, faded out along their whole length against the ground they lie on.
	SoftParticleScopeClass declareNoSprites(false, 0.0f);
	Matrix4x4 old_view;
	Matrix4x4 old_world;
	DX8Wrapper::Get_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Get_Transform(D3DTS_WORLD,old_world);

	// TheSuperHackers @perf stephanmeesters 04/07/2026
	// Splice nodes that have no bounding information (Z=0.0) at the correct location into the sorted list.
	SortingNodeStructList::iterator node = sorted_list.begin();
	while (node != sorted_list.end() && (*node)->transformed_center.Z > 0.0f) {
		++node;
	}
	sorted_list.splice(node, unsorted_list);

	while (!sorted_list.empty()) {
		SortingNodeStruct* state = sorted_list.front();
		sorted_list.pop_front();

		if ((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
			(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)) {
			Insert_To_Sorting_Pool(state);
		}
		else {
			// The other deferred path -- a node whose buffers are not sorting buffers is
			// drawn straight through rather than merged into the pool. Same deferral, so
			// the same restore: without it these draws inherit the flush site's state
			// exactly as the pooled ones did.
			DX8Wrapper::Set_Mesh_Technique(state->technique);
			DX8Wrapper::Set_Mesh_Has_Solid_Pass(state->mesh_has_solid_pass);
			DX8Wrapper::Set_Mesh_Renderer_Draw(state->mesh_renderer_draw);
			DX8Wrapper::Set_Soft_Particles(state->soft_particles, state->soft_particle_fade);
			DX8Wrapper::Set_Render_State(state->sorting_state);
			DX8Wrapper::Draw_Triangles(state->start_index,state->polygon_count,state->min_vertex_index,state->vertex_count);
			DX8Wrapper::Release_Render_State();
			DX8Wrapper::Set_Mesh_Technique(MESH_TECHNIQUE_UNCLASSIFIED);
			DX8Wrapper::Set_Mesh_Has_Solid_Pass(false);
			DX8Wrapper::Set_Mesh_Renderer_Draw(false);
			DX8Wrapper::Set_Soft_Particles(false, 0.0f);
			Release_Refs(state);
			clean_list.push_front(state);
		}
	}

	bool old_enable=DX8Wrapper::_Is_Triangle_Draw_Enabled();
	DX8Wrapper::_Enable_Triangle_Draw(_EnableTriangleDraw);
	Flush_Sorting_Pool();
	DX8Wrapper::_Enable_Triangle_Draw(old_enable);

	DX8Wrapper::Set_Index_Buffer(nullptr,0);
	DX8Wrapper::Set_Vertex_Buffer(nullptr);
	total_sorting_vertices=0;

	DynamicIBAccessClass::_Reset(false);
	DynamicVBAccessClass::_Reset(false);


	DX8Wrapper::Set_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,old_world);

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Deinit()
{
	//
	//	Flush the sorted list
	//
	while (!sorted_list.empty()) {
		delete sorted_list.front();
		sorted_list.pop_front();
	}

	//
	//	Flush the unsorted list
	//
	while (!unsorted_list.empty()) {
		delete unsorted_list.front();
		unsorted_list.pop_front();
	}

	//
	//	Flush the clean list
	//
	while (!clean_list.empty()) {
		delete clean_list.front();
		clean_list.pop_front();
	}

	delete[] temp_index_array;
	temp_index_array=nullptr;
	temp_index_array_count=0;
}
