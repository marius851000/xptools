/* 
 * Copyright (c) 2013, Laminar Research.
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

#include "WED_Validate.h"

#include "WED_Globals.h"
#include "WED_Sign_Parser.h"
#include "WED_Runway.h"
#include "WED_Sealane.h"
#include "WED_Helipad.h"
#include "WED_Airport.h"
#include "WED_AirportSign.h"
#include "WED_ToolUtils.h"
#include "WED_FacadePlacement.h"
#include "WED_ForestPlacement.h"
#include "WED_ObjPlacement.h"
#include "WED_StringPlacement.h"
#include "WED_LinePlacement.h"
#include "WED_PolygonPlacement.h"
#include "WED_DrapedOrthophoto.h"
#include "WED_FacadeNode.h"
#include "WED_RampPosition.h"
#include "WED_TaxiRoute.h"
#include "WED_ATCFlow.h"
#include "WED_LibraryMgr.h"
#include "WED_AirportBoundary.h"
#include "WED_GISUtils.h"
#include "WED_Group.h"
#include "WED_ATCRunwayUse.h"
#include "WED_ATCWindRule.h"
#include "WED_EnumSystem.h"
#include "WED_Taxiway.h"
#include "WED_GroupCommands.h"
#include "IGIS.h"


#include "AptDefs.h"
#include "IResolver.h"

#include "GISUtils.h"
#include "PlatformUtils.h"
#include "MathUtils.h"
#include "WED_ATCFrequency.h"

#define MAX_LON_SPAN_GATEWAY 0.2
#define MAX_LAT_SPAN_GATEWAY 0.2

// For now this is a debug mode - we printf all airport ICAOs with problems and don't interrupt validate.
#define FIND_BAD_AIRPORTS 1

#define CHECK_ZERO_LENGTH 1


static set<string>	s_used_rwy;
static set<string>	s_used_hel;
static set<string>	s_icao;
static set<string>	s_flow_names;
static set<int>		s_legal_rwy_oneway;
static set<int>		s_legal_rwy_twoway;
static string name;
static string n1;
static string n2;

static bool GetThingResouce(WED_Thing * who, string& r)
{
	WED_ObjPlacement * obj;
	WED_FacadePlacement * fac;
	WED_ForestPlacement * fst;
	WED_LinePlacement * lin;
	WED_StringPlacement * str;
	WED_PolygonPlacement * pol;
	
	#define CAST_WITH_CHECK(CLASS,VAR) \
	if(who->GetClass() == CLASS::sClass && (VAR = dynamic_cast<CLASS *>(who)) != NULL) { \
		VAR->GetResource(r); \
		return true; } 
	
	CAST_WITH_CHECK(WED_ObjPlacement,obj)
	CAST_WITH_CHECK(WED_FacadePlacement,fac)
	CAST_WITH_CHECK(WED_ForestPlacement,fst)
	CAST_WITH_CHECK(WED_LinePlacement,lin)
	CAST_WITH_CHECK(WED_StringPlacement,str)
	CAST_WITH_CHECK(WED_PolygonPlacement,pol)
	
	return false;
}

static WED_Thing* ValidateOneTaxiSign(WED_Thing* who, string& msg)
{
	/*--Taxi Sign Validation Rules---------------------------------------------
		See Taxi Sign spec and parser for detailed validation rules
	 */

		WED_AirportSign * airSign = dynamic_cast<WED_AirportSign*>(who);
		string signName;
		airSign->GetName(signName);

		//Create the necessary parts for a parsing operation
		parser_in_info in(signName);
		parser_out_info out;

		ParserTaxiSign(in,out);
		if(out.errors.size() > 0)
		{
			int MAX_ERRORS = 12;//TODO - Is this good?
			for (int i = 0; i < MAX_ERRORS && i < out.errors.size(); i++)
			{
				msg += out.errors[i].msg;
				msg += '\n';
			}
			return who;
		}

	return NULL;
}

