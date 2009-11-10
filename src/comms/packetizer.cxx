#include <stdint.h>

#include <stdio.h>

#include "packetizer.hxx"


// initialize gps property nodes 
void UGPacketizer::bind_gps_nodes() {
    gps_timestamp_node = fgGetNode("/sensors/gps/time-stamp", true);
    gps_lat_node = fgGetNode("/sensors/gps/latitude-deg", true);
    gps_lon_node = fgGetNode("/sensors/gps/longitude-deg", true);
    gps_alt_node = fgGetNode("/sensors/gps/altitude-m", true);
    gps_ve_node = fgGetNode("/sensors/gps/ve-ms", true);
    gps_vn_node = fgGetNode("/sensors/gps/vn-ms", true);
    gps_vd_node = fgGetNode("/sensors/gps/vd-ms", true);
    gps_unix_sec_node = fgGetNode("/sensors/gps/unix-time-sec", true);
}

// initialize imu property nodes 
void UGPacketizer::bind_imu_nodes() {
    imu_timestamp_node = fgGetNode("/sensors/imu/timestamp", true);
    imu_p_node = fgGetNode("/sensors/imu/p-rad_sec", true);
    imu_q_node = fgGetNode("/sensors/imu/q-rad_sec", true);
    imu_r_node = fgGetNode("/sensors/imu/r-rad_sec", true);
    imu_ax_node = fgGetNode("/sensors/imu/ax-mps_sec", true);
    imu_ay_node = fgGetNode("/sensors/imu/ay-mps_sec", true);
    imu_az_node = fgGetNode("/sensors/imu/az-mps_sec", true);
    imu_hx_node = fgGetNode("/sensors/imu/hx", true);
    imu_hy_node = fgGetNode("/sensors/imu/hy", true);
    imu_hz_node = fgGetNode("/sensors/imu/hz", true);
}


UGPacketizer::UGPacketizer() {
    bind_gps_nodes();
    bind_imu_nodes();
}


int UGPacketizer::packetize_gps( uint8_t *buf ) {
    uint8_t *startbuf = buf;

    double time = gps_timestamp_node->getDoubleValue();
    *(double *)buf = time; buf += 8;

    double lat = gps_lat_node->getDoubleValue();
    *(double *)buf = lat; buf += 8;

    double lon = gps_lon_node->getDoubleValue();
    *(double *)buf = lon; buf += 8;

    /* resolution of 0.001 meters */
    int32_t alt = (int)(gps_alt_node->getDoubleValue() * 1000);
    *(int32_t *)buf = alt; buf += 4;

    /* +/- 327.67 mps (732.9 mph), resolution of 0.01 mps */
    int16_t vn = (int16_t)(gps_vn_node->getDoubleValue() * 100);
    *(int16_t *)buf = vn; buf += 2;

    int16_t ve = (int16_t)(gps_ve_node->getDoubleValue() * 100);
    *(int16_t *)buf = ve; buf += 2;

    int16_t vd = (int16_t)(gps_vd_node->getDoubleValue() * 100);
    *(int16_t *)buf = vd; buf += 2;
    
    double date = gps_unix_sec_node->getDoubleValue();
    *(double *)buf = date; buf += 8;

    uint8_t status = 0;
    *buf = status; buf++;

    return buf - startbuf;
}


void UGPacketizer::decode_gps( uint8_t *buf ) {
    double time = *(double *)buf; buf += 8;
    double lat = *(double *)buf; buf += 8;
    double lon = *(double *)buf; buf += 8;
    int32_t alt = *(int32_t *)buf; buf += 4;
    int16_t vn = *(int16_t *)buf; buf += 2;
    int16_t ve = *(int16_t *)buf; buf += 2;
    int16_t vd = *(int16_t *)buf; buf += 2;
    double date = *(double *)buf; buf += 8;
    uint8_t status = *(uint8_t *)buf; buf += 1;

    printf("t = %.2f (%.8f %.8f) a=%.2f  (%.2f %.2f %.2f) %.2f %d\n",
	   time, lat, lon, alt/1000.0, vn/100.0, ve/100.0, vd/100.0, date,
	   status);
}


struct imu {
   double time;
   double p,q,r;		/* angular velocities    */
   double ax,ay,az;		/* acceleration          */
   double hx,hy,hz;             /* magnetic field     	 */
   double Ps,Pt;                /* static/pitot pressure */
   // double Tx,Ty,Tz;          /* temperature           */
   double phi,the,psi;          /* attitudes             */
   uint64_t status;		/* error type		 */
};


int UGPacketizer::packetize_imu( uint8_t *buf ) {
    uint8_t *startbuf = buf;

    double time = imu_timestamp_node->getDoubleValue();
    *(double *)buf = time; buf += 8;

    int32_t p = (int)(imu_p_node->getDoubleValue() * 1000);
    *(int32_t *)buf = p; buf += 4;

    int32_t q = (int)(imu_q_node->getDoubleValue() * 1000);
    *(int32_t *)buf = q; buf += 4;

    int32_t r = (int)(imu_r_node->getDoubleValue() * 1000);
    *(int32_t *)buf = r; buf += 4;

    int32_t ax = (int)(imu_ax_node->getDoubleValue() * 1000);
    *(int32_t *)buf = ax; buf += 4;

    int32_t ay = (int)(imu_ay_node->getDoubleValue() * 1000);
    *(int32_t *)buf = ay; buf += 4;

    int32_t az = (int)(imu_az_node->getDoubleValue() * 1000);
    *(int32_t *)buf = az; buf += 4;

    int32_t hx = (int)(imu_hx_node->getDoubleValue() * 1000);
    *(int32_t *)buf = hx; buf += 4;

    int32_t hy = (int)(imu_hy_node->getDoubleValue() * 1000);
    *(int32_t *)buf = hy; buf += 4;

    int32_t hz = (int)(imu_hz_node->getDoubleValue() * 1000);
    *(int32_t *)buf = hz; buf += 4;

    uint8_t status = 0;
    *buf = status; buf++;

    return buf - startbuf;
}


void UGPacketizer::decode_imu( uint8_t *buf ) {
    double time = *(double *)buf; buf += 8;
    double p = *(int32_t *)buf; buf += 4;
    double q = *(int32_t *)buf; buf += 4;
    double r = *(int32_t *)buf; buf += 4;
    double ax = *(int32_t *)buf; buf += 4;
    double ay = *(int32_t *)buf; buf += 4;
    double az = *(int32_t *)buf; buf += 4;
    double hx = *(int32_t *)buf; buf += 4;
    double hy = *(int32_t *)buf; buf += 4;
    double hz = *(int32_t *)buf; buf += 4;
    uint8_t status = *(uint8_t *)buf; buf += 1;

    printf("t = %.2f (%.3f %.3f %.3f) (%.3f %.3f %.f) (%.3f %.3f %.3f) %d\n",
	   time,
	   p/1000.0, q/1000.0, r/1000.0,
	   ax/1000.0, ay/1000.0, az/1000.0,
	   hx/1000.0, hy/1000.0, hz/1000.0,
	   status );
}