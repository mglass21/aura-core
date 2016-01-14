//
// FILE: fgfs_imu.cxx
// DESCRIPTION: aquire live sensor data from a live running instance
// of Flightgear
//

#include "python/pyprops.hxx"

#include <stdio.h>
#include <string>
#include <string.h>

#include "comms/netSocket.h"
#include "util/timing.h"

#include "imu_fgfs.hxx"


static netSocket sock;
static int port = 0;

// property nodes
static pyPropertyNode imu_node;
static pyPropertyNode airdata_node;
static pyPropertyNode act_node;
static pyPropertyNode apm2_node;

// static SGPropertyNode *configroot = NULL;
// static SGPropertyNode *outputroot = NULL;
// static SGPropertyNode *imu_port_node = NULL;

// static SGPropertyNode *imu_timestamp_node = NULL;
// static SGPropertyNode *imu_p_node = NULL;
// static SGPropertyNode *imu_q_node = NULL;
// static SGPropertyNode *imu_r_node = NULL;
// static SGPropertyNode *imu_ax_node = NULL;
// static SGPropertyNode *imu_ay_node = NULL;
// static SGPropertyNode *imu_az_node = NULL;
// static SGPropertyNode *imu_hx_node = NULL;
// static SGPropertyNode *imu_hy_node = NULL;
// static SGPropertyNode *imu_hz_node = NULL;

// static SGPropertyNode *airdata_timestamp_node = NULL;
// static SGPropertyNode *airdata_airspeed_node = NULL;
// static SGPropertyNode *airdata_pressure_node = NULL;

// static SGPropertyNode *imu_roll_truth_node = NULL;
// static SGPropertyNode *imu_pitch_truth_node = NULL;
// static SGPropertyNode *imu_yaw_truth_node = NULL;

// static SGPropertyNode *act_throttle_node = NULL;
// static SGPropertyNode *fake_extern_volts_node = NULL;
// static SGPropertyNode *fake_extern_amps_node = NULL;
// static SGPropertyNode *fake_extern_current_node = NULL;

static bool airdata_inited = false;


// initialize fgfs_imu input property nodes
static void bind_input( pyPropertyNode *config ) {
    if ( config->hasChild("port") ) {
	port = config->getLong("port");
    }
}


// initialize imu output property nodes 
static void bind_imu_output( pyPropertyNode *base ) {
    imu_node = *base;
    act_node = pyGetNode("/actuators/actuator", true);
    apm2_node = pyGetNode("/sensors/apm2", true);
    // set initial fake value
    apm2_node.setDouble( "board_vcc", 5.0 );

    // outputroot = pyGetNode( rootname.c_str(), true );

    // imu_timestamp_node = outputroot->getChild("time-stamp", 0, true);
    // imu_p_node = outputroot->getChild("p-rad_sec", 0, true);
    // imu_q_node = outputroot->getChild("q-rad_sec", 0, true);
    // imu_r_node = outputroot->getChild("r-rad_sec", 0, true);
    // imu_ax_node = outputroot->getChild("ax-mps_sec", 0, true);
    // imu_ay_node = outputroot->getChild("ay-mps_sec", 0, true);
    // imu_az_node = outputroot->getChild("az-mps_sec", 0, true);
    // imu_hx_node = outputroot->getChild("hx", 0, true);
    // imu_hy_node = outputroot->getChild("hy", 0, true);
    // imu_hz_node = outputroot->getChild("hz", 0, true);

    // imu_roll_truth_node = outputroot->getChild("roll-truth-deg", 0, true);
    // imu_pitch_truth_node = outputroot->getChild("pitch-truth-deg", 0, true);
    // imu_yaw_truth_node = outputroot->getChild("yaw-truth-deg", 0, true);
}


// initialize airdata output property nodes 
static void bind_airdata_output( pyPropertyNode *base ) {
    airdata_node = *base;
    
    // airdata_timestamp_node = outputroot->getChild("time-stamp", 0, true);
    // airdata_airspeed_node = outputroot->getChild("airspeed-kt", 0, true);
    // airdata_pressure_node = outputroot->getChild("pressure-mbar", 0, true);

    // act_throttle_node = pyGetNode("/actuators/actuator/channel", 2, true);
    // fake_extern_volts_node = pyGetNode("/sensors/APM2/extern-volt", true);
    // fake_extern_amps_node  = pyGetNode("/sensors/APM2/extern-amps", true);
    // fake_extern_current_node  = pyGetNode("/sensors/APM2/extern-current-mah", true);

    // set initial fake value
    airdata_node.setDouble( "temp_degC", 15.0 );

    airdata_inited = true;
}