static WED_Thing* ValidateOneTaxiway(WED_Thing* who, string& msg)
{
	/*--Taxiway Validation Rules-----------------------------------------------
		Water is not a valide surface type for taxiways
		Outer boundry of taxiway is not at least 3 sided
		Hole of taxiway is not at least 3 sided
	 */

	WED_Taxiway * twy = dynamic_cast<WED_Taxiway*>(who);
	if(twy->GetSurface() == surf_Water && gExportTarget == wet_gateway)
	{
		msg = "Water is not a valid surface type for taxiways";
#if !FIND_BAD_AIRPORTS
		DoUserAlert(msg.c_str());
		return who;
#endif
		printf("%s", msg.c_str());
	}

	IGISPointSequence * ps;
	ps = twy->GetOuterRing();
	if(!ps->IsClosed() || ps->GetNumSides() < 3)
	{
		msg = "Outer boundary of taxiway is not at least 3 sided.";
	}
	else
	{
		for(int h = 0; h < twy->GetNumHoles(); ++h)
		{
			ps = twy->GetNthHole(h);
			if(!ps->IsClosed() || ps->GetNumSides() < 3)
			{
				// Ben says: two-point holes are INSANELY hard to find.  So we do the rare thing and intentionally
				// hilite the hole so that the user can nuke it.				
				msg = "Hole of taxiway is not at least 3 sided.";
				WED_Thing * h = dynamic_cast<WED_Thing *>(ps);
				if(h) 
				{
#if !FIND_BAD_AIRPORTS
					DoUserAlert(msg.c_str());
					return h;
#endif			
				}
			}
		}
	}

	return NULL;
}

