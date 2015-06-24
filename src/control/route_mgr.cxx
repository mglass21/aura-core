// route_mgr.cxx - manage a route (i.e. a collection of waypoints)
//
// Written by Curtis Olson, started January 2004.
//            Norman Vine
//            Melchior FRANZ
//
// Copyright (C) 2004  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//


#include <math.h>
#include <stdlib.h>

#include "comms/display.h"
#include "comms/logging.h"
#include "comms/remote_link.h"
#include "include/globaldefs.h"
#include "main/globals.hxx"
//#include "mission/mission_mgr.hxx"
#include "props/props_io.hxx"
#include "sensors/gps_mgr.hxx"
#include "util/exception.hxx"
#include "util/sg_path.hxx"
#include "util/wind.hxx"

#include "waypoint.hxx"
#include "route_mgr.hxx"


FGRouteMgr::FGRouteMgr() :
    active( new SGRoute ),
    standby( new SGRoute ),
    config_props( NULL ),
    home_lon_node( NULL ),
    home_lat_node( NULL ),
    home_azimuth_node( NULL ),
    last_lon( 0.0 ),
    last_lat( 0.0 ),
    last_az( 0.0 ),
    bank_limit_node( NULL ),
    L1_period_node( NULL ),
    L1_damping_node( NULL ),
    xtrack_gain_node( NULL ),
    lon_node( NULL ),
    lat_node( NULL ),
    alt_node( NULL ),
    groundspeed_node( NULL ),
    groundtrack_node( NULL ),
    target_heading_error_deg( NULL ),
    target_course_deg( NULL ),
    ap_roll_node( NULL ),
    target_agl_node( NULL ),
    override_agl_node( NULL ),
    target_msl_node( NULL ),
    override_msl_node( NULL ),
    target_waypoint( NULL ),
    wp_dist_m( NULL ),
    wp_eta_sec( NULL ),
    xtrack_dist_m( NULL ),
    proj_dist_m( NULL ),
    start_mode( FIRST_WPT ),
    follow_mode( XTRACK_LEG_HDG ),
    completion_mode( LOOP ),
    dist_remaining_m( 0.0 )
{
}


FGRouteMgr::~FGRouteMgr() {
    delete standby;
    delete active;
}


// bind property nodes
void FGRouteMgr::bind() {
    home_lon_node = fgGetNode("/mission/home/longitude-deg", true );
    home_lat_node = fgGetNode("/mission/home/latitude-deg", true );
    home_azimuth_node = fgGetNode("/mission/home/azimuth-deg", true );

    bank_limit_node = fgGetNode("/config/fcs/autopilot/L1-controller/bank-limit-deg", true);
    L1_period_node = fgGetNode("/config/fcs/autopilot/L1-controller/period", true);
    L1_damping_node = fgGetNode("/config/fcs/autopilot/L1-controller/damping", true);

    // sanity check, set some conservative values if none are provided
    // in the autopilot config
    if ( bank_limit_node->getDoubleValue() < 0.1 ) {
	bank_limit_node->setDoubleValue( 20.0 );
    }
    if ( L1_period_node->getDoubleValue() < 0.1 ) {
	L1_period_node->setDoubleValue( 25.0 );
    }
    if ( L1_damping_node->getDoubleValue() < 0.1 ) {
	L1_damping_node->setDoubleValue( 0.7 );
    }

    xtrack_gain_node = fgGetNode( "/mission/route/xtrack-steer-gain", true );

    lon_node = fgGetNode( "/position/longitude-deg", true );
    lat_node = fgGetNode( "/position/latitude-deg", true );
    alt_node = fgGetNode( "/position/altitude-ft", true );
    groundspeed_node = fgGetNode("/velocity/groundspeed-ms", true);
    groundtrack_node = fgGetNode( "/orientation/groundtrack-deg", true );

    ap_roll_node = fgGetNode("/autopilot/settings/target-roll-deg", true);
    target_course_deg = fgGetNode( "/autopilot/settings/target-groundtrack-deg", true );
    target_msl_node = fgGetNode( "/autopilot/settings/target-msl-ft", true );
    override_msl_node
	= fgGetNode( "/autopilot/settings/override-msl-ft", true );
    target_agl_node = fgGetNode( "/autopilot/settings/target-agl-ft", true );
    override_agl_node
	= fgGetNode( "/autopilot/settings/override-agl-ft", true );
    target_waypoint
	= fgGetNode( "/mission/route/target-waypoint-idx", true );
    wp_dist_m = fgGetNode( "/mission/route/wp-dist-m", true );
    wp_eta_sec = fgGetNode( "/mission/route/wp-eta-sec", true );
    xtrack_dist_m = fgGetNode( "/mission/route/xtrack-dist-m", true );
    proj_dist_m = fgGetNode( "/mission/route/projected-dist-m", true );
}