// function prototypes
bool fgfs_imu_init( pyPropertyNode *base, pyPropertyNode *config ) {
    bind_input( config );
    bind_imu_output( base );

    // open a UDP socket
    if ( ! sock.open( false ) ) {
	printf("Error opening imu input socket\n");
	return false;
    }

    // bind ...
    if ( sock.bind( "", port ) == -1 ) {
	printf("error binding to port %d\n", port );
	return false;
    }

    // don't block waiting for input
    sock.setBlocking( false );

    return true;
}


// function prototypes
bool fgfs_airdata_init( pyPropertyNode *base ) {
    bind_airdata_output( base );

    return true;
}


// swap big/little endian bytes
static void my_swap( uint8_t *buf, int index, int count )
{
    int i;
    uint8_t tmp;
    for ( i = 0; i < count / 2; ++i ) {
        tmp = buf[index+i];
        buf[index+i] = buf[index+count-i-1];
        buf[index+count-i-1] = tmp;
    }
}


bool fgfs_imu_update() {
    const int fgfs_imu_size = 52;
    uint8_t packet_buf[fgfs_imu_size];

    bool fresh_data = false;

    int result;
    while ( (result = sock.recv(packet_buf, fgfs_imu_size, 0))
	    == fgfs_imu_size )
    {
	fresh_data = true;

	if ( ulIsLittleEndian ) {
	    my_swap( packet_buf, 0, 8 );
	    my_swap( packet_buf, 8, 4 );
	    my_swap( packet_buf, 12, 4 );
	    my_swap( packet_buf, 16, 4 );
	    my_swap( packet_buf, 20, 4 );
	    my_swap( packet_buf, 24, 4 );
	    my_swap( packet_buf, 28, 4 );
	    my_swap( packet_buf, 32, 4 );
	    my_swap( packet_buf, 36, 4 );
	    my_swap( packet_buf, 40, 4 );
	    my_swap( packet_buf, 44, 4 );
	    my_swap( packet_buf, 48, 4 );
	}

	uint8_t *buf = packet_buf;
	double time = *(double *)buf; buf += 8;
	float p = *(float *)buf; buf += 4;
	float q = *(float *)buf; buf += 4;
	float r = *(float *)buf; buf += 4;
	float ax = *(float *)buf; buf += 4;
	float ay = *(float *)buf; buf += 4;
	float az = *(float *)buf; buf += 4;
	float airspeed = *(float *)buf; buf += 4;
	float pressure = *(float *)buf; buf += 4;
	float roll_truth = *(float *)buf; buf += 4;
	float pitch_truth = *(float *)buf; buf += 4;
	float yaw_truth = *(float *)buf; buf += 4;

	double cur_time = get_Time();
	imu_node.setDouble( "timestamp", cur_time );
	imu_node.setDouble( "p_rad_sec", p );
	imu_node.setDouble( "q_rad_sec", q );
	imu_node.setDouble( "r_rad_sec", r );
	imu_node.setDouble( "ax_mps_sec", ax );
	imu_node.setDouble( "ay_mps_sec", ay );
	imu_node.setDouble( "az_mps_sec", az );
	imu_node.setDouble( "hx", 0.0 );
	imu_node.setDouble( "hy", 0.0 );
	imu_node.setDouble( "hz", 0.0 );
	imu_node.setDouble( "roll_truth_node", roll_truth );
	imu_node.setDouble( "pitch_truth_node", pitch_truth );
	imu_node.setDouble( "yaw_truth_node", yaw_truth );

	if ( airdata_inited ) {
	    airdata_node.setDouble( "timestamp", cur_time );
	    airdata_node.setDouble( "airspeed_kt", airspeed );
	    const double inhg2mbar = 33.8638866667;
	    airdata_node.setDouble( "pressure_mbar", pressure * inhg2mbar );

	    // fake volt/amp values here for no better place to do it
	    static double last_time = cur_time;
	    static double mah = 0.0;
	    double thr = act_node.getDouble("channel[2]");
	    apm2_node.setDouble("extern_volt", 16.0 - thr);
	    apm2_node.setDouble("extern_amps", thr * 12.0);
	    double dt = cur_time - last_time;
	    mah += thr*12.0 * (1000.0/3600.0) * dt;
	    last_time = cur_time;
	    apm2_node.setDouble( "extern_current_mah", mah );
	}
    }

    return fresh_data;
}


bool fgfs_airdata_update() {
    bool fresh_data = false;

    static double last_time = 0.0;
    double cur_time = airdata_node.getDouble("timestamp");

    if ( cur_time > last_time ) {
	fresh_data = true;
    }

    last_time = cur_time;

    return fresh_data;
}


void fgfs_imu_close() {
    sock.close();
}