static WED_Thing* ValidateOneRunwayOrSealane(WED_Thing* who, string& msg)
{
	/*--Runway/Sealane Validation Rules----------------------------------------
		Duplicate Runway/Sealane Name
		Low-end Name rules
		  - Empty Low-end Name
		  - Illegal suffix for low-end runway
		  - Illegal characters in its low-end name
		  - Illegal low-end number, must be between 1 and 36
		High-end Name rules
		  - Empty High-end Name
		  - Illegal suffix for high-end runway
		  - Illegal characters in its high-end name
		  - Illegal high-end number, must be between 19 and 36
		Other Suffix rules
		  - Mismatched suffixes, L vs R etc
		  - Suffix on only one end
		Runway Numbers rules
		  - Low number must be first
		  - High end is not the reciprocal of the low end
		Runway Other rules
		  - Width must be atleast 1 meter wide
		  - Water us bit a valid surface type (runway only)
		  - Overlapping displaced thresholds
		  - Illegal surface roughness, shuold be between 0 and 1, inclusive
		  - Has end outside world map
		  - Needs to be reversed to match its name
		  - Misaligned with its runway name
	 */

	if (s_used_rwy.count(name))	msg = "The runway/sealane name '" + name + "' has already been used.";
	s_used_rwy.insert(name);
	string::size_type p = name.find("/");
	if (p != name.npos)
	{
		n1 = name.substr(0,p);
		n2 = name.substr(p+1);
	} else
		n1 = name;

	int suf1 = 0, suf2 = 0;
	int	num1 = -1, num2 = -1;

	if (n1.empty())	msg = "The runway/sealane '" + name + "' has an empty low-end name.";
	else {
		int suffix = n1[n1.length()-1];
		if (suffix < '0' || suffix > '9')
		{
			if (suffix == 'L' || suffix == 'R' || suffix == 'C' || suffix == 'S') suf1 = suffix;
			else msg = "The runway/sealane '" + name + "' has an illegal suffix for the low-end runway.";
			n1.erase(n1.length()-1);
		}

		int i;
		for (i = 0; i < n1.length(); ++i)
		if (n1[i] < '0' || n1[i] > '9')
		{
			msg = "The runway/sealane '" + name + "' has an illegal characters in its low-end name.";
			break;
		}
		if (i == n1.length())
		{
			num1 = atoi(n1.c_str());
		}
		if (num1 < 1 || num1 > 36)
		{
			msg = "The runway/sealane '" + name + "' has an illegal low-end number, which must be between 1 and 36.";
			num1 = -1;
		}
	}

	if (p != name.npos)
	{
		if (n2.empty())	msg = "The runway/sealane '" + name + "' has an empty high-end name.";
		else {
			int suffix = n2[n2.length()-1];
			if (suffix < '0' || suffix > '9')
			{
				if (suffix == 'L' || suffix == 'R' || suffix == 'C' || suffix == 'S') suf2 = suffix;
				else msg = "The runway/sealane '" + name + "' has an illegal suffix for the high-end runway.";
				n2.erase(n2.length()-1);
			}

			int i;
			for (i = 0; i < n2.length(); ++i)
			if (n2[i] < '0' || n2[i] > '9')
			{
				msg = "The runway/sealane '" + name + "' has an illegal characters in its high-end name.";
				break;
			}
			if (i == n2.length())
			{
				num2 = atoi(n2.c_str());
			}
			if (num2 < 19 || num2 > 36)
			{
				msg = "The runway/sealane '" + name + "' has an illegal high-end number, which must be between 19 and 36.";
				num2 = -1;
			}
		}
	}

	if (suf1 != 0 && suf2 != 0)
	{
		if ((suf1 == 'L' && suf2 != 'R') ||
			(suf1 == 'R' && suf2 != 'L') ||
			(suf1 == 'C' && suf2 != 'C') ||
			(suf1 == 'S' && suf2 != 'S'))
				msg = "The runway/sealane '" + name + "' has mismatched suffixes - check L vs R, etc.";
	}
	else if((suf1 == 0) != (suf2 == 0))
	{
		msg = "The runway/sealane '" + name + "' has a suffix on only one end.";
	}
	if (num1 != -1 && num2 != -1)
	{
		if (num2 < num1)
			msg = "The runway/sealane '" + name + "' has mismatched runway numbers - the low number must be first.'";
		else if (num2 != num1 + 18)
			msg = "The runway/sealane '" + name + "' has mismatched runway numbers - high end is not the reciprocal of the low-end.";
	}

	if (msg.empty())
	{
		WED_GISLine_Width * lw = dynamic_cast<WED_GISLine_Width *>(who);
		Assert(lw);			
		if (lw->GetWidth() < 1.0) msg = "The runway/sealane '" + name + "' must be at least one meter wide.";

		WED_Runway * rwy = dynamic_cast<WED_Runway *>(who);
		if (rwy)
		{
			if(rwy->GetSurface() == surf_Water && gExportTarget == wet_gateway)
			{
				msg = "Water is not a valid surface type for runways";
				#if !FIND_BAD_AIRPORTS
				DoUserAlert(msg.c_str());
				return who;
				#endif
			}

			if (rwy->GetDisp1() + rwy->GetDisp2() > rwy->GetLength()) msg = "The runway/sealane '" + name + "' has overlapping displaced thresholds.";
			
			#if !GATEWAY_IMPORT_FEATURES
				if(rwy->GetRoughness() < 0.0 || rwy->GetRoughness() > 1.0) msg = "The runway '" + name + "' has an illegal surface roughness. It should be in the range 0 to 1.";
			#endif
			
		}
		
		Point2 ends[2];
		lw->GetNthPoint(0)->GetLocation(gis_Geo,ends[0]);
		lw->GetNthPoint(1)->GetLocation(gis_Geo,ends[1]);
		Bbox2	runway_extent(ends[0],ends[1]);
		if (runway_extent.xmin() < -180.0 ||
			runway_extent.xmax() >  180.0 ||
			runway_extent.ymin() <  -90.0 ||
			runway_extent.ymax() >   90.0)
		{
			msg = "The runway/sealane '" + name + "' has an end outside the World Map.";
		}	
		else
		{
			#if !GATEWAY_IMPORT_FEATURES
				double heading, len;
				Point2 ctr;
				Quad_2to1(ends, ctr, heading, len);
				double approx_heading = num1 * 10.0;
				double heading_delta = fabs(dobwrap(approx_heading - heading, -180.0, 180.0));
				if(heading_delta > 135.0)
					msg = "The runway/sealane '" + name + "' needs to be reversed to match its name.";
				else if(heading_delta > 45.0)
					msg = "The runway/sealane '" + name + "' is misaligned with its runway name.";				
			#endif
		}
	}

	return NULL;
}

