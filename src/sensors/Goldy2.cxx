//
// FILE: goldy2_imu.cxx
// DESCRIPTION: aquire live sensor data from a live running instance
// of Flightgear
//

#include "python/pyprops.hxx"

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <sys/time.h>  // settimeofday()

#include "comms/display.hxx"
#include "comms/logging.hxx"
#include "comms/netSocket.h"
#include "math/SGMath.hxx"
#include "math/SGGeodesy.hxx"
#include "util/timing.h"

#include "util_goldy2.hxx"
#include "Goldy2.hxx"


static netSocket sock;
static int port = 0;
static int gps_fix_value = 0;
static const int rcin_channels = 16;
static uint16_t rcin[rcin_channels];

static pyPropertyNode imu_node;
static pyPropertyNode gps_node;
static pyPropertyNode airdata_node;
static pyPropertyNode pilot_node;

static bool master_init = false;
static bool imu_inited = false;
static bool airdata_inited = false;
static bool gps_inited = false;
static bool pilot_input_inited = false;

struct imu_sensors_t {
    uint64_t time;
    uint16_t type;
    uint16_t valid;
    uint32_t imu_time_sync_in;
    float magX;
    float magY;
    float magZ;
    float accelX;
    float accelY;
    float accelZ;
    float gyroX;
    float gyroY;
    float gyroZ;
    float temp;
    float pressure;
    uint32_t att_time_sync_in;
    float yaw;
    float pitch;
    float roll;    
} imu_sensors;


// initialize goldy2_imu input property nodes
static void bind_input() {
    pyPropertyNode config = pyGetNode("/config/sensors/Goldy2", true);
    if ( config.hasChild("port") ) {
	port = config.getLong("port");
    }
    printf("Goldy2 port = %d\n", port);
}


// initialize imu output property nodes 
static void bind_imu_output( pyPropertyNode *base ) {
    if ( imu_inited ) {
        return;
    }
    imu_node = *base;

    imu_inited = true;
}


// initialize airdata output property nodes 
static void bind_airdata_output( pyPropertyNode *base ) {
    if ( airdata_inited ) {
	return;
    }
    airdata_node = *base;

    airdata_inited = true;
}


// initialize gps output property nodes
static void bind_gps_output( pyPropertyNode *base ) {
    if ( gps_inited ) {
        return;
    }
    gps_node = *base;

    gps_inited = true;
}

// initialize pilot output property nodes
static void bind_pilot_controls( pyPropertyNode *base ) {
    if ( pilot_input_inited ) {
        return;
    }
    pilot_node = *base;

    pilot_input_inited = true;
}


bool goldy2_init() {
    if ( master_init ) {
        return true;
    }

    bind_input();

    // open a UDP socket
    if ( ! sock.open( false ) ) {
	printf("Error opening goldy2 input socket\n");
	return false;
    }

    // bind ...
    if ( sock.bind( "", port ) == -1 ) {
	printf("error binding to port %d\n", port );
	return false;
    }

    // don't block waiting for input
    sock.setBlocking( false );

    master_init = true;

    return true;
}


// function prototypes
bool goldy2_imu_init( pyPropertyNode *base ) {
    if ( ! goldy2_init() ) {
        return false;
    }

    bind_imu_output( base );

    return true;
}


// function prototypes
bool goldy2_airdata_init( pyPropertyNode *base ) {
    if ( ! goldy2_init() ) {
        return false;
    }

    bind_airdata_output( base );

    return true;
}

bool goldy2_gps_init( pyPropertyNode *base  ) {
    if ( ! goldy2_init() ) {
        return false;
    }

    bind_gps_output( base );

    return true;
}

bool goldy2_pilot_init( pyPropertyNode *base ) {
    if ( ! goldy2_init() ) {
        return false;
    }

    bind_pilot_controls( base );

    return true;
}

// swap big/little endian bytes
static void my_swap( uint8_t *buf, int index, int count )
{
    return; // beaglebone is little endian and so is the ublox ...

    int i;
    uint8_t tmp;
    for ( i = 0; i < count / 2; ++i ) {
        tmp = buf[index+i];
        buf[index+i] = buf[index+count-i-1];
        buf[index+count-i-1] = tmp;
    }
}