void FGRouteMgr::init( SGPropertyNode *branch ) {
    config_props = branch;

    bind();

    active->clear();
    standby->clear();

    if ( config_props != NULL ) {
	if ( ! build() ) {
	    printf("Detected an internal inconsistency in the route\n");
	    printf(" configuration.  See earlier errors for\n" );
	    printf(" details.");
	    exit(-1);
	}

	// build() constructs the new route in the "standby" slot,
	// swap it to "active"
	swap();
    }
}


void FGRouteMgr::update() {
    double direct_course, direct_distance;
    double leg_course, leg_distance;

    double nav_course = 0.0;
    double nav_dist_m = 0.0;

    double target_agl_m = 0.0;
    double target_msl_m = 0.0;

    if ( active->size() > 0 ) {
	if ( GPS_age() < 10.0 ) {

	    // route start up logic: if start_mode == FIRST_WPT then
	    // there is nothing to do, we simply continue to track wpt
	    // 0 if that is the current waypoint.  If start_mode ==
	    // FIRST_LEG, then if we are tracking wpt 0, then
	    // increment it so we track the 2nd waypoint along the
	    // first leg.  If you have provided a 1 point route and
	    // request first_leg startup behavior, then don't do that
	    // again, force sane route parameters instead!
	    if ( (start_mode == FIRST_LEG)
		 && (active->get_waypoint_index() == 0) ) {
		if ( active->size() > 1 ) {
		    active->increment_current();
		} else {
		    start_mode = FIRST_WPT;
		    follow_mode = DIRECT;
		}
	    }

	    double L1_period = L1_period_node->getDoubleValue();
	    double L1_damping = L1_damping_node->getDoubleValue();
	    double gs_mps = groundspeed_node->getDoubleValue();

	    // track current waypoint of route (only if we have fresh gps data)
	    SGWayPoint prev = active->get_previous();
	    SGWayPoint wp = active->get_current();

	    // compute direct-to course and distance
	    wp.CourseAndDistance( lon_node->getDoubleValue(),
				  lat_node->getDoubleValue(),
				  alt_node->getDoubleValue(),
				  &direct_course, &direct_distance );

	    // compute leg course and distance
	    wp.CourseAndDistance( prev, &leg_course, &leg_distance );

	    // difference between ideal (leg) course and direct course
            double angle = leg_course - direct_course;
            if ( angle < -180.0 ) {
	        angle += 360.0;
            } else if ( angle > 180.0 ) {
	        angle -= 360.0;
            }

	    // compute cross-track error
	    double angle_rad = angle * SGD_DEGREES_TO_RADIANS;
            double xtrack_m = sin( angle_rad ) * direct_distance;
            double dist_m = cos( angle_rad ) * direct_distance;
	    /* printf("direct_dist = %.1f angle = %.1f dist_m = %.1f\n",
	              direct_distance, angle, dist_m); */
	    xtrack_dist_m->setDoubleValue( xtrack_m );
	    proj_dist_m->setDoubleValue( dist_m );

	    // compute cross-track steering compensation
	    double xtrack_gain = xtrack_gain_node->getDoubleValue();
	    double xtrack_comp = xtrack_m * xtrack_gain;
	    if ( xtrack_comp < -45.0 ) { xtrack_comp = -45.0; }
	    if ( xtrack_comp > 45.0 ) { xtrack_comp = 45.0; }

	    // default distance for waypoint acquisition, direct
	    // distance to the target waypoint.  This can be overrided
	    // later by leg following and replaced with distance
	    // remaining along the leg.
	    nav_dist_m = direct_distance;

	    if ( follow_mode == DIRECT ) {
		// direct to
		nav_course = direct_course;
	    } else if ( follow_mode == XTRACK_DIRECT_HDG ) {
		// cross track steering
		if ( (active->get_waypoint_index() == 0)
		     && (completion_mode != LOOP) ) {
		    // first waypoint is 'direct to' except for LOOP
		    // routes which track the leg connecting the last
		    // wpt to the first wpt.
		    nav_course = direct_course;
		} else if ( (active->get_waypoint_index() == active->size()-1)
			    && (completion_mode == EXTEND_LAST_LEG) ) {
		    // force leg heading logic on last leg so it is
		    // possible to extend the center line beyond the
		    // waypoint (if requested by completion mode.)
		    nav_course = leg_course - xtrack_comp;
		} else if ( fabs(angle) <= 45.0 ) {
		    // normal case, note: in this tracking mode,
		    // xtrack_course based on direct_course could
		    // start oscillating the closer we get to the
		    // target waypoint.
		    nav_course = direct_course - xtrack_comp;
		} else {
		    // navigate 'direct to' if off by more than the
		    // xtrack system can compensate for
		    nav_course = direct_course;
		}
	    } else if ( follow_mode == XTRACK_LEG_HDG ) {
		// cross track steering
		if ( (active->get_waypoint_index() == 0)
		     && (completion_mode != LOOP) ) {
		    // first waypoint is 'direct to' except for LOOP
		    // routes which track the leg connecting the last
		    // wpt to the first wpt.
		    nav_course = direct_course;
		} else if ( (active->get_waypoint_index() == active->size()-1)
			    && (completion_mode == EXTEND_LAST_LEG) ) {
		    // force leg heading logic on last leg so it is
		    // possible to extend the center line beyond the
		    // waypoint (if requested by completion mode.)
		    nav_dist_m = dist_m;
		    nav_course = leg_course - xtrack_comp;
		} else {
		    // normal case: xtrack_course based on leg_course,
		    // will have consistent stable regardless of
		    // distance from waypoint.
		    nav_dist_m = dist_m;
		    nav_course = leg_course - xtrack_comp;
		}
	    } else if ( follow_mode == LEADER ) {
		double L1_dist = (1.0 / SGD_PI) * L1_damping * L1_period * gs_mps;
		double wangle = 0.0;
		if ( L1_dist < 0.01 ) {
		    // ground speed <= 0.0 (problem?!?)
		    nav_course = direct_course;
		} else if ( L1_dist <= fabs(xtrack_m) ) {
		    // beyond L1 distance, steer as directly toward
		    // leg as allowed
		    wangle = 0.0;
		} else {
		    // steer towards imaginary point projected onto
		    // the route leg L1_distance ahead of us
		    wangle = acos(fabs(xtrack_m) / L1_dist) * SGD_RADIANS_TO_DEGREES;
		}
		if ( wangle < 30.0 ) {
		    wangle = 30.0;
		}
		if ( xtrack_m > 0.0 ) {
		    nav_course = direct_course + angle - 90.0 + wangle;
		} else {
		    nav_course = direct_course + angle + 90.0 - wangle;
		}
		if ( active->is_acquired() ) {
		    nav_dist_m = dist_m;
		} else {
		    // direct to first waypoint until we've acquired this route
		    nav_course = direct_course;
		    nav_dist_m = direct_distance;
		}

		// printf("direct=%.1f angle=%.1f nav=%.1f L1=%.1f xtrack=%.1f wangle=%.1f nav_dist=%.1f\n", direct_course, angle, nav_course, L1_dist, xtrack_m, wangle, nav_dist_m);
	    }

            if ( nav_course < 0.0 ) {
                nav_course += 360.0;
            } else if ( nav_course > 360.0 ) {
                nav_course -= 360.0;
            }

	    target_course_deg->setDoubleValue( nav_course );

	    // target bank angle computed here

	    double target_bank_deg = 0.0;

	    const double sqrt_of_2 = 1.41421356237309504880;
	    double omegaA = sqrt_of_2 * SGD_PI / L1_period;
	    double VomegaA = gs_mps * omegaA;
	    double course_error = groundtrack_node->getDoubleValue()
		- target_course_deg->getDoubleValue();
	    if ( course_error < -180.0 ) { course_error += 360.0; }
	    if ( course_error >  180.0 ) { course_error -= 360.0; }

	    double accel = 2.0 * sin(course_error * SG_DEGREES_TO_RADIANS) * VomegaA;
	    // double accel = 2.0 * gs_mps * gs_mps * sin(course_error * SG_DEGREES_TO_RADIANS) / L1;

	    static const double gravity = 9.81; // m/sec^2
	    double target_bank = -atan( accel / gravity );
	    target_bank_deg = target_bank * SG_RADIANS_TO_DEGREES;

	    double bank_limit_deg = bank_limit_node->getDoubleValue();
	    if ( target_bank_deg < -bank_limit_deg ) {
		target_bank_deg = -bank_limit_deg;
	    }
	    if ( target_bank_deg > bank_limit_deg ) {
		target_bank_deg = bank_limit_deg;
	    }
	    ap_roll_node->setDoubleValue( target_bank_deg );

	    target_agl_m = wp.get_target_agl_m();
	    target_msl_m = wp.get_target_alt_m();

	    // estimate distance remaining to completion of route
	    dist_remaining_m = nav_dist_m
		+ active->get_remaining_distance_from_current_waypoint();

#if 0
	    if ( display_on ) {
		printf("next leg: %.1f  to end: %.1f  wpt=%d of %d\n",
		       nav_dist_m, dist_remaining_m,
		       active->get_waypoint_index(), active->size());
	    }
#endif

	    // logic to mark completion of leg and move to next leg.
	    if ( completion_mode == LOOP ) {
		if ( nav_dist_m < 50.0 ) {
		    active->set_acquired( true );
		    active->increment_current();
		}
	    } else if ( completion_mode == CIRCLE_LAST_WPT ) {
		if ( nav_dist_m < 50.0 ) {
		    active->set_acquired( true );
		    if ( active->get_waypoint_index() < active->size() - 1 ) {
			active->increment_current();
		    } else {
			SGWayPoint wp = active->get_current();
			// FIXME: NEED TO GO TO CIRCLE MODE HERE SOME HOW!!!
			/*mission_mgr.request_task_circle(wp.get_target_lon(),
							wp.get_target_lat(),
							0.0, 0.0);*/
		    }
		}
	    } else if ( completion_mode == EXTEND_LAST_LEG ) {
		if ( nav_dist_m < 50.0 ) {
		    active->set_acquired( true );
		    if ( active->get_waypoint_index() < active->size() - 1 ) {
			active->increment_current();
		    } else {
			// follow the last leg forever
		    }
		}
	    }

	    // publish current target waypoint
	    target_waypoint->setIntValue( active->get_waypoint_index() );

	    // if ( display_on ) {
	    // printf("route dist = %0f\n", dist_remaining_m);
	    // }
	}
    } else {
        // FIXME: we've been commanded to follow a route, but no route
        // has been defined.

        // We are in ill-defined territory, should we do some sort of
        // circle of our home position?

	// FIXME: need to go to circle mode somehow here!!!!
	/* mission_mgr.request_task_circle(); */
    }

    wp_dist_m->setFloatValue( direct_distance );

    // update target altitude based on waypoint targets and possible
    // overrides ... preference is given to agl if both agl & msl are
    // set.
    double override_agl_ft = override_agl_node->getDoubleValue();
    double override_msl_ft = override_msl_node->getDoubleValue();
    if ( override_agl_ft > 1.0 ) {
	target_agl_node->setDoubleValue( override_agl_ft );
    } else if ( override_msl_ft > 1.0 ) {
	target_msl_node->setDoubleValue( override_msl_ft );
    } else if ( target_agl_m > 1.0 ) {
	target_agl_node->setDoubleValue( target_agl_m * SG_METER_TO_FEET );
    } else if ( target_msl_m > 1.0 ) {
	target_msl_node->setDoubleValue( target_msl_m * SG_METER_TO_FEET );
    }

    double gs_mps = groundspeed_node->getDoubleValue();
    if ( gs_mps > 0.1 ) {
	wp_eta_sec->setFloatValue( direct_distance / gs_mps );
    } else {
	wp_eta_sec->setFloatValue( 0.0 );
    }

#if 0
    if ( display_on ) {
	SGPropertyNode *ground_deg = fgGetNode("/orientation/groundtrack-deg", true);
	double gtd = ground_deg->getDoubleValue();
	if ( gtd < 0 ) { gtd += 360.0; }
	double diff = wp_course - gtd;
	if ( diff < -180.0 ) { diff += 360.0; }
	if ( diff > 180.0 ) { diff -= 360.0; }
	SGPropertyNode *psi = fgGetNode("/orientation/heading-deg", true);
	printf("true filt=%.1f true-wind-est=%.1f target-hd=%.1f\n",
	       psi->getDoubleValue(), true_deg, hd * SGD_RADIANS_TO_DEGREES);
	printf("gt cur=%.1f target=%.1f diff=%.1f\n", gtd, wp_course, diff);
	diff = hd*SGD_RADIANS_TO_DEGREES - true_deg;
	if ( diff < -180.0 ) { diff += 360.0; }
	if ( diff > 180.0 ) { diff -= 360.0; }
	printf("wnd: cur=%.1f target=%.1f diff=%.1f\n",
	       true_deg, hd * SGD_RADIANS_TO_DEGREES, diff);
    }
#endif
}