static WED_Thing* ValidateOneHelipad(WED_Thing* who, string& msg)
{
	/*--Helipad Validation Rules-----------------------------------------------
		Helipad Name rules
		  - Name already used
		  - The selected helipad has no name
		  - Name does not start with letter H
		  - Name is longer than 3 characters
		  - Contains illegal characters, must be in the form of H<number>
		Helipad Width rules
		  - Helipad is less than one meter wide
		  - Helipad is less than one meter long
	 */
	if (s_used_hel.count(name))	msg = "The helipad name '" + name + "' has already been used.";
	s_used_hel.insert(name);

	n1 = name;
	if (n1.empty())
	{
		msg = "The selected helipad has no name.";
	}
	else
	{
		if (n1[0] != 'H')
		{
			msg = "The helipad '" + name + "' does not start with the letter H.";
		}
		else
		{
			if(n1.length() > 3)
			{
				msg = "The helipad '" + name + "' is longer than the maximum 3 characters.";
			}

			n1.erase(0,1);
			for (int i = 0; i < n1.length(); ++i)
			{
				if (n1[i] < '0' || n1[i] > '9')
				{
					msg = "The helipad '" + name + "' conntains illegal characters in its name.  It must be in the form H<number>.";
					break;
				}
			}
		}
	}
	if (msg.empty())
	{
		WED_Helipad * heli = dynamic_cast<WED_Helipad *>(who);
		if (heli->GetWidth() < 1.0) msg = "The helipad '" + name + "' is less than one meter wide.";
		if (heli->GetLength() < 1.0) msg = "The helipad '" + name + "' is less than one meter long.";
	}

	return NULL;
}

static WED_Thing* ValidateOneAirport(WED_Thing* who, string& msg)
{
	/*--Validate Airport Rules-------------------------------------------------
		Airport Name rules
		  - Empty ICAO code
		  - ICAO used twice in airport
	 */

	s_used_hel.clear();
	s_used_rwy.clear();
	s_flow_names.clear();
	s_legal_rwy_oneway.clear();
	s_legal_rwy_twoway.clear();

	WED_Airport * apt = dynamic_cast<WED_Airport *>(who);
	if(who)
	{
		WED_GetAllRunwaysOneway(apt,s_legal_rwy_oneway);
		WED_GetAllRunwaysTwoway(apt,s_legal_rwy_twoway);

		string icao;
		apt->GetICAO(icao);
		if(icao.empty())
		{
			msg = "The airport '" + name + "' has an empty ICAO code.";
		}
		else
		{
			if(s_icao.count(icao))
			{
				msg = "The airport ICAO code '" + icao + "' is used twice in your WED project file.";
			}
			else
			{
				s_icao.insert(icao);
			}
		}
	}

	return NULL;
}

static WED_Thing* ValidateOneFacade(WED_Thing* who, string& msg)
{
	/*--Facade Validate Rules--------------------------------------------------
		wet_xplane_900 rules
		  - Custom facade wall choices are only in X-Plane 10 and newer
		  - Curved facades are only supported in X-Plane 10 and newer
		Other rules
		  - Facades may not have holes in them
	 */

	WED_FacadePlacement * fac = dynamic_cast<WED_FacadePlacement*>(who);
	DebugAssert(who);
	if(gExportTarget == wet_xplane_900 && fac->HasCustomWalls())
	{
		msg = "Custom facade wall choices are only supported in X-Plane 10 and newer.";
	}
	
	if(fac->GetNumHoles() > 0)
	{
		msg = "Facades may not have holes in them.";
	}

	if(gExportTarget == wet_xplane_900 && WED_HasBezierPol(fac))
		msg = "Curved facades are only supported in X-Plane 10 and newer.";

	return NULL;
}

