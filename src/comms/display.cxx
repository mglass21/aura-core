#include "python/pyprops.hxx"

#include <sys/types.h>		// opendir() mkdir()
#include <dirent.h>		// opendir()
#include <stdio.h>		// sscanf()
#include <stdlib.h>		// random()
#include <string.h>		// strncmp()
#include <sys/stat.h>		// mkdir()
#include <sys/time.h>
#include <zlib.h>

#include "include/globaldefs.h"

#include "sensors/gps_mgr.hxx"
#include "util/timing.h"

#include "display.h"

bool display_on = false;   // dump summary to display periodically

// property nodes
static bool props_inited = false;

static pyPropertyNode imu_node;
static pyPropertyNode gps_node;
static pyPropertyNode airdata_node;
static pyPropertyNode filter_node;
static pyPropertyNode orient_node;
static pyPropertyNode pos_pressure_node;
static pyPropertyNode act_node;
static pyPropertyNode remote_link_node;
static pyPropertyNode route_node;
static pyPropertyNode status_node;
static pyPropertyNode apm2_node;


static void init_props() {
    props_inited = true;

    // initialize property nodes
    imu_node = pyGetNode("/sensors/imu", true);
    gps_node = pyGetNode("/sensors/gps", true);
    airdata_node = pyGetNode("/sensors/airdata", true);
    filter_node = pyGetNode("/filters/filter", true);
    orient_node = pyGetNode("/orientation", true);
    pos_pressure_node = pyGetNode("/position/pressure", true);
    act_node = pyGetNode("/actuators/actuator", true);
    remote_link_node = pyGetNode("/comms/remote_link", true);
    route_node = pyGetNode("/task/route", true);
    status_node = pyGetNode("/status", true);
    apm2_node = pyGetNode("/sensors/APM2", true);
}


// periodic console summary of attitude/location estimate
void display_message()
{
    if ( !props_inited ) {
	init_props();
    }

    printf("[m/s^2]:ax  = %6.3f ay  = %6.3f az  = %6.3f \n",
	   imu_node.getDouble("ax_mps_sec"),
	   imu_node.getDouble("ay_mps_sec"),
	   imu_node.getDouble("az_mps_sec"));
    printf("[deg/s]:p   = %6.3f q   = %6.3f r   = %6.3f \n",
	   imu_node.getDouble("p_rad_sec") * SGD_RADIANS_TO_DEGREES,
	   imu_node.getDouble("q_rad_sec") * SGD_RADIANS_TO_DEGREES,
	   imu_node.getDouble("r_rad_sec") * SGD_RADIANS_TO_DEGREES);
    printf("[Gauss]:hx  = %6.3f hy  = %6.3f hz  = %6.3f \n",
	   imu_node.getDouble("hx"),
	   imu_node.getDouble("hy"),
	   imu_node.getDouble("hz"));
    printf("[deg  ]:phi = %6.2f the = %6.2f psi = %6.2f \n",
	   orient_node.getDouble("roll_deg"),
	   orient_node.getDouble("pitch_deg"),
	   orient_node.getDouble("heading_deg"));
    printf("[     ]:Palt  = %6.3f Pspd  = %6.3f             \n",
	   pos_pressure_node.getDouble("altitude_m"),
	   airdata_node.getDouble("airspeed_kt"));
#if 0
    // gyro bias from mnav filter
    printf("[deg/s]:bp  = %6.3f,bq  = %6.3f,br  = %6.3f \n",
	   xs[4] * SGD_RADIANS_TO_DEGREES,
	   xs[5] * SGD_RADIANS_TO_DEGREES,
	   xs[6] * SGD_RADIANS_TO_DEGREES);
#endif

    if ( GPS_age() < 10.0 ) {
	time_t current_time = gps_node.getLong("unix_time_sec");
	double remainder = gps_node.getDouble("unix_time_sec") - current_time;
	struct tm *date = gmtime(&current_time);
        printf("[GPS  ]:date = %04d/%02d/%02d %02d:%02d:%05.2f\n",
	       date->tm_year + 1900, date->tm_mon + 1, date->tm_mday,
	       date->tm_hour, date->tm_min, date->tm_sec + remainder);
        printf("[GPS  ]:lon = %f[deg], lat = %f[deg], alt = %f[m], age = %.2f\n",
	       gps_node.getDouble("longitude_deg"),
	       gps_node.getDouble("latitude_deg"),
	       gps_node.getDouble("altitude_m"), GPS_age());
    } else {
	printf("[GPS  ]:[%0f seconds old]\n", GPS_age());
    }

    if ( filter_node.getString("navigation") == "valid" ) {
        printf("[filter]:lon = %f[deg], lat = %f[deg], alt = %f[m]\n",
	       filter_node.getDouble("longitude_deg"),
	       filter_node.getDouble("latitude_deg"),
	       filter_node.getDouble("altitude_m"));	
    } else {
	printf("[filter]:[No Valid Data]\n");
    }

    printf("[act  ]: %.2f %.2f %.2f %.2f %.2f\n",
	   act_node.getDouble("channel[0]"),
	   act_node.getDouble("channel[1]"),
	   act_node.getDouble("channel[2]"),
	   act_node.getDouble("channel[3]"),
	   act_node.getDouble("channel[4]"));
    printf("[health]: cmdseq = %ld  tgtwp = %ld  loadavg = %.2f  vcc = %.2f\n",
           remote_link_node.getLong("sequence_num"),
	   route_node.getLong("target_waypoint_idx"),
           status_node.getDouble("system_load_avg"),
	   apm2_node.getDouble("board_vcc"));
    printf("\n");

    // printf("imu size = %d\n", sizeof( struct imu ) );
}