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

// RenderLight — platform-agnostic light snapshot for RenderStateStruct.
// Mirrors D3DLIGHT8 fields using engine-native types (Vector3). Enum values
// match D3DLIGHTTYPE so conversion is a static_cast. Phase 5b deliverable.

#pragma once

#include "vector3.h"

enum class RenderLightType : int
{
	POINT       = 1, // == D3DLIGHT_POINT
	SPOT        = 2, // == D3DLIGHT_SPOT
	DIRECTIONAL = 3  // == D3DLIGHT_DIRECTIONAL
};

struct RenderLight
{
	RenderLightType Type = RenderLightType::DIRECTIONAL;
	Vector3 Diffuse;
	Vector3 Specular;
	Vector3 Ambient;
	Vector3 Position;
	Vector3 Direction;
	float Range        = 0.0f;
	float Falloff      = 0.0f;
	float Theta        = 0.0f;
	float Phi          = 0.0f;
	float Attenuation0 = 0.0f;
	float Attenuation1 = 0.0f;
	float Attenuation2 = 0.0f;
};