static WED_Thing * ValidateRecursive(WED_Thing * who, WED_LibraryMgr * lib_mgr)
{
	int i;
	who->GetName(name);
	string msg;

	// Don't validate hidden stuff - we won't export it!
	WED_Entity * ee = dynamic_cast<WED_Entity *>(who);
	if(ee && ee->GetHidden())
		return NULL;

	WED_Thing * bad_thing = NULL;
	if(who->GetClass() == WED_AirportSign::sClass)
	{
		ValidateOneTaxiSign(who,msg);
		//Caught at bottom
	}

	//------------------------------------------------------------------------------------
	// CHECKS FOR DANGLING PARTS - THIS SHOULD NOT HAPPEN BUT EVERY NOW AND THEN IT DOES
	//------------------------------------------------------------------------------------

	IGISPointSequence * ps = dynamic_cast<IGISPointSequence *>(who);
	if(ps)
	{
		int nn = ps->GetNumSides();
		if(nn < 1)
		{
			msg = "Linear feature needs at least two points.";
		}
		
		for(int n = 0; n < nn; ++n)
		{
			Bezier2 b; Segment2 s;
			bool bez = ps->GetSide(gis_Geo,n,s,b);
			if(bez) {s.p1 = b.p1; s.p2 = b.p2; }
			
			if(s.p1 == s.p2)
			{
				WED_Thing * parent = who->GetParent();
				if(parent)
				{
					if (parent->GetClass() == WED_ForestPlacement::sClass ||
						parent->GetClass() == WED_FacadePlacement::sClass ||
						parent->GetClass() == WED_DrapedOrthophoto::sClass ||
						parent->GetClass() == WED_PolygonPlacement::sClass)
					{
					#if CHECK_ZERO_LENGTH
						msg = string("Zero length side on line or polygon, parent is a '") + parent->GetClass() + "'.";
					#endif
					}
				}
			}
		}
	}

	//------------------------------------------------------------------------------------
	// CHECKS FOR GENERAL APT.DAT BOGUSNESS
	//------------------------------------------------------------------------------------
	
	if(who->GetClass() == WED_Taxiway::sClass)
	{
		ValidateOneTaxiway(who,msg);
	}

	if (who->GetClass() == WED_Runway::sClass || who->GetClass() == WED_Sealane::sClass)
	{
		bad_thing = ValidateOneRunwayOrSealane(who, msg);

		#if FIND_BAD_AIRPORTS
		if(bad_thing != NULL)
		{
			return bad_thing;
		}
		#endif
	}

	if (who->GetClass() == WED_Helipad::sClass)
	{
		ValidateOneHelipad(who, msg);
	}

	if(who->GetClass() == WED_Airport::sClass)
	{
		ValidateOneAirport(who,msg);
	}

	//------------------------------------------------------------------------------------
	// CHECKS FOR V10 DSF OVERLAY EXTENSIONS
	//------------------------------------------------------------------------------------

	if(who->GetClass() == WED_FacadePlacement::sClass)
	{
		ValidateOneFacade(who, msg);
	}
	
	if(who->GetClass() == WED_ForestPlacement::sClass)
	{
		WED_ForestPlacement * fst = dynamic_cast<WED_ForestPlacement *>(who);
		DebugAssert(fst);
		if(gExportTarget == wet_xplane_900 && fst->GetFillMode() != dsf_fill_area)
			msg = "Line and point forests are only supported in X-Plane 10 and newer.";
	}
	
	//------------------------------------------------------------------------------------
	// CHECKS FOR ATC FIELD BUGS
	//------------------------------------------------------------------------------------	
	
	if(gExportTarget != wet_xplane_900)	// not even legal for v9
	{
		WED_ATCFlow * flow;
		WED_ATCWindRule * wind;
		WED_TaxiRoute * taxi;
		
		if(who->GetClass() == WED_ATCFlow::sClass && ((flow = dynamic_cast<WED_ATCFlow *>(who)) != NULL))
		{
			AptFlow_t exp;
			flow->Export(exp);
			if(exp.icao.empty())
				msg = "ATC Flow '" + name + "' has a blank ICAO code for its METAR source.";
				
			if(name.empty())
				msg = "An ATC flow has a blank name.  You must name every flow.";
				
			if(s_flow_names.count(name) > 0)
			{
				msg = "You have two airport flows named '" + name + "'.  Every ATC flow name must be unique.";				
			}
			else 
				s_flow_names.insert(name);
			
			// Make sure we have at least one runway to use.  The exported flow does NOT have this - it only contains exports
			// of properties of the apt flow rule itself!
			int rwy_rule_count = 0;
			int nn = who->CountChildren();
			for(int n = 0; n < nn; ++n)
			{
				WED_Thing * c = who->GetNthChild(n);
				if(c->GetClass() == WED_ATCRunwayUse::sClass)
					++rwy_rule_count;
			}
			
			if(rwy_rule_count == 0)
				msg = "You have an airport flow with no runway use rules.  You need at least oneway use rule to create an active runway.";
				
			if(s_legal_rwy_oneway.count(flow->GetPatternRunway()) == 0)
				msg = "The pattern runway " + string(ENUM_Desc(flow->GetPatternRunway())) + " is illegal for the ATC flow '" + name + "' because it is not a runway at this airport.";
		}

		if(who->GetClass() == WED_ATCWindRule::sClass && ((wind = dynamic_cast<WED_ATCWindRule *>(who)) != NULL))
		{
			AptWindRule_t exp;
			wind->Export(exp);
			if(exp.icao.empty())
				msg = "ATC wind rule '" + name + "' has a blank ICAO code for its METAR source.";
		}
		
		if(who->GetClass() == WED_TaxiRoute::sClass && ((taxi = dynamic_cast<WED_TaxiRoute *>(who)) != NULL))
		{
			// See bug http://dev.x-plane.com/bugbase/view.php?id=602 - blank names are okay!
//			if (name.empty() && !taxi->IsRunway())
//			{
//				msg = "This taxi route has no name.  All taxi routes must have a name so that ATC can give taxi instructions.";
//			}
			
			if(taxi->HasInvalidHotZones(s_legal_rwy_oneway))
			{
				msg = "The taxi route '" + name + "' has hot zones for runways not present at its airport.";
			}
			
			if(taxi->IsRunway())
			if(s_legal_rwy_twoway.count(taxi->GetRunway()) == 0)
			{
				msg = "The taxi route '" + name + "' is set to a runway not present at the airport.";
			}
			
			Point2	start, end;
			taxi->GetNthPoint(0)->GetLocation(gis_Geo, start);
			taxi->GetNthPoint(1)->GetLocation(gis_Geo, end);
			if(start == end)
			{
				#if CHECK_ZERO_LENGTH			
					msg = "The taxi route '" + name + "' is zero length.";
				#endif
			}
		}
		if(who->GetClass() == WED_ATCFrequency::sClass)
		{
			const WED_ATCFrequency * freq = dynamic_cast<WED_ATCFrequency *>(who);
			if(freq !=  NULL)
			{
				AptATCFreq_t freq_info;
				freq->Export(freq_info);
				const int freq_type = ENUM_Import(ATCFrequency, freq_info.atc_type);
				if(freq_type == atc_Delivery || freq_type == atc_Ground || freq_type == atc_Tower)
				{
					int mhz = freq_info.freq / 100;
					if(mhz < 118 || mhz > 136)
					{
						msg = "The ATC frequency " + name + " is illegal. (Clearance Delivery, Ground, and Tower frequencies must be between 118 and 136 MHz.)";
					}
				}
			}
		}
	}
	
	//------------------------------------------------------------------------------------
	// CHECKS FOR V10 APT.DAT FEATURES
	//------------------------------------------------------------------------------------	
	
	if(who->GetClass() == WED_RampPosition::sClass)
	{
		AptGate_t	g;
		WED_RampPosition * ramp = dynamic_cast<WED_RampPosition*>(who);
		DebugAssert(ramp);
		ramp->Export(g);
		
		if(gExportTarget == wet_xplane_900)
		if(g.equipment != 0)
		if(g.type != atc_ramp_misc || g.equipment != atc_traffic_all)
			msg = "Ramp starts with specific traffic and types are only suported in X-Plane 10 and newer.";
			
		if(g.equipment == 0)
			msg = "Ramp starts must have at least one valid type of equipment selected.";
		
		if(gExportTarget == wet_xplane_1050)
		{
			//Our flag to keep going until we find an error
			bool found_err = false;
			if(g.airlines == "" && !found_err)
			{
				//Error:"not really an error, we're just done here"
				found_err = true;
			}
			else if(g.airlines.length() < 3 && !found_err)
			{
				msg = "Ramp start airlines string " + g.airlines + " is not a group of three letters.";
				found_err = true;
			}
			
			//The number of spaces 
			int num_spaces = 0;
			
			if(!found_err)
			for(string::iterator itr = g.airlines.begin(); itr != g.airlines.end(); itr++)
			{
				char c = *itr;
				if(c == ' ')
				{
					num_spaces++;
				}
				else 
				{
					if(c < 'A' || c > 'Z')
					{
						msg = "Ramp start airlines string " + g.airlines + " contains non-uppercase letters.";
						found_err = true;
					}
				}
			}

			//The length of the string
			int wo_spaces_len = (g.airlines.length() - num_spaces);
			if(wo_spaces_len % 3 != 0 && !found_err)
			{
				msg = string("Ramp start airlines string " + g.airlines + " is not in groups of three letters.");
				found_err = true;
			}

			//ABC, num_spaces = 0 = ("ABC".length()/3) - 1
			//ABC DEF GHI, num_spaces = 2 = "ABCDEFGHI".length()/3 - 1
			//ABC DEF GHI JKL MNO PQR, num_spaces = 5 = "...".length()/3 - 1 
			if(num_spaces != (wo_spaces_len/3) - 1 && !found_err)
			{
				msg = string("Ramp start airlines string " + g.airlines + " is not spaced correctly.");
				found_err = true;
			}
		}
	}

	if(gExportTarget == wet_xplane_900)
	{
		if(who->GetClass() == WED_ATCFlow::sClass)
		{
			msg = "ATC flow information is only supported in x-Plane 10 and newer.";
		}
		if(who->GetClass() == WED_TaxiRoute::sClass)
		{
			msg = "ATC taxi routes are only supported in x-Plane 10 and newer.";
		}
	}
	
	if(who->GetClass() == WED_ATCRunwayUse::sClass)
	{
		WED_ATCRunwayUse * use = dynamic_cast<WED_ATCRunwayUse *>(who);
		AptRunwayRule_t urule;
		use->Export(urule);
		if(urule.operations == 0)
			msg = "ATC runway use must support at least one operation type.";
		else if(urule.equipment == 0)
			msg = "ATC runway use must support at least one equipment type.";
	}
	
	//------------------------------------------------------------------------------------
	// CHECKS FOR SUBMISSION TO GATEWAY
	//------------------------------------------------------------------------------------

	if(gExportTarget == wet_gateway)
	{
		if(who->GetClass() != WED_Group::sClass)
		if(WED_GetParentAirport(who) == NULL)
			msg = "You cannot export airport overlays to the X-Plane Airport Gateway if overlay elements are outside airports in the hierarchy.";

		if(who->GetClass() == WED_Airport::sClass)
		{
			WED_Airport * apt = dynamic_cast<WED_Airport *>(who);
			Bbox2 bounds;
			apt->GetBounds(gis_Geo, bounds);
			if(bounds.xspan() > MAX_LON_SPAN_GATEWAY ||
			   bounds.yspan() > MAX_LAT_SPAN_GATEWAY)
			{
				msg = "This airport is too big.  Perhaps a random part of the airport has been dragged to another part of the world?";
			}
		
		}

		#if !GATEWAY_IMPORT_FEATURES
		if(who->GetClass() == WED_AirportBoundary::sClass)
		{
			if(WED_HasBezierPol(dynamic_cast<WED_AirportBoundary*>(who)))
				msg = "Do not use bezier curves in airport boundaries.";
		}
		#endif

		if(who->GetClass() == WED_DrapedOrthophoto::sClass)
			msg = "Draped orthophotos are not allowed in the global airport database.";

		string res;
		if(GetThingResouce(who,res))
		{
			if(!lib_mgr->IsResourceDefault(res))
				msg = "The library path '" + res + "' is not part of X-Plane's default installation and cannot be submitted to the global airport database.";
			if(lib_mgr->IsResourceDeprecatedOrPrivate(res))
				msg = "The library path '" + res + "' is a deprecated or private X-Plane resource and cannot be used in global airports.";				
		}
	}

	//------------------------------------------------------------------------------------


	#if !FIND_BAD_AIRPORTS

	if (!msg.empty())
	{
		DoUserAlert(msg.c_str());
		return who;
	}

	#endif

	int nn = who->CountChildren();
	for (int n = 0; n < nn; ++n)
	{
		WED_Thing * fail = ValidateRecursive(who->GetNthChild(n), lib_mgr);
		#if FIND_BAD_AIRPORTS
			if(fail)
				msg = "Child has a bad part.";
		#else
			if (fail) return fail;
		#endif
	}
	
	if(who->GetClass() == WED_Airport::sClass)
	{
		if(s_used_hel.empty() && s_used_rwy.empty())
		{
			msg = "The airport '" + name + "' contains no runways, sea lanes, or helipads.";
			#if !FIND_BAD_AIRPORTS
			DoUserAlert(msg.c_str());
			return who;
			#endif
		}
		
		#if FIND_BAD_AIRPORTS
		if(!msg.empty())
		{
			WED_Airport * apt = dynamic_cast<WED_Airport *>(who);			
			string icao;
			apt->GetICAO(icao);
			printf("Airport '%s' invalid: %s\n", icao.c_str(), msg.c_str());
		}
		return NULL;
		#endif
	}
	
	#if FIND_BAD_AIRPORTS
		if(!msg.empty())
		{
			printf("     %s\n", msg.c_str());
		}
		return msg.empty() ? NULL : who;
	#else
		DebugAssert(msg.empty());
	#endif
	
	return NULL;
}

