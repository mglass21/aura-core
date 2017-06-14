#!/usr/bin/python

import argparse
import datetime
import os
import sys
import tempfile
from progress.bar import Bar

from props import root, getNode

sys.path.append("../src")
from comms.packet_id import *
import comms.packer

import commands
import current
import auraparser

m2nm   = 0.0005399568034557235 # meters to nautical miles

def logical_category(id):
    if id == GPS_PACKET_V1 or id == GPS_PACKET_V2 or id == GPS_PACKET_V3:
        return 'gps'
    elif id == IMU_PACKET_V1 or id == IMU_PACKET_V2 \
         or id == IMU_PACKET_V3:
        return 'imu'
    elif id == AIRDATA_PACKET_V3 or id == AIRDATA_PACKET_V4 \
         or id == AIRDATA_PACKET_V5:
        return 'air'
    elif id == FILTER_PACKET_V1 or id == FILTER_PACKET_V2 \
         or id == FILTER_PACKET_V3:
        return 'filter'
    elif id == ACTUATOR_PACKET_V1 or id == ACTUATOR_PACKET_V2:
        return 'act'
    elif id == PILOT_INPUT_PACKET_V1 \
         or id == PILOT_INPUT_PACKET_V2:
        return 'pilot'
    elif id == AP_STATUS_PACKET_V1 or id == AP_STATUS_PACKET_V2 \
         or id == AP_STATUS_PACKET_V3 or id == AP_STATUS_PACKET_V4 \
         or id == AP_STATUS_PACKET_V5:
        return 'ap'
    elif id == SYSTEM_HEALTH_PACKET_V2 \
         or id == SYSTEM_HEALTH_PACKET_V3 \
         or id == SYSTEM_HEALTH_PACKET_V4:
        return 'health'
    elif id == PAYLOAD_PACKET_V1 or id == PAYLOAD_PACKET_V2:
        return 'payload'
    elif id == RAVEN_PACKET_V1:
        return 'raven'
    elif id == EVENT_PACKET_V1:
        return 'event'
    else:
        return 'unknown-packet-id'

# When a binary record of some id is read, it gets parsed into the
# property tree structure.  The following code simple calls the
# appropriate text packer function for the given id to extract the
# same data back out of the property tree and format it as a text
# record.
def generate_record(category, index, delim=','):
    if category == 'gps':
        record = comms.packer.pack_gps_text(index, delim)
        return record
    elif category == 'imu':
        record = comms.packer.pack_imu_text(index, delim)
        return record
    elif category == 'air':
        record = comms.packer.pack_airdata_text(index, delim)
        return record
    elif category == 'filter':
        record = comms.packer.pack_filter_text(index, delim)
        return record
    elif category == 'act':
        record = comms.packer.pack_act_text(index, delim)
        return record
    elif category == 'pilot':
        record = comms.packer.pack_pilot_text(index, delim)
        return record
    elif category == 'ap':
        record = comms.packer.pack_ap_status_text(index, delim)
        return record
    elif category == 'health':
        record = comms.packer.pack_system_health_text(index, delim)
        return record
    elif category == 'payload':
        record = comms.packer.pack_payload_text(index, delim)
        return record
    elif category == 'raven':
        record = comms.packer.pack_raven_text(index, delim)
        return record
    elif category == 'event':
        record = comms.packer.pack_event_text(index, delim)
        return record
    # elif category == 'error':
    #     error_node = getNode("/autopilot/errors", True)
    #     data = [ '%.4f' % error_node.getFloat('timestamp'),
    #              '%.1f' % error_node.getFloat('roll_error') ]
    #     return delim.join(data)

argparser = argparse.ArgumentParser(description='aura export')
argparser.add_argument('--flight', help='load specified flight log')
argparser.add_argument('--skip-seconds', help='seconds to skip when processing flight log')

args = argparser.parse_args()

data = {}

gps_node = getNode('/sensors/gps', True)
located = False
lon = 0.0
lat = 0.0
sec = 0.0

