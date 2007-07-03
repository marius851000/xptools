/* 
 * Copyright (c) 2004, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 *
 */
 
#include "XObjBuilder.h"
#include "XObjDefs.h"

XObjBuilder::XObjBuilder(XObj8 * inObj) : obj(inObj), lod(NULL)
{
	tex_repeat_s = 1.0;
	tex_repeat_t = 1.0;
	tex_offset_s = 0.0;
	tex_offset_t = 0.0;
	layer_group_offset = 0;
	
	SetDefaultState();
}

XObjBuilder::~XObjBuilder()
{
	if (lod)	
		EndLOD();
}
 	
void	XObjBuilder::BeginLOD(float inNear, float inFar)
{
	if (lod) 
		EndLOD();
	obj->lods.push_back(XObjLOD8());
	lod = &obj->lods.back();
	lod->lod_near = inNear;
	lod->lod_far = inFar;
	SetDefaultState();
}

void	XObjBuilder::EndLOD(void)
{
	SetDefaultState();
	lod = NULL;
}

void	XObjBuilder::Finish(void)
{
	if (!layer_group.empty() && !obj->lods.empty())
	{
		XObjCmd8 cmd;
		cmd.cmd = attr_Layer_Group;
		cmd.name = layer_group;
		cmd.params[0] = layer_group_offset;
		obj->lods.front().cmds.insert(obj->lods.front().cmds.begin(),cmd);
	}
}

void	XObjBuilder::SetAttribute(int attr)
{
	switch(attr) {
	case attr_Shade_Flat:	flat = 1;		break;
	case attr_Shade_Smooth:	flat = 0;		break;
	case attr_NoCull:		two_sided = 1;	break;
	case attr_Cull:			two_sided = 0;	break;
	case attr_Tex_Cockpit:	cockpit = -1;	break;
	case attr_Tex_Normal:	cockpit = -2;	break;
	case attr_No_Blend:		no_blend = 0.5;	break;
	case attr_Blend:		no_blend = -1.0;break;
	case attr_Hard:			hard = "object";break;
	case attr_No_Hard:		hard = "";		break;
	case attr_Reset: 
		diffuse[0] = 1.0; diffuse[1] = 1.0; diffuse[2] = 1.0;
		emission[0] = 0.0; emission[1] = 0.0; emission[2] = 0.0;
		shiny = 0.0;
		break;
	}
}

void	XObjBuilder::SetAttribute1(int attr, float v)
{
	switch(attr) {
	case attr_Offset: 	offset = v;					break;
	case attr_Shiny_Rat:shiny  = v;					break;
	case attr_No_Blend:	no_blend = v;				break;
	case attr_Tex_Cockpit_Subregion: cockpit = v;	break;	
	}
}

void XObjBuilder::SetAttribute1Named(int attr, float v, const char * s)
{
	if (attr == attr_Hard)
	{
		hard = s ? s : "";
	}
	else if (attr == attr_Layer_Group)
	{
		layer_group = s;
		layer_group_offset = v;
	}
	else
	{
		AssureLOD();
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = attr;
		lod->cmds.back().name = s;
		lod->cmds.back().params[0] = v;
	}
}

void	XObjBuilder::SetAttribute3(int attr, float v[3])
{
	switch(attr) {
	case attr_Emission_RGB: 
		emission[0] = v[0]; emission[1] = v[1]; emission[2] = v[2]; 
		break;
	case attr_Diffuse_RGB: 
		diffuse[0] = v[0]; diffuse[1] = v[1]; diffuse[2] = v[2]; 
		break;
	}
}