bool	WED_ValidateApt(IResolver * resolver, WED_Thing * wrl)
{
#if FIND_BAD_AIRPORTS
	string exp_target_str;
	switch(gExportTarget)
	{
		case wet_xplane_900:
			exp_target_str = "wet_xplane_900";
			break;
		case wet_xplane_1000:
			exp_target_str = "wet_xplane_1000";
			break;
		case wet_xplane_1021:
			exp_target_str = "wet_xplane_1021";
			break;
		case wet_xplane_1050:
			exp_target_str = "wet_xplane_1050";
			break;
		case wet_gateway:
			exp_target_str = "wet_gateway";
			break;
		default: 
			DebugAssert("Export target %s is unknown", exp_target_str.c_str());
	}
	
	printf("Export Target: %s\n", exp_target_str.c_str());
#endif

#if !GATEWAY_IMPORT_FEATURES
	string msg = "";
	if(WED_DoSelectZeroLength(resolver))
	{
		msg = "Your airport contains zero-length ATC routing lines. These should be deleted.";
#if !FIND_BAD_AIRPORTS
		DoUserAlert(msg.c_str());
		return false;
#else
		printf("%s\n", msg.c_str());
#endif
	}
	
	if(WED_DoSelectDoubles(resolver))
	{
		msg = "Your airport contains doubled ATC routing nodes. These should be merged.";
#if !FIND_BAD_AIRPORTS
		DoUserAlert(msg.c_str());
		return false;
#else
		printf("%s\n", msg.c_str());
#endif
	}
	
	if(WED_DoSelectCrossing(resolver))	
	{
		msg = "Your airport contains crossing ATC routing lines with no node at the crossing point.  Split the lines and join the nodes.";
#if !FIND_BAD_AIRPORTS
		DoUserAlert(msg.c_str());
		return false;
#else
		printf("%s\n", msg.c_str());
#endif
	}
#endif
	s_used_hel.clear();
	s_used_rwy.clear();
	s_flow_names.clear();
	s_legal_rwy_oneway.clear();
	s_icao.clear();
	if(wrl == NULL) wrl = WED_GetWorld(resolver);
	ISelection * sel = WED_GetSelect(resolver);
	
	WED_LibraryMgr * lib_mgr = 	WED_GetLibraryMgr(resolver);
	
	WED_Thing * bad_guy = ValidateRecursive(wrl, lib_mgr);
	if (bad_guy)
	{
		wrl->StartOperation("Select Invalid");
		sel->Select(bad_guy);
		wrl->CommitOperation();
		return false;
	}
	return true;
}
