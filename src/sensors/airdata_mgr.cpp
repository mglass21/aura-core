/**
 * \file: airdata_mgr.cpp
 *
 * Front end management interface for reading air data.
 *
 * Copyright (C) 2009 - Curtis L. Olson curtolson@gmail.com
 *
 * $Id: airdata_mgr.cpp,v 1.1 2009/05/01 11:40:48 curt Exp $
 */


#include <math.h>
#include <string.h>

#include "globaldefs.h"

#include "adns/mnav/ahrs.h"
#include "comms/logging.h"
#include "props/props.hxx"

#include "mnav.h"

#include "airdata_mgr.h"

//
// Global variables
//

static float Ps_filt = 0.0;
static float Pt_filt = 0.0;
static float true_alt_m = 0.0;
static float ground_alt_press = 0.0;
static float climb_filt = 0.0;
static float accel_filt = 0.0;

// air data property nodes
static SGPropertyNode *Ps_node = NULL;
static SGPropertyNode *Pt_node = NULL;
static SGPropertyNode *Ps_filt_node = NULL;
static SGPropertyNode *Pt_filt_node = NULL;
static SGPropertyNode *pressure_error_m_node = NULL;
static SGPropertyNode *true_alt_ft_node = NULL;
static SGPropertyNode *agl_alt_ft_node = NULL;
static SGPropertyNode *vert_fps_node = NULL;
static SGPropertyNode *forward_accel_node = NULL;
static SGPropertyNode *ground_alt_press_m_node = NULL;



void AirData_init() {
    // initialize air data property nodes
    Ps_node = fgGetNode("/sensors/air-data/Ps-m", true);
    Pt_node = fgGetNode("/sensors/air-data/Pt-ms", true);
    Ps_filt_node = fgGetNode("/position/altitude-pressure-m", true);
    Pt_filt_node = fgGetNode("/velocities/airspeed-kt", true);
    true_alt_ft_node = fgGetNode("/position/altitude-ft",true);
    agl_alt_ft_node = fgGetNode("/position/altitude-agl-ft", true);
    pressure_error_m_node = fgGetNode("/position/pressure-error-m", true);
    vert_fps_node = fgGetNode("/velocities/pressure-vertical-speed-fps",true);
    forward_accel_node = fgGetNode("/accelerations/airspeed-ktps",true);
    ground_alt_press_m_node
        = fgGetNode("/position/ground-altitude-pressure-m", true);

    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/sensors", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "air-data" ) {
	    string source = section->getChild("source")->getStringValue();
	    string basename = "/sensors/";
	    basename += section->getDisplayName();
	    printf("i = %d  name = %s source = %s %s\n",
		   i, name.c_str(), source.c_str(), basename.c_str());
	    if ( source == "mnav" ) {
		// nop
	    } else {
		printf("Unknown air data source = '%s' in config file\n",
		       source.c_str());
	    }
	}
    }
}


static void do_pressure_helpers( struct imu *data ) {
    static float Ps_filt_last = 0.0;
    static float Pt_filt_last = 0.0;
    static double t_last = 0.0;

    // Do a simple first order low pass filter to reduce noise
    Ps_filt = 0.97 * Ps_filt + 0.03 * data->Ps;
    Pt_filt = 0.97 * Pt_filt + 0.03 * data->Pt;

    // best guess at true altitude
    true_alt_m = Ps_filt + pressure_error_m_node->getDoubleValue();

    // compute rate of climb based on pressure altitude change
    float climb = (Ps_filt - Ps_filt_last) / (data->time - t_last);
    Ps_filt_last = Ps_filt;
    climb_filt = 0.97 * climb_filt + 0.03 * climb;

    // compute a forward acceleration value based on pitot speed
    // change
    float accel = (Pt_filt - Pt_filt_last) / (data->time - t_last);
    Pt_filt_last = Pt_filt;
    accel_filt = 0.97 * accel_filt + 0.03 * accel;

    // determine ground reference altitude.  Average pressure
    // altitude over first 30 seconds unit is powered on.  This
    // assumes that system clock time starts counting at zero.
    // Watch out if we ever find a way to set the system clock to
    // real time.
    static bool first_time = true;
    if ( first_time ) { 
	ground_alt_press = data->Ps;
	first_time = false;
    }
    if ( data->time < 30.0 ) {
	float dt = data->time - t_last;
	float elapsed = data->time - dt;
	ground_alt_press
	    = (elapsed * ground_alt_press + dt * data->Ps)
	    / data->time;
	ground_alt_press_m_node->setDoubleValue( ground_alt_press );
    }

    t_last = data->time;

    // printf("Ps = %.1f nav = %.1f bld = %.1f vsi = %.2f\n",
    //        Ps_filt, navpacket.alt, true_alt_m, climb_filt);
}


bool AirData_update() {
    bool fresh_data = false;

    struct imu imupacket;

    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/sensors", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "air-data" ) {
	    string source = section->getChild("source")->getStringValue();
	    string basename = "/sensors/";
	    basename += section->getDisplayName();
	    // printf("i = %d  name = %s source = %s %s\n",
	    //	   i, name.c_str(), source.c_str(), basename.c_str());
	    if ( source == "mnav" ) {
		fresh_data = mnav_get_airdata(&imupacket);
	    } else {
		printf("Unknown air data source = '%s' in config file\n",
		       source.c_str());
	    }
	}
    }

    if ( fresh_data ) {
	do_pressure_helpers( &imupacket );

	// publish values to property tree
	Ps_node->setDoubleValue( imupacket.Ps );
	Pt_node->setDoubleValue( imupacket.Pt );
	Ps_filt_node->setDoubleValue( Ps_filt );
	Pt_filt_node->setDoubleValue( Pt_filt * SG_MPS_TO_KT );
	true_alt_ft_node->setDoubleValue( true_alt_m * SG_METER_TO_FEET );
	agl_alt_ft_node->setDoubleValue( (Ps_filt - ground_alt_press)
					* SG_METER_TO_FEET );
	vert_fps_node->setDoubleValue( climb_filt * SG_METER_TO_FEET );
	forward_accel_node->setDoubleValue( accel_filt * SG_MPS_TO_KT );

	// if ( console_link_on ) {
	//     console_link_imu( &imupacket );
	// }

	// if ( log_to_file ) {
	//     log_imu( &imupacket );
	// }
    }

    return fresh_data;
}


void AirData_close() {
    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/sensors", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "air-data" ) {
	    string source = section->getChild("source")->getStringValue();
	    string basename = "/sensors/";
	    basename += section->getDisplayName();
	    // printf("i = %d  name = %s source = %s %s\n",
	    //	   i, name.c_str(), source.c_str(), basename.c_str());
	    if ( source == "mnav" ) {
		// nop
	    } else {
		printf("Unknown air data source = '%s' in config file\n",
		       source.c_str());
	    }
	}
    }
}