static bool parse_ublox_msg( uint8_t msg_class, uint8_t msg_id,
			     uint16_t payload_length, uint8_t *payload )
{
    bool new_position = false;
    static bool set_system_time = false;

    if ( msg_class == 0x01 && msg_id == 0x02 ) {
	// NAV-POSLLH
	my_swap( payload, 0, 4);
	my_swap( payload, 4, 4);
	my_swap( payload, 8, 4);
	my_swap( payload, 12, 4);
	my_swap( payload, 16, 4);
	my_swap( payload, 20, 4);
	my_swap( payload, 24, 4);

	uint8_t *p = payload;
	uint32_t iTOW = *((uint32_t *)p+0);
	int32_t lon = *((int32_t *)(p+4));
	int32_t lat = *((int32_t *)(p+8));
	int32_t height = *((int32_t *)(p+12));
	int32_t hMSL = *((int32_t *)(p+16));
	// uint32_t hAcc = *((uint32_t *)(p+20));
	// uint32_t vAcc = *((uint32_t *)(p+24));
	if ( display_on && 0 ) {
	    if ( gps_fix_value < 3 ) {
		printf("nav-posllh (%d) %d %d %d %d\n",
		       iTOW, lon, lat, height, hMSL);
	    }
	}
    } else if ( msg_class == 0x01 && msg_id == 0x06 ) {
	// NAV-SOL
	my_swap( payload, 0, 4);
	my_swap( payload, 4, 4);
	my_swap( payload, 8, 2);
	my_swap( payload, 12, 4);
	my_swap( payload, 16, 4);
	my_swap( payload, 20, 4);
	my_swap( payload, 24, 4);
	my_swap( payload, 28, 4);
	my_swap( payload, 32, 4);
	my_swap( payload, 36, 4);
	my_swap( payload, 40, 4);
	my_swap( payload, 44, 2);

	uint8_t *p = payload;
	uint32_t iTOW = *((uint32_t *)(p+0));
	int32_t fTOW = *((int32_t *)(p+4));
	int16_t week = *((int16_t *)(p+8));
	uint8_t gpsFix = p[10];
	// uint8_t flags = p[11];
	int32_t ecefX = *((int32_t *)(p+12));
	int32_t ecefY = *((int32_t *)(p+16));
	int32_t ecefZ = *((int32_t *)(p+20));
	// uint32_t pAcc = *((uint32_t *)(p+24));
	int32_t ecefVX = *((int32_t *)(p+28));
	int32_t ecefVY = *((int32_t *)(p+32));
	int32_t ecefVZ = *((int32_t *)(p+36));
	// uint32_t sAcc = *((uint32_t *)(p+40));
	// uint16_t pDOP = *((uint16_t *)(p+44));
	uint8_t numSV = p[47];
	if ( display_on && 0 ) {
	    printf("nav-sol (%d) %d %d %d %d %d [ %d %d %d ]\n",
		   gpsFix, iTOW, fTOW, ecefX, ecefY, ecefZ,
		   ecefVX, ecefVY, ecefVZ);
	}
	SGVec3d ecef( ecefX / 100.0, ecefY / 100.0, ecefZ / 100.0 );
	SGGeod wgs84;
	SGGeodesy::SGCartToGeod( ecef, wgs84 );
	SGQuatd ecef2ned = SGQuatd::fromLonLat(wgs84);
	SGVec3d vel_ecef( ecefVX / 100.0, ecefVY / 100.0, ecefVZ / 100.0 );
	// SGVec3d vel_ecef = ecef2ned.backTransform(vel_ned);
	SGVec3d vel_ned = ecef2ned.transform( vel_ecef );
	if ( display_on && 0 ) {
	    printf("my vel ned = %.2f %.2f %.2f\n", vel_ned.x(), vel_ned.y(), vel_ned.z());
	    printf("wgs84 = %.10f, %.10f, %.2f\n",
                    wgs84.getLatitudeDeg(), wgs84.getLongitudeDeg(),
                    wgs84.getElevationM() );
 
	}

 	gps_node.setLong( "satellites", numSV );
 	gps_fix_value = gpsFix;
	if ( gps_fix_value == 0 ) {
	    gps_node.setLong( "status", 0 );
	} else if ( gps_fix_value == 1 || gps_fix_value == 2 ) {
	    gps_node.setLong( "status", 1 );
	} else if ( gps_fix_value == 3 ) {
	    gps_node.setLong( "status", 2 );
	}

	if ( fabs(ecefX) > 650000000
	     || fabs(ecefY) > 650000000
	     || fabs(ecefZ) > 650000000 ) {
	    // earth radius is about 6371km (637,100,000 cm).  If one
	    // of the ecef coordinates is beyond this radius we know
	    // we have bad data.  This means we won't toss data until
	    // above about 423,000' MSL
	    if ( event_log_on ) {
		event_log( "ublox", "received bogus ecef data" );
	    }
	} else if ( wgs84.getElevationM() > 60000 ) {
	    // sanity check: assume altitude > 60k meters (200k feet) is bad
	} else if ( wgs84.getElevationM() < -1000 ) {
	    // sanity check: assume altitude < -1000 meters (-3000 feet) is bad
	} else if ( gpsFix == 3 ) {
	    // passed basic sanity checks and gps is reporting a 3d fix
	    new_position = true;
	    gps_node.setDouble( "timestamp", get_Time() );
	    gps_node.setDouble( "latitude_deg", wgs84.getLatitudeDeg() );
	    gps_node.setDouble( "longitude_deg", wgs84.getLongitudeDeg() );
	    gps_node.setDouble( "altitude_m", wgs84.getElevationM() );
	    gps_node.setDouble( "vn_ms", vel_ned.x() );
	    gps_node.setDouble( "ve_ms", vel_ned.y() );
	    gps_node.setDouble( "vd_ms", vel_ned.z() );
	    // printf("        %.10f %.10f %.2f - %.2f %.2f %.2f\n",
	    //        wgs84.getLatitudeDeg(),
	    //        wgs84.getLongitudeDeg(),
	    //        wgs84.getElevationM(),
	    //        vel_ned.x(), vel_ned.y(), vel_ned.z() );

	    double julianDate = (week * 7.0) + 
		(0.001 * iTOW) / 86400.0 +  //86400 = seconds in 1 day
		2444244.5; // 2444244.5 Julian date of GPS epoch (Jan 5 1980 at midnight)
	    julianDate = julianDate - 2440587.5; // Subtract Julian Date of Unix Epoch (Jan 1 1970)

	    double unixSecs = julianDate * 86400.0;
	    // double unixFract = unixSecs - floor(unixSecs);
	    // struct timeval time;
	    gps_node.setDouble( "unix_time_sec", unixSecs );
#if 0
	    if ( unixSecs > 1263154775 && !set_system_time) {
		printf("Setting system time to %.3f\n", unixSecs);
		set_system_time = true;
		time.tv_sec = floor(unixSecs);
		time.tv_usec = floor(unixFract * 1000000.);
		settimeofday(&time, NULL);
	    }
#endif
	}
   } else if ( msg_class == 0x01 && msg_id == 0x12 ) {
	// NAV-VELNED
	my_swap( payload, 0, 4);
	my_swap( payload, 4, 4);
	my_swap( payload, 8, 4);
	my_swap( payload, 12, 4);
	my_swap( payload, 16, 4);
	my_swap( payload, 20, 4);
	my_swap( payload, 24, 4);
	my_swap( payload, 28, 4);
	my_swap( payload, 32, 4);

	uint8_t *p = payload;
	uint32_t iTOW = *((uint32_t *)p+0);
	int32_t velN = *((int32_t *)(p+4));
	int32_t velE = *((int32_t *)(p+8));
	int32_t velD = *((int32_t *)(p+12));
	uint32_t speed = *((uint32_t *)(p+16));
	// uint32_t gspeed = *((uint32_t *)(p+20));
	int32_t heading = *((int32_t *)(p+24));
	// uint32_t sAcc = *((uint32_t *)(p+28));
	// uint32_t cAcc = *((uint32_t *)(p+32));
	if ( display_on && 0 ) {
	    if ( gps_fix_value < 3 ) {
		printf("nav-velned (%d) %.2f %.2f %.2f s = %.2f h = %.2f\n",
		       iTOW, velN / 100.0, velE / 100.0, velD / 100.0,
		       speed / 100.0, heading / 100000.0);
	    }
	}
    } else if ( msg_class == 0x01 && msg_id == 0x21 ) {
	// NAV-TIMEUTC
	my_swap( payload, 0, 4);
	my_swap( payload, 4, 4);
	my_swap( payload, 8, 4);
	my_swap( payload, 12, 2);

	uint8_t *p = payload;
	uint32_t iTOW = *((uint32_t *)(p+0));
	// uint32_t tAcc = *((uint32_t *)(p+4));
	int32_t nano = *((int32_t *)(p+8));
	int16_t year = *((int16_t *)(p+12));
	uint8_t month = p[14];
	uint8_t day = p[15];
	uint8_t hour = p[16];
	uint8_t min = p[17];
	uint8_t sec = p[18];
	uint8_t valid = p[19];
	if ( display_on && 0 ) {
	    if ( gps_fix_value < 3 ) {
		printf("nav-timeutc (%d) %02x %04d/%02d/%02d %02d:%02d:%02d\n",
		       iTOW, valid, year, month, day, hour, min, sec);
	    }
	}
	if ( !set_system_time && year > 2009 ) {
	    set_system_time = true;
	    printf("set system clock: nav-timeutc (%d) %02x %04d/%02d/%02d %02d:%02d:%02d\n",
		   iTOW, valid, year, month, day, hour, min, sec);
	    struct tm gps_time;
	    gps_time.tm_sec = sec;
	    gps_time.tm_min = min;
	    gps_time.tm_hour = hour;
	    gps_time.tm_mday = day;
	    gps_time.tm_mon = month - 1;
	    gps_time.tm_year = year - 1900;
	    time_t unix_sec = mktime( &gps_time );
	    printf("gps->unix time = %d\n", (int)unix_sec);
	    struct timeval fulltime;
	    fulltime.tv_sec = unix_sec;
	    fulltime.tv_usec = nano / 1000;
	    settimeofday( &fulltime, NULL );
	}
    } else if ( msg_class == 0x01 && msg_id == 0x30 ) {
	// NAV-SVINFO (partial parse)
	my_swap( payload, 0, 4);

	uint8_t *p = payload;
	// uint32_t iTOW = *((uint32_t *)(p+0));
	uint8_t numCh = p[4];
	// uint8_t globalFlags = p[5];
	int satUsed = 0;
	for ( int i = 0; i < numCh; i++ ) {
	    // uint8_t satid = p[9 + 12*i];
	    // uint8_t flags = p[10 + 12*i];
	    uint8_t quality = p[11 + 12*i];
	    // printf(" chn=%d satid=%d flags=%d quality=%d\n", i, satid, flags, quality);
	    if ( quality > 3 ) {
		satUsed++;
	    }
	}
 	// gps_satellites_node.setLong( satUsed );
	if ( display_on && 0 ) {
	    if ( gps_fix_value < 3 ) {
		printf("Satellite count = %d/%d\n", satUsed, numCh);
	    }
	}
    } else {
	if ( display_on && 0 ) {
	    if ( gps_fix_value < 3 ) {
		printf("UBLOX msg class = %d  msg id = %d\n",
		       msg_class, msg_id);
	    }
	}
    }

    return new_position;
}


