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
 *                     $Archive:: /Commando/Code/ww3d2/matpass.h                              $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 6                                                           $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "WW3D2/shader.h"

class TextureClass;
class VertexMaterialClass;
class MeshModelClass;
class OBBoxClass;

/**
** MaterialPassClass
**
** This class wraps all of the data needed to describe an additional
** material pass for any object.  The motivation for this class is to
** implement certain types of special effects.  All data needed to
** apply the pass should be generated procedurally.  Typically a
** vertex processor will be used to generate any needed u-v's or vertex
** colors.  Alternatively, we could add the option to request to
** re-use the model's existing u-v's or vertex colors.
**
**
*/
class MaterialPassClass : public RefCountClass
{
public:

	MaterialPassClass();
	virtual ~MaterialPassClass() override;

	/// MW: Had to make this virtual so app can perform direct/custom D3D setup.
	virtual void	Install_Materials() const;
	virtual void	UnInstall_Materials() const { };	///< reset/cleanup D3D states

	void							Set_Texture(TextureClass * Texture,int stage = 0);
	void							Set_Shader(ShaderClass shader);
	void							Set_Material(VertexMaterialClass * mat);

	TextureClass *				Get_Texture(int stage = 0) const;
	VertexMaterialClass *	Get_Material() const;

	TextureClass *				Peek_Texture(int stage = 0) const;
	ShaderClass					Peek_Shader()	const							{ return Shader; }
	VertexMaterialClass *	Peek_Material() const						{ return Material; }

	void							Set_Cull_Volume(OBBoxClass * volume)		{ CullVolume = volume; }
	OBBoxClass *				Get_Cull_Volume() const					{ return CullVolume; }

	void							Enable_On_Translucent_Meshes(bool onoff)	{ EnableOnTranslucentMeshes = onoff; }
	bool							Is_Enabled_On_Translucent_Meshes()		{ return EnableOnTranslucentMeshes; }

	/*
	** May this pass be drawn over effect geometry -- a mesh no pass of which writes depth?
	**
	** A material pass re-draws the mesh's triangles with a material of its own, so what it
	** contributes is decided by the pass, not by the mesh's own texture or alpha. On a
	** surface that is the point: the triangles have coverage, and painting them again with
	** a projected texture decorates what is already there. On effect geometry there is no
	** coverage to decorate. A light shaft is a pair of quads whose entire shape lives in
	** the alpha of its texture, so a second pass paints the quads themselves -- and it is
	** most visible exactly where the effect is most transparent, which is the opposite of
	** what an overlay should do.
	**
	** Ordering compounds it. Effect geometry is usually sorted, so its own pass is deferred
	** to the sorting renderer's flush while a material pass is drawn immediately; the
	** overlay therefore lands *underneath* the thing it was meant to modulate.
	**
	** Default true: an overlay that has no opinion keeps the behaviour it always had.
	*/
	virtual bool				Is_Enabled_On_Effect_Geometry() const	{ return true; }

	static void					Enable_Per_Polygon_Culling(bool onoff)		{ EnablePerPolygonCulling = onoff; }
	static bool					Is_Per_Polygon_Culling_Enabled()		{ return EnablePerPolygonCulling; }

protected:

	enum { MAX_TEX_STAGES = 8 };

	TextureClass *				Texture[MAX_TEX_STAGES];
	ShaderClass					Shader;
	VertexMaterialClass *	Material;
	bool							EnableOnTranslucentMeshes;

	OBBoxClass *				CullVolume;
	static bool					EnablePerPolygonCulling;

};


inline TextureClass * MaterialPassClass::Peek_Texture(int stage) const
{
	WWASSERT(stage >= 0);
	WWASSERT(stage < MAX_TEX_STAGES);
	return Texture[stage];
}