bool FGRouteMgr::swap() {
    if ( !standby->size() ) {
	// standby route is empty
	return false;
    }

    // swap standby <=> active routes
    SGRoute *tmp = active;
    active = standby;
    standby = tmp;

    // set target way point to the first waypoint in the new active route
    active->set_current( 0 );

    return true;
}


bool FGRouteMgr::build() {
    standby->clear();

    SGPropertyNode *node;
    int i;

    int count = config_props->nChildren();
    for ( i = 0; i < count; ++i ) {
        node = config_props->getChild(i);
        string name = node->getName();
        // cout << name << endl;
        if ( name == "wpt" ) {
            SGWayPoint wpt( node );
            standby->add_waypoint( wpt );
	} else if ( name == "enable" ) {
	    // happily ignore this
        } else {
            printf("Unknown top level section: %s\n", name.c_str() );
            return false;
        }
    }

    printf("loaded %d waypoints\n", standby->size());

    return true;
}


int FGRouteMgr::new_waypoint( const string& wpt_string )
{
    SGWayPoint wp = make_waypoint( wpt_string );
    standby->add_waypoint( wp );
    return 1;
}


int FGRouteMgr::new_waypoint( const double field1, const double field2,
			      const double alt,
			      const int mode )
{
    if ( mode == 0 ) {
        // relative waypoint
	SGWayPoint wp( field2, field1, -9999.0, alt, 0.0, 0.0,
		       SGWayPoint::RELATIVE, "" );
	standby->add_waypoint( wp );
    } else if ( mode == 1 ) {
	// absolute waypoint
	SGWayPoint wp( field1, field2, -9999.0, alt, 0.0, 0.0,
		       SGWayPoint::ABSOLUTE, "" );
	standby->add_waypoint( wp );
    }

    return 1;
}


