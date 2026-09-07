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
 *                 Project Name : WWPhys                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/lightenvironment.h                     $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                              $*
 *                                                                                             *
 *                     $Modtime:: 06/27/02 9:23a                                              $*
 *                                                                                             *
 *                    $Revision:: 5                                                           $*
 *                                                                                             *
 * 06/27/02 KM Shader system light environment updates                                       *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "always.h"
#include "vector3.h"


class Matrix3D;
class LightClass;


/**
** LightEnvironmentClass
** The scene's DIRECTIONAL lighting for a given object: the sun and moon (up to MAX_LIGHTS of
** them) plus the ambient term, transformed into camera space for the shaders to read.
**
** It used to be more than that. Point and spot lights were collapsed into this same
** four-slot array -- each one flattened into an equivalent directional light aimed at the
** object's bounding-sphere centre, with its distance and cone attenuation folded into the
** colour, and the four strongest of them kept. C7 of the clustered lighting plan
** deleted that: local lights are now evaluated per pixel out of the cluster grid
** (clustered.hlsli, GpuLightListClass), where they have real positions, real inverse-square
** falloff and no per-object limit at all. Add_Light now *ignores* anything that is not
** directional, so a caller that hands one in gets no CPU approximation rather than a second,
** wrong copy of a light the shader is already drawing correctly.
**
** What that leaves is exactly what the name always should have meant: sun plus ambient.
*/
class LightEnvironmentClass
{
public:

	LightEnvironmentClass();
	~LightEnvironmentClass();

	/*
	** Usage (starting from scratch each frame):
	** - Reset the object
	** - Set the scene ambient light (will be derived from lightmap sampling probably)
	** - Add in all overlapping lights, this object will keep the most important ones
	** - When ready to render, call Pre_Render_Update and push into the gerd.
	**
	** Usage (caching the lights, only done if the object and the lights are not moving)
	** - Reset and collect the lights once and keep this object around
	** - When ready to render, call Pre_Render_Update and push into the gerd.
	*/
	void					Reset(const Vector3 & object_center,const Vector3 & scene_ambient);
	void					Add_Light(const LightClass & light);
	void					Pre_Render_Update(const Matrix3D & camera_tm);

	/*
	** Accessors
	*/
	const Vector3 &	Get_Equivalent_Ambient() const			{ return OutputAmbient; }
	void Set_Output_Ambient(Vector3& oa) { OutputAmbient = oa; }
	int					Get_Light_Count() const					{ return LightCount; }
	const Vector3 &	Get_Light_Direction(int i)	const				{ return InputLights[i].Direction; }
	const Vector3 &	Get_Light_Diffuse(int i) const				{ return InputLights[i].Diffuse; }

	static int			Get_Max_Lights() { return MAX_LIGHTS; }
	enum { MAX_LIGHTS = 4 };	//Made this public, so other code can tell how many lights are allowed. - MW

	/* Equality over what a draw actually consumes -- the light count, the object centre and
	** the camera-space output lights -- so it can serve as a batching/redundancy key. It has
	** no caller today, and C7 checked that before deleting the point/spot fields: every field
	** it reads is on the directional side and survived untouched, so the deletion cannot have
	** changed the answer this returns for any pair of light environments a scene without local
	** lights builds. (On a scene *with* them the count differs, because those lights no longer
	** occupy slots here at all -- which is the intended change, not a batching accident.) */
	bool operator== (const LightEnvironmentClass& that) const
	{
		if (LightCount!=that.LightCount) return false;
		bool dif=!(ObjectCenter==that.ObjectCenter);
		dif|=OutputAmbient!=that.OutputAmbient;
		for (int i=0;i<LightCount;++i) {
			dif|=!(OutputLights[i].Diffuse==that.OutputLights[i].Diffuse);
			dif|=!(OutputLights[i].Direction==that.OutputLights[i].Direction);
			if (dif) return false;
		}
		return true;
	}

protected:

	struct InputLightStruct
	{
		void				Init_From_Directional_Light(const LightClass & light,const Vector3 & object_center);

		Vector3			Direction;
		Vector3			Ambient;
		Vector3			Diffuse;
	};

	struct OutputLightStruct
	{
		void				Init(const InputLightStruct & input,const Matrix3D & camera_tm);

		Vector3			Direction;						// direction to the light.
		Vector3			Diffuse;							// diffuse color * attenuation
	};

	/*
	** Member variables
	*/
	int					LightCount;
	Vector3				ObjectCenter;					// center of the object to be lit
	InputLightStruct	InputLights[MAX_LIGHTS];	// Sorted list of input lights from the greatest contributor to the least

	Vector3				OutputAmbient;					// scene ambient + lights' ambients
	OutputLightStruct	OutputLights[MAX_LIGHTS];	// output lights
};