static bool scan_ublox(uint8_t *packet, int packet_len) {
    static int state = 0;
    static int msg_class = 0, msg_id = 0;
    static int length_lo = 0, length_hi = 0, payload_length = 0;
    static int counter = 0;
    static uint8_t cksum_A = 0, cksum_B = 0, cksum_lo = 0, cksum_hi = 0;
    static uint8_t payload[500];
    int pos = 0;

    // printf("read ublox, entry state = %d\n", state);

    bool new_position = false;

    if ( state == 0 ) {
	counter = 0;
	cksum_A = cksum_B = 0;
	while ( pos < packet_len && packet[pos] != 0xB5 ) {
	    // printf( "state0: len = %d val = %2X\n", len, packet[pos] );
	    pos += 1;
	}
	if ( pos < packet_len && packet[pos] == 0xB5 ) {
	    // printf( "read 0xB5\n");
	    state++;
	}
    }
    if ( state == 1 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    if ( packet[pos] == 0x62 ) {
		// printf( "read 0x62\n");
		state++;
	    } else if ( packet[pos] == 0xB5 ) {
		// printf( "read 0xB5\n");
	    } else {
		state = 0;
	    }
	}
    }
    if ( state == 2 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    msg_class = packet[pos];
	    cksum_A += packet[pos];
	    cksum_B += cksum_A;
	    // printf( "msg class = %d\n", msg_class );
	    state++;
	}
    }
    if ( state == 3 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    msg_id = packet[pos];
	    cksum_A += packet[pos];
	    cksum_B += cksum_A;
	    // printf( "msg id = %d\n", msg_id );
	    state++;
	}
    }
    if ( state == 4 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    length_lo = packet[pos];
	    cksum_A += packet[pos];
	    cksum_B += cksum_A;
	    state++;
	}
    }
    if ( state == 5 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    length_hi = packet[pos];
	    cksum_A += packet[pos];
	    cksum_B += cksum_A;
	    payload_length = length_hi*256 + length_lo;
	    // printf( "payload len = %d\n", payload_length );
	    if ( payload_length > 400 ) {
		state = 0;
	    } else {
		state++;
	    }
	}
    }
    if ( state == 6 ) {
	pos += 1;
	while ( pos < packet_len ) {
	    payload[counter++] = packet[pos];
	    // printf( "%02X ", packet[pos] );
	    cksum_A += packet[pos];
	    cksum_B += cksum_A;
	    if ( counter >= payload_length ) {
		break;
	    }
	    pos += 1;
	}

	if ( counter >= payload_length ) {
	    state++;
	    // printf( "\n" );
	}
    }
    if ( state == 7 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    cksum_lo = packet[pos];
	    state++;
	}
    }
    if ( state == 8 ) {
	pos += 1;
	if ( pos < packet_len ) {
	    cksum_hi = packet[pos];
	    if ( cksum_A == cksum_lo && cksum_B == cksum_hi ) {
		// printf( "checksum passes (%d)!\n", msg_id );
		new_position = parse_ublox_msg( msg_class, msg_id,
						payload_length, payload );
		state++;
	    } else {
		if ( display_on && 0 ) {
		    printf("checksum failed %d %d (computed) != %d %d (message)\n",
			   cksum_A, cksum_B, cksum_lo, cksum_hi );
		}
	    }
	    // this is the end of a record, reset state to 0 to start
	    // looking for next record
	    state = 0;
	}
    }

    return new_position;
}