void	XObjBuilder::AccumTri(float inTri[24])
{
	inTri[6 ] = inTri[6 ] * tex_repeat_s + tex_offset_s;
	inTri[7 ] = inTri[7 ] * tex_repeat_t + tex_offset_t;
	inTri[14] = inTri[14] * tex_repeat_s + tex_offset_s;
	inTri[15] = inTri[15] * tex_repeat_t + tex_offset_t;
	inTri[22] = inTri[22] * tex_repeat_s + tex_offset_s;
	inTri[23] = inTri[23] * tex_repeat_t + tex_offset_t;

	int		idx1 = obj->geo_tri.accumulate(inTri   );
	int		idx2 = obj->geo_tri.accumulate(inTri+8 );
	int		idx3 = obj->geo_tri.accumulate(inTri+16);

	int		start_i = obj->indices.size();

	obj->indices.push_back(idx1);
	obj->indices.push_back(idx2);	
	obj->indices.push_back(idx3);
	
	int		end_i = obj->indices.size();
	
	AssureLOD();
	SyncAttrs();
		
	if (lod->cmds.empty() || 
		lod->cmds.back().cmd != obj8_Tris ||
		lod->cmds.back().idx_count + lod->cmds.back().idx_offset != start_i)
	{
		XObjCmd8	cmd;
		cmd.cmd = obj8_Tris;
		cmd.idx_offset = start_i;
		cmd.idx_count = end_i - start_i;
		lod->cmds.push_back(cmd);
	} else {
		lod->cmds.back().idx_count += (end_i - start_i);
	}	
}

void	XObjBuilder::AccumLine(float inLine[12])
{
	int idx1 = obj->geo_lines.accumulate(inLine  );
	int idx2 = obj->geo_lines.accumulate(inLine+6);
	
	int		start_i = obj->indices.size();

	obj->indices.push_back(idx1);
	obj->indices.push_back(idx2);

	int		end_i = obj->indices.size();
	
	AssureLOD();
	
	if (lod->cmds.empty() || 
		lod->cmds.back().cmd != obj8_Lines ||
		lod->cmds.back().idx_count + lod->cmds.back().idx_offset != start_i)
	{
		XObjCmd8	cmd;
		cmd.cmd = obj8_Lines;
		cmd.idx_offset = start_i;
		cmd.idx_count = end_i - start_i;
		lod->cmds.push_back(cmd);
	} else {
		lod->cmds.back().idx_count += (end_i - start_i);
	}

}

void	XObjBuilder::AccumLight(float inPoint[6])
{
	int idx = obj->geo_lights.accumulate(inPoint);
	
	AssureLOD();
	if (!lod->cmds.empty() &&
		 lod->cmds.back().cmd == obj8_Lights &&
		 (lod->cmds.back().idx_offset + lod->cmds.back().idx_count) == idx)
	{
		lod->cmds.back().idx_count++;
	} else {
		lod->cmds.push_back(XObjCmd8());
		lod->cmds.back().cmd = obj8_Lights;
		lod->cmds.back().idx_offset = idx;
		lod->cmds.back().idx_count = 1;
	}	
}

void	XObjBuilder::AccumLightNamed(float xyz[3], const char * name)
{
	AssureLOD();
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = obj8_LightNamed;
	lod->cmds.back().params[0] = xyz[0];
	lod->cmds.back().params[1] = xyz[1];
	lod->cmds.back().params[2] = xyz[2]	;
	lod->cmds.back().name = name;
}

void	XObjBuilder::AccumLightCustom(float xyz[3], float params[9], const char * dataref)
{
	AssureLOD();
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = obj8_LightCustom;
	lod->cmds.back().params[0] = xyz[0];
	lod->cmds.back().params[1] = xyz[1];
	lod->cmds.back().params[2] = xyz[2]	;
	for (int n = 0; n < 9; ++n)
		lod->cmds.back().params[n+3] = params[n];
	lod->cmds.back().name = dataref;
}

void	XObjBuilder::AccumSmoke(int cmd, float xyz[3], float size)
{
	AssureLOD();
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = cmd;
	lod->cmds.back().params[0] = xyz[0];
	lod->cmds.back().params[1] = xyz[1];
	lod->cmds.back().params[2] = xyz[2];
	lod->cmds.back().params[3] = size;	
}

void	XObjBuilder::AccumAnimBegin(void)
{
	AssureLOD(); 
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = anim_Begin;	
}

void	XObjBuilder::AccumAnimEnd(void)
{
	AssureLOD(); 
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = anim_End;
}

void	XObjBuilder::AccumTranslate(float xyz1[3], float xyz2[3], float v1, float v2, const char * ref)
{
	AccumTranslateBegin(ref);
	AccumTranslateKey(v1, xyz1);
	AccumTranslateKey(v2, xyz2);
	AccumTranslateEnd();
}

