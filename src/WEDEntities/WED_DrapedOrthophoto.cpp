/*
 * Copyright (c) 2008, Laminar Research.
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

#include "WED_DrapedOrthophoto.h"
#include "WED_TextureBezierNode.h"
#include "WED_Ring.h"

DEFINE_PERSISTENT(WED_DrapedOrthophoto)
TRIVIAL_COPY(WED_DrapedOrthophoto,WED_GISPolygon)

WED_DrapedOrthophoto::WED_DrapedOrthophoto(WED_Archive * a, int i) : WED_GISPolygon(a,i),
	heading(this,"Heading",      SQL_Name("WED_dsf_overlay", "heading"),   XML_Name("draped_orthophoto","heading"),   0.0,3,1),
	resource(this,"Resource",    SQL_Name("WED_dsf_overlay", "resource"),  XML_Name("draped_orthophoto","resource"),  ""),
	top(this,"Texture Top",      SQL_Name("WED_dsf_overlay", "tex_top"),   XML_Name("draped_orthophoto","tex_top"),   1.0,5,3),
	bottom(this,"Texture Bottom",SQL_Name("WED_dsf_overlay", "tex_bottom"),XML_Name("draped_orthophoto","tex_bottom"),0.0,5,3),
	left(this,"Texture Left",    SQL_Name("WED_dsf_overlay", "tex_left"),  XML_Name("draped_orthophoto","tex_left"),  0.0,5,3),
	right(this,"Texture Right",  SQL_Name("WED_dsf_overlay", "tex_right"), XML_Name("draped_orthophoto","tex_right"), 1.0,5,3)
{
}

WED_DrapedOrthophoto::~WED_DrapedOrthophoto()
{
}

void		WED_DrapedOrthophoto::GetResource(	  string& r) const
{
	r = resource.value;
}

void		WED_DrapedOrthophoto::SetResource(const string& r)
{
	resource = r;
}

double WED_DrapedOrthophoto::GetHeading(void) const
{
	return heading.value;
}

void WED_DrapedOrthophoto::SetHeading(double h)
{
	heading = h;
}

// this function tells if the resource is a .POL definition (true) or
// something having a different or no suffix at all (false)
// Its important, as orthophoto's are allowed to directly refer to the image. In such cases,
// the .POL, along with a .DDS version of the image, is created when writing the .DSF

bool WED_DrapedOrthophoto::IsNew(string * out_suffix) 
{
	//Find position
	int pos = resource.value.find_last_of('.',resource.value.size());
	if(pos == resource.value.npos)
		return false;
	
	//get the ending extension
	string testString = resource.value.substr(pos);
	
	//If it is not .pol
	
	if(testString != ".pol")
	{
		
		if(out_suffix != NULL)
		{
			*out_suffix = testString;
		}
		//it is new, therefore true
		return true;
	}
	else
	{
		//It is an old .pol file, therefore false
		return false;
	}
}

void  WED_DrapedOrthophoto::GetSubTexture(Bbox2& b)
{
	b.p1.x_ = left;
	b.p1.y_ = bottom;
	b.p2.x_ = right;
	b.p2.y_ = top;
}

void  WED_DrapedOrthophoto::SetSubTexture(const Bbox2& b)
{
	top    = b.p2.y();
	bottom = b.p1.y();
	right  = b.p2.x();
	left   = b.p1.x();
}

// This function recalculates the UV map, stretching the texture to fully cover the polygon.
//
// Unless its a quadrilateral (4-sided non-bezier polygons), the strecth is linear.
// I.e. the texture is scaled independently in u+v directions (aspect changes), until it covers
// all parts of the irregular shaped polygon. Resultingly some parts of the texture are usually
// not visible, but the texture appears 'undistorted'.
// Qudrilateral orthos are special - they are always streched to the corners, i.e. distorted as needed
// to exactly fit the poligon with all of the texture visible.

void WED_DrapedOrthophoto::Redrape(bool calcHdg)
{ 	if(HasLayer(gis_UV))
	{
		Bbox2  ll_box;           // the lon/lat bounding box of the poly - this is what the texture needs to cover
		Point2 ctr;

		Bbox2  uv_box;           // the part of the texture we are actually suppposed to use.
		GetSubTexture(uv_box);

		// We want to allow for rotated textures. Thus we have to rotate the coordinates before UV calculation
		// really doesn't matter around what point we rotate, as long it is somewehre nearby
		double angle = GetHeading();

		GetOuterRing()->GetNthPoint(0)->GetLocation(gis_Geo,ctr);   // simply choose the first point as coordinate rotation center

		int nh = GetNumEntities();

		for (int h = 0; h < nh; h++)
		{
			WED_Thing * ring = GetNthChild(h);
			int         np   = ring->CountChildren();
			WED_Ring * rCopy;
															// bad solution, as I can't find a way to cleanly remove this duplicate after use
			// rCopy = dynamic_cast <WED_Ring *> (ring->Clone());
			vector <BezierPoint2> pt_bak;                    // better solution: backup of the coordinates we're going to rotate
			for(int n = 0; n < np; ++n)
			{
				WED_TextureBezierNode * s = dynamic_cast <WED_TextureBezierNode *> (ring->GetNthChild(n));
				BezierPoint2 pt;

				s->GetBezierLocation(gis_Geo,pt);
				pt_bak.push_back(pt);
			}
			rCopy = dynamic_cast <WED_Ring *> (ring);        // now that we have a backup, we can mess with the original without guilt
			rCopy->Rotate(gis_Geo, ctr, -angle);             // rotate coordinates to match desired texture heading
			if (h==0) rCopy->GetBounds(gis_Geo, ll_box);     // get the bounding box in _rotated_ coordinates

			for(int n = 0; n < np; ++n)
			{
				WED_TextureBezierNode * src  = dynamic_cast <WED_TextureBezierNode *> (rCopy->GetNthChild(n));
				WED_TextureBezierNode * dest = dynamic_cast <WED_TextureBezierNode *> (ring->GetNthChild(n));
				Point2 st,uv;

				// 4-sided orthos w/no bezier nodes are special. They are always streched to these corners, i.e. distorted.
				if(h == 0 && np == 4 && !WED_HasBezierSeq(GetOuterRing()))
				{
					switch (n)
					{
						case 0: uv=uv_box.bottom_left();  break;
						case 1: uv=uv_box.bottom_right(); break;
						case 2: uv=uv_box.top_right();    break;
						case 3: uv=uv_box.top_left();
								if (calcHdg)
								{
									st = pt_bak[3].pt;
//								dest->GetLocation(gis_Geo,st);
									double hdg = 0.0;
									if (st.y()-ctr.y() != 0.0)
										hdg = 180.0/M_PI*atan((st.x()-ctr.x())*cos(ctr.y()/180.0*M_PI)/(st.y()-ctr.y()));  // very crude heading calculation
//									printf("hdg=%7.2lf\n",hdg);
//									fflush(stdout);
									SetHeading(hdg);
								}
					}
				}
				else
				{
					src->GetLocation(gis_Geo,st);
					uv = Point2((st.x() - ll_box.xmin()) / ll_box.xspan() * uv_box.xspan() + uv_box.xmin(),
								(st.y() - ll_box.ymin()) / ll_box.yspan() * uv_box.yspan() + uv_box.ymin());
				}
				dest->SetLocation(gis_UV,uv);

				if(src->GetControlHandleHi(gis_Geo,st))
				{
					dest->SetControlHandleHi(gis_UV,Point2(
						(st.x() - ll_box.xmin()) / ll_box.xspan() * uv_box.xspan() + uv_box.xmin(),
						(st.y() - ll_box.ymin()) / ll_box.yspan() * uv_box.yspan() + uv_box.ymin()));
				}
				if(src->GetControlHandleLo(gis_Geo,st))
				{
					dest->SetControlHandleLo(gis_UV,Point2(
						(st.x() - ll_box.xmin()) / ll_box.xspan() * uv_box.xspan() + uv_box.xmin(),
						(st.y() - ll_box.ymin()) / ll_box.yspan() * uv_box.yspan() + uv_box.ymin()));
				}
				
				WED_TextureBezierNode * p = dynamic_cast <WED_TextureBezierNode *> (ring->GetNthChild(n));
				p->SetBezierLocation(gis_Geo,pt_bak[n]);    // much better: restore to coordinate to what they were from the backup
			}
//			rCopy->Rotate(gis_Geo, ctr, angle);             // the ugly, ugly solution: rotate back to restore original coordinates
//			rCopy->Delete();                                // remove the Clone()'d + rotated ring again: won't work (assert in WED_thing:558)
		}
	}
}

void  WED_DrapedOrthophoto::PropEditCallback(int before)
{                                             // we want to catch changes of the heading property only, for now
#if 0 // DEV
	static double old = 0.0;
	if (before)                               // we will _always_ get called twice in succession. Before the edit takes place and after the update.
		old = heading.value;                  // so memorize the heading to see if it changed
	else
	{
		double new_heading = heading.value;
		if(fabs(old - new_heading) > 0.1)     // It changed. Nice to know, since we are called here even if some other property changed ..
		{
			printf("%9lx %12.9lf %12.9lf ch=%i\n",(long int) this, old, new_heading, GetNumEntities());
			Redrape(0);
		}
		else
			printf("false alarm\n");
		fflush(stdout);
	}
#else
	if (!before) Redrape(0);
#endif
}