// parse packets
bool goldy2_parse( uint8_t *buf, int size ) {
    if ( size < 8 ) {
        printf("goldy packet corruption!\n");
	return false;
    }
    // printf("  header = %c %c %c\n", buf[0], buf[1], buf[2]);
    // printf("  type id = 0x%02x size = %d\n", buf[3], size);
    int len = buf[4] + 256*buf[5];
    // printf("  package len = %d\n", len);
    uint16_t CRC = buf[6+len] + 256*buf[7+len];
    // printf("  CRC = %d\n", CRC);
    // printf("  Computed CRC = %d\n", utilCRC16(buf+3, len+3, 0));
    if ( CRC != utilCRC16(buf+3, len+3, 0) ) {
	printf("goldy packet CRC mismatch!\n");
	return false;
    }

    // check header
    if ( buf[0] != 'U' || buf[1] != 'M' || buf[2] != 'N' ) {
	printf("goldy packet header invalid\n");
	return false;
    }
    if ( buf[3] == 0x81 && len == 76 ) {
	// IMU packet
        uint8_t *payload = buf + 6;
	uint64_t time_ls = *(uint32_t *)payload; payload += 4;
	uint64_t time_ms = *(uint32_t *)payload; payload += 4;
	// printf("time1 = %llu time2 = %llu\n", time_ls, time_ms);
	imu_sensors.time = time_ls + 4294967295U * time_ms;
        // for ( int i = 0; i < 8; ++i ) {
	//     printf("%02x ", *(buf + 6 + i));
	// }
	// printf("\n");
	// printf("time = %llu\n", imu_sensors.time);
	imu_sensors.type = *(uint16_t *)payload; payload += 2;
	// printf("type = %d\n", imu_sensors.type);
	imu_sensors.valid = *(uint16_t *)payload; payload += 2;
	// printf("valid = %d\n", imu_sensors.valid);
	imu_sensors.imu_time_sync_in = *(uint32_t *)payload; payload += 4;
	// printf("imu sync time = %d\n", imu_sensors.imu_time_sync_in);
	imu_sensors.magX = *(float *)payload; payload += 4;
	imu_sensors.magY = *(float *)payload; payload += 4;
	imu_sensors.magZ = *(float *)payload; payload += 4;
	imu_sensors.accelX = *(float *)payload; payload += 4;
	imu_sensors.accelY = *(float *)payload; payload += 4;
	imu_sensors.accelZ = *(float *)payload; payload += 4;
	// printf("          accel = %.3f %.3f %.3f\n", imu_sensors.accelX, imu_sensors.accelY, imu_sensors.accelZ);
	imu_sensors.gyroX = *(float *)payload; payload += 4;
	imu_sensors.gyroY = *(float *)payload; payload += 4;
	imu_sensors.gyroZ = *(float *)payload; payload += 4;
	// printf("          gyro = %.3f %.3f %.3f\n", imu_sensors.gyroX, imu_sensors.gyroY, imu_sensors.gyroZ);
	imu_sensors.temp = *(float *)payload; payload += 4;
	imu_sensors.pressure = *(float *)payload; payload += 4;
	imu_sensors.att_time_sync_in = *(uint32_t *)payload; payload += 4;
	imu_sensors.yaw = *(float *)payload; payload += 4;
	imu_sensors.pitch = *(float *)payload; payload += 4;
	imu_sensors.roll = *(float *)payload; payload += 4;
    } else if ( buf[3] == 0x82 ) {
	// printf("GPS Packet len = %d\n", len);
        uint8_t *payload = buf + 6;
	scan_ublox(payload, len);
    } else if ( buf[3] == 0x85 && len == 32 ) {
        uint8_t *payload = buf + 6;
        for ( int i = 0; i < rcin_channels; i++ ) {
	    rcin[i] = *(uint16_t *)payload; payload += 2;
	    // printf(" %d", rcin[i]);
	}
        // printf("\n");

    } else {
	// printf("goldy unknown packet or wrong length.\n");
	return false;
    }

    return true;
}