void	XObjBuilder::AccumRotate(float axis[3], float r1, float r2, float v1, float v2, const char * ref)
{
	AccumRotateBegin(axis, ref);
	AccumRotateKey(v1, r1);
	AccumRotateKey(v2, r2);
	AccumRotateEnd();
}

void	XObjBuilder::AccumTranslateBegin(const char * ref)
{
	AssureLOD(); 
	XObjAnim8 anim;
	anim.dataref = ref;
	obj->animation.push_back(anim);
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = anim_Translate;
	lod->cmds.back().idx_offset = obj->animation.size()-1;	
}

void	XObjBuilder::AccumTranslateKey(float v, float xyz[3])
{
	obj->animation.back().keyframes.push_back(XObjKey());
	obj->animation.back().keyframes.back().key = v;
	obj->animation.back().keyframes.back().v[0] = xyz[0];
	obj->animation.back().keyframes.back().v[1] = xyz[1];
	obj->animation.back().keyframes.back().v[2] = xyz[2];	
}


void	XObjBuilder::AccumTranslateEnd(void)
{
	if (obj->animation.back().keyframes.size() > 1 &&
		obj->animation.back().keyframes.front().key > obj->animation.back().keyframes.back().key)
	{
		reverse(obj->animation.back().keyframes.begin(),obj->animation.back().keyframes.end());
	}
}

void	XObjBuilder::AccumRotateBegin(float axis[3], const char * ref)
{
	AssureLOD(); 
	XObjAnim8 anim;
	anim.dataref = ref;
	anim.axis[0] = axis[0];	
	anim.axis[1] = axis[1];
	anim.axis[2] = axis[2];	
	obj->animation.push_back(anim);
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = anim_Rotate;
	lod->cmds.back().idx_offset = obj->animation.size()-1;	
}

void	XObjBuilder::AccumRotateKey(float v, float a)
{
	obj->animation.back().keyframes.push_back(XObjKey());
	obj->animation.back().keyframes.back().key = v;
	obj->animation.back().keyframes.back().v[0] = a;
}

void	XObjBuilder::AccumRotateEnd(void)
{
	if (obj->animation.back().keyframes.size() > 1 &&
		obj->animation.back().keyframes.front().key > obj->animation.back().keyframes.back().key)
	{
		reverse(obj->animation.back().keyframes.begin(),obj->animation.back().keyframes.end());
	}
}


void	XObjBuilder::AccumShow(float v1, float v2, const char * ref)
{
	AssureLOD(); 
	XObjAnim8 anim;
	anim.dataref = ref;
		
	anim.keyframes.push_back(XObjKey());
	anim.keyframes.back().key = v1;
	anim.keyframes.push_back(XObjKey());
	anim.keyframes.back().key = v2;
	
	obj->animation.push_back(anim);
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = anim_Show;
	lod->cmds.back().idx_offset = obj->animation.size()-1;	
}

void	XObjBuilder::AccumHide(float v1, float v2, const char * ref)
{
	AssureLOD(); 
	XObjAnim8 anim;
	anim.dataref = ref;

	anim.keyframes.push_back(XObjKey());
	anim.keyframes.back().key = v1;
	anim.keyframes.push_back(XObjKey());
	anim.keyframes.back().key = v2;
	
	obj->animation.push_back(anim);
	lod->cmds.push_back(XObjCmd8());
	lod->cmds.back().cmd = anim_Hide;
	lod->cmds.back().idx_offset = obj->animation.size()-1;	
	}

void	XObjBuilder::AssureLOD(void)
{
	if (lod == NULL)
	{
		obj->lods.push_back(XObjLOD8());
		obj->lods.back().lod_near = 0;
		obj->lods.back().lod_far  = 0;	
		lod = &obj->lods.back();
	}

}

void	XObjBuilder::SetDefaultState(void)
{
	o_hard = hard = "";
	o_flat = flat = 0;
	o_two_sided = two_sided = 0;
	o_no_blend = no_blend = -1.0;
	o_cockpit = cockpit = -2;
	o_offset = offset = 0.0;

	diffuse[0] = 1.0; diffuse[1] = 1.0; diffuse[2] = 1.0;
	o_diffuse[0] = 1.0; o_diffuse[1] = 1.0; o_diffuse[2] = 1.0;
	emission[0] = 0.0; emission[1] = 0.0; emission[2] = 0.0;
	o_emission[0] = 0.0; o_emission[1] = 0.0; o_emission[2] = 0.0;
	o_shiny = shiny = 0.0;
}

