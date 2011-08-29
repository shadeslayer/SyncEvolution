#! /usr/bin/python -u

# * Copyright (C) 2011 Intel Corporation

# This file is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 or 3.0 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

import dbus
import string
import sys

# Defines
SYNCML_UUID  = "00000002-0000-1000-8000-0002ee000002"
PNPINFO_UUID = "00001200-0000-1000-8000-00805f9b34fb"
PNPINFO_ATTRIB = "0x1200"
SOURCE_ATTRIB  = "0x0205"
VENDOR_ATTRIB  = "0x0201"
PRODUCT_ATTRIB = "0x0202"

def extractValueFromServiceRecord(servRec, attribId):

    pos = string.find(servRec, attribId)

    if(pos < 0):
        return ""

    pos = string.find(servRec, "value", pos + len(attribId))
    pos = string.find(servRec, '"', pos) + 1
    endPos = string.find(servRec,'"', pos)
    return servRec[pos:endPos]

def getVendorAndProductId(pnpInfoServRec):
    '''Get the vendor and product ids from xml formatted service record.'''

    servRec = pnpInfoServRec.values()[0]

    sourceVal  = extractValueFromServiceRecord(servRec, SOURCE_ATTRIB)
    vendorVal  = extractValueFromServiceRecord(servRec, VENDOR_ATTRIB)
    productVal = extractValueFromServiceRecord(servRec, PRODUCT_ATTRIB)
    return (sourceVal, vendorVal, productVal)

def writeDeviceInfoToFile(ids, vendor, product, hasSyncML):

    filename = "syncevo-phone-info-[%s].txt" % product
    FILE = open(filename,"w")
    FILE.write("Thanks, for helping us improve phone syncing on Linux.\n")
    FILE.write("Please send this file or its contents to blixtra [at] gmail.com\n\n" )
    FILE.write("SyncML support: %s\n" % hasSyncML)
    if(len(ids) > 0):
        FILE.write("Source: %s\n"     % (ids[0]))
        FILE.write("Vendor: %s=%s\n"  % (ids[1], vendor))
        FILE.write("product: %s=%s\n\n" % (ids[2], product))
    else:
        FILE.write("Vendor: %s\n"  % vendor)
        FILE.write("product: %s\n\n" % product)
        FILE.write("This phone doesn't support the bluetooth Device ID profile.\n" )

    FILE.close()
    return filename

# Start main program
bus = dbus.SystemBus()
bluezIface = dbus.Interface(bus.get_object('org.bluez', '/'),
                            'org.bluez.Manager')

hasSyncmlSupport  = False
hasPnpInfoSupport = False
ids = {}

adapters = bluezIface.ListAdapters()
for adapter in adapters:
    adapterIface = dbus.Interface(bus.get_object('org.bluez', adapter),
                                  'org.bluez.Adapter')
    devices = adapterIface.ListDevices()
    for device in devices:
        try:
            deviceIface = dbus.Interface(bus.get_object('org.bluez', device),
                                         'org.bluez.Device')
            props = deviceIface.GetProperties();
            uuids = props["UUIDs"]
            print "Device name:", props.get("Name", "???")
            print "MAC Address:", props.get("Address", "???")
            for uuid in uuids:
                if SYNCML_UUID == uuid:
                    hasSyncmlSupport = True
                    print "   Supports SyncML."
                if PNPINFO_UUID == uuid:
                    hasPnpInfoSupport = True
                    print "   Looking up device information..."
                    sys.stdout.flush()
                    serviceRecord = deviceIface.DiscoverServices(PNPINFO_ATTRIB)
                    ids = getVendorAndProductId(serviceRecord)

            vendor  = raw_input("   What company makes this phone? (examples: Nokia, Sony Ericsson), empty to skip: ")
            if vendor:
                product = raw_input("   What is the model of this phone? (example: N900, K750i), empty to skip: ")
                if product:
                    # Write the results to a file
                    filename = writeDeviceInfoToFile(ids, vendor, product, hasSyncmlSupport)
                    print "Thanks, please send the file %s to blixtra [at] gmail.com" % filename
        except dbus.exceptions.DBusException, ex:
            print "   Failed, skipping device: %s" % ex