// main input parsing function
bool goldy2_update() {
    const int goldy2_max_size = 2048;
    uint8_t packet_buf[goldy2_max_size];

    // printf("checking for packet ...\n");

    int result;
    while ( (result = sock.recv(packet_buf, goldy2_max_size, 0)) >= 0 ) {
        // printf("Read a packet, len = %d\n", result);
	goldy2_parse(packet_buf, result);
    }

    return true;
}


bool goldy2_imu_update() {
    static uint64_t last_imu_internal_time = 0;

    goldy2_update();

    if ( imu_inited ) {
        // do some stuff
    }

    bool fresh_data = false;

    if ( imu_sensors.time> last_imu_internal_time ) {
	fresh_data = true;
	last_imu_internal_time = imu_sensors.time;

	double cur_time = get_Time();
	imu_node.setDouble( "timestamp", cur_time );
	imu_node.setDouble( "p_rad_sec", imu_sensors.gyroX );
	imu_node.setDouble( "q_rad_sec", imu_sensors.gyroY );
	imu_node.setDouble( "r_rad_sec", imu_sensors.gyroZ );
	imu_node.setDouble( "ax_mps_sec", imu_sensors.accelX );
	imu_node.setDouble( "ay_mps_sec", imu_sensors.accelY );
	imu_node.setDouble( "az_mps_sec", imu_sensors.accelZ );
	imu_node.setDouble( "hx", imu_sensors.magX );
	imu_node.setDouble( "hy", imu_sensors.magY );
	imu_node.setDouble( "hz", imu_sensors.magZ );
	imu_node.setDouble( "tempC", imu_sensors.temp );
	imu_node.setDouble( "pressure", imu_sensors.pressure );
	imu_node.setDouble( "roll_deg", imu_sensors.roll );
	imu_node.setDouble( "pitch_deg", imu_sensors.pitch );
	imu_node.setDouble( "yaw_deg", imu_sensors.yaw );
    }

    return fresh_data;
}