SGWayPoint FGRouteMgr::make_waypoint( const string& wpt_string ) {
    string target = wpt_string;
    double lon = 0.0;
    double lat = 0.0;
    double alt_m = -9999.0;
    double agl_m = -9999.0;
    double speed_kt = 0.0;

    // WARNING: this routine doesn't have any way to handle AGL
    // altitudes.  Nor can it handle any offset heading/dist requests

    // extract altitude
    size_t pos = target.find( '@' );
    if ( pos != string::npos ) {
        alt_m = atof( target.c_str() + pos + 1 ) * SG_FEET_TO_METER;
        target = target.substr( 0, pos );
    }

    // check for lon,lat
    pos = target.find( ',' );
    if ( pos != string::npos ) {
        lon = atof( target.substr(0, pos).c_str());
        lat = atof( target.c_str() + pos + 1);
    }

    printf("Adding waypoint lon = %.6f lat = %.6f alt_m = %.0f\n",
           lon, lat, alt_m);
    SGWayPoint wp( lon, lat, alt_m, agl_m, speed_kt, 0.0,
                   SGWayPoint::ABSOLUTE, "" );

    return wp;
}


bool FGRouteMgr::reposition() {
    double home_lon = home_lon_node->getDoubleValue();
    double home_lat = home_lat_node->getDoubleValue();
    double home_az = home_azimuth_node->getDoubleValue();

    SGWayPoint wp(home_lon, home_lat);
    return reposition_pattern(wp, home_az);
}


bool FGRouteMgr::reposition_if_necessary() {
    double home_lon = home_lon_node->getDoubleValue();
    double home_lat = home_lat_node->getDoubleValue();
    double home_az = home_azimuth_node->getDoubleValue();

    if ( fabs(home_lon - last_lon) > 0.000001 ||
	 fabs(home_lat - last_lat) > 0.000001 ||
	 fabs(home_az - last_az) > 0.001 )
    {
	reposition();

	last_lon = home_lon;
	last_lat = home_lat;
	last_az = home_az;

	return true;
    }

    return false;
}


bool FGRouteMgr::reposition_pattern( const SGWayPoint &wp, const double hdg )
{
    // sanity check
    if ( fabs(wp.get_target_lon() > 0.0001)
	 || fabs(wp.get_target_lat() > 0.0001) )
    {
	// good location
	active->refresh_offset_positions( wp, hdg );
	if ( display_on ) {
	    printf( "ROUTE pattern updated: %.6f %.6f (course = %.1f)\n",
		    wp.get_target_lon(), wp.get_target_lat(),
		    hdg );
	}
	return true;
    } else {
	// bogus location, ignore ...
	return false;
    }
}