void XObjBuilder::SyncAttrs(void)
{
	if (flat != o_flat)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = flat ? attr_Shade_Flat : attr_Shade_Smooth;
		o_flat = flat;
	}
	
	if (hard != o_hard)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = hard.empty() ? attr_No_Hard : attr_Hard;
		if (hard != "object" && !hard.empty())
			lod->cmds.back().name = hard;
		o_hard = hard;
	}
	
	if (two_sided != o_two_sided)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = two_sided ? attr_NoCull : attr_Cull;
		o_two_sided = two_sided;
	}
	
	if (cockpit != o_cockpit)
	{
		lod->cmds.push_back(XObjCmd8()); 
		switch(cockpit) {
		case -2:
			lod->cmds.back().cmd = attr_Tex_Normal;
			break;
		case -1:
			lod->cmds.back().cmd = attr_Tex_Cockpit;
			break;
		default:
			lod->cmds.back().cmd = attr_Tex_Cockpit_Subregion;
			lod->cmds.back().params[0] = cockpit;
			break;
		}
		o_cockpit = cockpit;
	}
	
	if (no_blend != o_no_blend)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = (no_blend >= 0.0) ? attr_No_Blend : attr_Blend;
		if (no_blend >= 0.0)
			lod->cmds.back().params[0] = no_blend;
		o_no_blend = no_blend;
	}

	if (offset != o_offset)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = attr_Offset;
		lod->cmds.back().params[0] = offset;
		o_offset = offset;
	}

	if (emission[0] != o_emission[0] || emission[1] != o_emission[1] || emission[2] != o_emission[2] ||
		diffuse[0] != o_diffuse[0] || diffuse[1] != o_diffuse[1] || diffuse[2] != o_diffuse[2] ||
		shiny != o_shiny)
	if (emission[0] == 0.0 && emission[1] == 0.0 && emission[2] == 0.0 &&
		diffuse[0] == 1.0 && diffuse[1] == 1.0 && diffuse[2] == 1.0 && 
		shiny == 0.0)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = attr_Reset;
		o_emission[0] = emission[0];
		o_emission[1] = emission[1];
		o_emission[2] = emission[2];
		o_shiny = shiny;
		o_diffuse[0] = diffuse[0];
		o_diffuse[1] = diffuse[1];
		o_diffuse[2] = diffuse[2];
	}

	if (shiny != o_shiny)
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = attr_Shiny_Rat;
		lod->cmds.back().params[0] = shiny;
		o_shiny = shiny;
	}

	if (emission[0] != o_emission[0] || emission[1] != o_emission[1] || emission[2] != o_emission[2])
	{		
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = attr_Emission_RGB;
		lod->cmds.back().params[0] = emission[0];
		lod->cmds.back().params[1] = emission[1];
		lod->cmds.back().params[2] = emission[2];
		o_emission[0] = emission[0];
		o_emission[1] = emission[1];
		o_emission[2] = emission[2];
	}
	
	if (diffuse[0] != o_diffuse[0] || diffuse[1] != o_diffuse[1] || diffuse[2] != o_diffuse[2])
	{
		lod->cmds.push_back(XObjCmd8()); 
		lod->cmds.back().cmd = attr_Diffuse_RGB;
		lod->cmds.back().params[0] = diffuse[0];
		lod->cmds.back().params[1] = diffuse[1];
		lod->cmds.back().params[2] = diffuse[2];
		o_diffuse[0] = diffuse[0];
		o_diffuse[1] = diffuse[1];
		o_diffuse[2] = diffuse[2];
	}
}

void	XObjBuilder::SetTexRepeatParams(float repeat_s, float repeat_t, float offset_s, float offset_t)
{
	tex_repeat_s = repeat_s;
	tex_repeat_t = repeat_t;
	tex_offset_s = offset_s;
	tex_offset_t = offset_t;
}

