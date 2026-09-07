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
 *                     $Archive:: /Commando/Code/ww3d2/lightenvironment.cpp                   $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 2/01/01 5:40p                                               $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "lightenvironment.h"
#include "matrix3d.h"
#include "camera.h"
#include "light.h"


/************************************************************************************************
**
** LightEnvironmentClass::InputLightStruct Implementation
**
************************************************************************************************/

void LightEnvironmentClass::InputLightStruct::Init_From_Directional_Light
(
	const LightClass & light,
	const Vector3 & object_center
)
{
	Direction = -light.Get_Transform().Get_Z_Vector();

	light.Get_Ambient(&Ambient);
	light.Get_Diffuse(&Diffuse);
}


/************************************************************************************************
**
** LightEnvironmentClass::OutputLightStruct Implementation
**
************************************************************************************************/

void LightEnvironmentClass::OutputLightStruct::Init
(
	const InputLightStruct & input,
	const Matrix3D & camera_tm
)
{
	Diffuse = input.Diffuse;
	Matrix3D::Inverse_Rotate_Vector(camera_tm,input.Direction,&Direction);

	// Guard against a direction that is invalid
	if(Direction.Length2() == 0.0f) {
		Direction.X = 1.0f;
	}
}



/************************************************************************************************
**
** LightEnvironmentClass Implementation
**
************************************************************************************************/

LightEnvironmentClass::LightEnvironmentClass() :
	LightCount(0),
	ObjectCenter(0,0,0),
	OutputAmbient(0,0,0)
{
}


LightEnvironmentClass::~LightEnvironmentClass()
{
}


void LightEnvironmentClass::Reset(const Vector3 & object_center,const Vector3 & ambient)
{
	LightCount = 0;
	ObjectCenter = object_center;
	OutputAmbient = ambient;
}


void LightEnvironmentClass::Add_Light(const LightClass & light)
{
	// TheSuperHackers @feature andytraber 07/09/2026 C7 of the clustered lighting plan:
	// point and spot lights are not this class's business any more. They are evaluated per
	// pixel out of the cluster grid (GpuLightListClass collects the same two lists Render_Seg
	// used to walk here), and a light that arrived here as well would be drawn twice --
	// once correctly and once as the flattened directional approximation this class used to
	// build. Ignored rather than asserted on: several callers hand this object whatever
	// their scene's light list holds and are not the right place to filter.
	if (light.Get_Type() != LightClass::DIRECTIONAL) {
		return;
	}

	// Jani: Don't accept lights that are almost black
	Vector3 diff;
	light.Get_Diffuse(&diff);
	if (diff[0]<0.05f && diff[1]<0.05f && diff[2]<0.05f) {
		return;
	}

	InputLightStruct new_light;
	new_light.Init_From_Directional_Light(light, ObjectCenter);

	/*
	** Add in the ambient component
	*/
	OutputAmbient += new_light.Ambient;

	// The insertion sort STAYS, and it is not vestigial. It used to do two jobs -- order the
	// slots, and decide which of many local lights survived into the four of them -- and only
	// the second is gone. Slot order is still load-bearing: DX8Wrapper::Set_Light_Environment
	// gives slot 0 the specular term, and the mesh shaders read the slots by index, so
	// appending the globals in list order instead of brightness order would reshuffle the sun
	// and its fills and change a frame that has no local lights in it at all. C7's null
	// control (civ_buildings at 0 differing pixels) is precisely the measurement that would
	// have caught that, which is the reason not to spend it proving the sort is redundant.
	for (int light_index=0; light_index < LightCount; light_index++) {
		if (new_light.Diffuse.Length2() > InputLights[light_index].Diffuse.Length2()) {

			// Move back the lights in the InputLights Array to make space for the new light.
			// The last light might be discarded if it moves off the array as it is the weakest light in the list.
			for (int i = LightCount; i > light_index; --i) {
				if (i < MAX_LIGHTS) {
					InputLights[i] = InputLights[i - 1];
				}
			}

			// Add the new light into the InputLights List where it belongs
			InputLights[light_index] = new_light;

			// Increment the light count if we have not reach the maximum lights limit yet
			LightCount = min(LightCount + 1, (int)MAX_LIGHTS);

			// Since we have inserted a new light, we are done for this function
			return;
		}
	}

	// If the light was not inserted but there are still spots empty in the InputLights list, insert the lights at the end of the list
	if (LightCount < MAX_LIGHTS) {
		InputLights[LightCount] = new_light;
		++LightCount;
	}
}

void LightEnvironmentClass::Pre_Render_Update(const Matrix3D & camera_tm)
{
	/*
	** Transform each light into camera space
	** and add up the ambient effect of each light
	*/
	for (int light_index=0; light_index<LightCount; light_index++) {
		OutputLights[light_index].Init(InputLights[light_index],camera_tm);
	}
	// Clamp ambient.
	OutputAmbient.X = WWMath::Clamp(OutputAmbient.X,0.0f,1.0f);
	OutputAmbient.Y = WWMath::Clamp(OutputAmbient.Y,0.0f,1.0f);
	OutputAmbient.Z = WWMath::Clamp(OutputAmbient.Z,0.0f,1.0f);
}