bool goldy2_airdata_update() {
    bool fresh_data = false;

    static double last_time = 0.0;
    double cur_time = airdata_node.getDouble("timestamp");

    if ( cur_time > last_time ) {
	fresh_data = true;
    }

    last_time = cur_time;

    return fresh_data;
}


bool goldy2_gps_update() {
    static double last_timestamp = 0.0;
    double current_timestamp = gps_node.getDouble("timestamp");
    if ( current_timestamp > last_timestamp ) {
        last_timestamp = current_timestamp;
        return true;
    } else {
        return false;
    }
}

bool goldy2_pilot_update() {
    pilot_node.setDouble( "timestamp", get_Time() );
    pilot_node.setDouble( "aileron", (rcin[2] - 992.0) / 820.0);
    pilot_node.setDouble( "elevator", (rcin[3] - 992.0) / 820.0);
    pilot_node.setDouble( "throttle", (rcin[1] - 172.0) / 1640.0);
    pilot_node.setDouble( "rudder", (rcin[4] - 992.0) / 820.0);
    if ( rcin[0] < 992 ) {
        pilot_node.setBool("manual", false);
    } else {
        pilot_node.setBool("manual", true);
    }
    pilot_node.setLong("status", 1);
    return true;
}

void goldy2_imu_close() {
    sock.close();
}

void goldy2_airdata_close() {
}

void goldy2_gps_close() {
}

void goldy2_pilot_close() {
}