if args.flight:
    if os.path.isdir(args.flight):
        filename = os.path.join(args.flight, 'flight.dat.gz')
    else:
        filename = args.flight
    print "filename:", filename
    if filename.endswith('.gz'):
        (fd, filetmp) = tempfile.mkstemp()
        command = 'zcat ' + filename + ' > ' + filetmp
        print command
        os.system(command)
        fd = open(filetmp, 'r')
    else:
        fd = open(filename, 'r')

    full = fd.read()

    if filename.endswith('.gz'):
        # remove temporary file name
        os.remove(filetmp)
        
    divs = 500
    size = len(full)
    chunk_size = size / divs
    threshold = chunk_size
    print 'len of decompressed file:', size

    bar = Bar('Parsing log file:', max = divs, suffix = '%(percent)d%% (%(eta)ds)')

    while True:
        try:
            (id, index, counter) = auraparser.file_read(full)
            if not located:
                if gps_node.getInt('satellites') >= 5:
                    lat = gps_node.getFloat('latitude_deg')
                    lon = gps_node.getFloat('longitude_deg')
                    sec = gps_node.getFloat('unix_time_sec')
                    located = True
            current.compute_derived_data()
            category = logical_category(id)
            record = generate_record(category, index)
            key = '%s-%d' % (category, index)
            # print 'key:', key
            if key in data:
                data[key].append(record)
            else:
                data[key] = [ record ]
            if counter > threshold:
                threshold += chunk_size
                bar.next()
        except:
            bar.finish()
            print 'end of file'
            break
else:
    print 'A flight log file must be provided'

output_dir = os.path.dirname(os.path.realpath(filename))

# last recorded time stamp
filter_node = getNode('/filters/filter', True)
status_node = getNode('/status', True)
total_time = filter_node.getFloat('timestamp')
apm2_node = getNode("/sensors/APM2", True)

for key in sorted(data):
    size = len(data[key])
    if total_time > 0.01:
        rate = size / total_time
    else:
        rate = 0.0
    print '%-10s %5.1f/sec (%7d records)' % (key, rate, size)
    filename = os.path.join(output_dir, key + ".txt")
    f = open(filename, 'w')
    for line in data[key]:
        f.write(line + '\n')

print
print "Total log time: %.1f min" % (total_time / 60.0)
print "Flight timer: %.1f min" % (status_node.getFloat('flight_timer') / 60.0)
print "Autopilot time: %.1f min" % (status_node.getFloat('local_autopilot_timer') / 60.0)
print "Distance flown: %.2f nm (%.2f km)" % (status_node.getFloat('flight_odometer')*m2nm, status_node.getFloat('flight_odometer')*0.001)
print "Battery Usage: %.0f mah" % apm2_node.getInt("extern_current_mah")
print

apikey = None
try:
    from os.path import expanduser
    home = expanduser("~")
    f = open(home + '/.forecastio')
    apikey = f.read().rstrip()
except:
    print "you must sign up for a free apikey at forecast.io and insert it as a single line inside a file called ~/.forecastio (with no other text in the file)"

if not apikey:
    print "Cannot lookup weather because no forecastio apikey found."
elif sec < 1:
    print "Cannot lookup weather because gps didn't report unix time."
else:
    print
    #utc = datetime.timezone(0)
    d = datetime.datetime.utcfromtimestamp(sec)
    print d.strftime("%Y-%m-%d-%H:%M:%S")

    url = 'https://api.darksky.net/forecast/' + apikey + '/%.8f,%.8f,%.d' % (lat, lon, sec)

    import urllib, json
    response = urllib.urlopen(url)
    data = json.loads(response.read())
    mph2kt = 0.868976
    mb2inhg = 0.0295299830714
    if 'currently' in data:
        currently = data['currently']
        #for key in currently:
        #    print key, ':', currently[key]
        if 'icon' in currently:
            icon = currently['icon']
            print 'Summary:', icon
        if 'temperature' in currently:
            tempF = currently['temperature']
            tempC = (tempF - 32.0) * 5 / 9
            print 'Temp:', '%.1f F' % tempF, '(%.1f C)' % tempC
        if 'dewPoint' in currently:
            tempF = currently['dewPoint']
            tempC = (tempF - 32.0) * 5 / 9
            print 'Dewpoint:', '%.1f F' % tempF, '(%.1f C)' % tempC
        if 'humidity' in currently:
            hum = currently['humidity']
            print 'Humidity:', '%.0f%%' % (hum * 100.0)
        if 'pressure' in currently:
            mbar = currently['pressure']
            inhg = mbar * mb2inhg
            print 'Pressure:', '%.2f inhg' % inhg, '(%.1f mbar)' % mbar
        if 'windSpeed' in currently:
            wind_mph = currently['windSpeed']
            wind_kts = wind_mph * mph2kt
        else:
            wind_mph = 0
            wind_kts = 0
        if 'windBearing' in currently:
            wind_deg = currently['windBearing']
        else:
            wind_deg = 0
        print "Wind %d deg @ %.1f kt (%.1f mph) @ " % (wind_deg, wind_kts, wind_mph, )
        if 'visibility' in currently:
            vis = currently['visibility']
            print 'Visibility:', '%.1f miles' % vis
        if 'cloudCover' in currently:
            cov = currently['cloudCover']
            print 'Cloud Cover:', '%.0f%%' % (cov * 100.0)
