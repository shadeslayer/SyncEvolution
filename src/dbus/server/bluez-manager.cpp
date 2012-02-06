/*
 * Copyright (C) 2011 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */


#include <syncevo/GLibSupport.h>

#include "bluez-manager.h"
#include "server.h"
#include "syncevo/SmartPtr.h"

#include <algorithm>

#include <boost/assign/list_of.hpp>

using namespace GDBusCXX;


SE_BEGIN_CXX

BluezManager::BluezManager(Server &server) :
    DBusRemoteObject(!strcmp(getEnv("DBUS_TEST_BLUETOOTH", ""), "none") ?
                     NULL : /* simulate missing Bluez */
                     GDBusCXX::dbus_get_bus_connection(!strcmp(getEnv("DBUS_TEST_BLUETOOTH", ""), "session") ?
                                                       "SESSION" : /* use our own Bluez stub */
                                                       "SYSTEM" /* use real Bluez */,
                                                       NULL, true, NULL),
                     "/", "org.bluez.Manager", "org.bluez",
                     true),
    m_server(server),
    m_adapterChanged(*this, "DefaultAdapterChanged")
{
    if (getConnection()) {
        m_done = false;
        DBusClientCall1<DBusObject_t> getAdapter(*this, "DefaultAdapter");
        getAdapter.start(boost::bind(&BluezManager::defaultAdapterCb, this, _1, _2 ));
        m_adapterChanged.activate(boost::bind(&BluezManager::defaultAdapterChanged, this, _1));
    } else {
        m_done = true;
    }
}

void BluezManager::defaultAdapterChanged(const DBusObject_t &adapter)
{
    m_done = false;
    //remove devices that belong to this original adapter
    if(m_adapter) {
        BOOST_FOREACH(boost::shared_ptr<BluezDevice> &device, m_adapter->getDevices()) {
            m_server.removeDevice(device->getMac());
        }
    }
    string error;
    defaultAdapterCb(adapter, error);
}

void BluezManager::defaultAdapterCb(const DBusObject_t &adapter, const string &error)
{
    if(!error.empty()) {
        SE_LOG_DEBUG (NULL, NULL, "Error in calling DefaultAdapter of Interface org.bluez.Manager: %s", error.c_str());
        m_done = true;
        return;
    }
    m_adapter.reset(new BluezAdapter(*this, adapter));
}

BluezManager::BluezAdapter::BluezAdapter(BluezManager &manager, const string &path) :
    DBusRemoteObject(manager.getConnection(),
                     path, "org.bluez.Adapter", "org.bluez"),
    m_manager(manager), m_devNo(0), m_devReplies(0),
    m_deviceRemoved(*this,  "DeviceRemoved"), m_deviceAdded(*this, "DeviceCreated")
{
    DBusClientCall1<std::vector<DBusObject_t> > listDevices(*this, "ListDevices");
    listDevices.start(boost::bind(&BluezAdapter::listDevicesCb, this, _1, _2));
    m_deviceRemoved.activate(boost::bind(&BluezAdapter::deviceRemoved, this, _1));
    m_deviceAdded.activate(boost::bind(&BluezAdapter::deviceCreated, this, _1));
}

void BluezManager::BluezAdapter::listDevicesCb(const std::vector<DBusObject_t> &devices, const string &error)
{
    if(!error.empty()) {
        SE_LOG_DEBUG (NULL, NULL, "Error in calling ListDevices of Interface org.bluez.Adapter: %s", error.c_str());
        checkDone(true);
        return;
    }
    m_devNo = devices.size();
    BOOST_FOREACH(const DBusObject_t &device, devices) {
        boost::shared_ptr<BluezDevice> bluezDevice(new BluezDevice(*this, device));
        m_devices.push_back(bluezDevice);
    }
    checkDone();
}

void BluezManager::BluezAdapter::deviceRemoved(const DBusObject_t &object)
{
    string address;
    std::vector<boost::shared_ptr<BluezDevice> >::iterator devIt;
    for(devIt = m_devices.begin(); devIt != m_devices.end(); ++devIt) {
        if(boost::equals((*devIt)->getPath(), object)) {
            address = (*devIt)->m_mac;
            if((*devIt)->m_reply) {
                m_devReplies--;
            }
            m_devNo--;
            m_devices.erase(devIt);
            break;
        }
    }
    m_manager.m_server.removeDevice(address);
}

void BluezManager::BluezAdapter::deviceCreated(const DBusObject_t &object)
{
    m_devNo++;
    boost::shared_ptr<BluezDevice> bluezDevice(new BluezDevice(*this, object));
    m_devices.push_back(bluezDevice);
}

BluezManager::BluezDevice::BluezDevice (BluezAdapter &adapter, const string &path) :
    GDBusCXX::DBusRemoteObject(adapter.m_manager.getConnection(),
                               path, "org.bluez.Device", "org.bluez"),
    m_adapter(adapter), m_reply(false), m_propertyChanged(*this, "PropertyChanged")
{
    DBusClientCall1<PropDict> getProperties(*this, "GetProperties");
    getProperties.start(boost::bind(&BluezDevice::getPropertiesCb, this, _1, _2));

    m_propertyChanged.activate(boost::bind(&BluezDevice::propertyChanged, this, _1, _2));
}

/**
 * check whether the current device has the PnP Information attribute.
 */
static bool hasPnpInfoService(const std::vector<std::string> &uuids)
{
    // The UUID that indicates the PnPInformation attribute is available.
    static const char * PNPINFOMATION_ATTRIBUTE_UUID = "00001200-0000-1000-8000-00805f9b34fb";

    // Note: GetProperties appears to return this list sorted which binary_search requires.
    if(std::binary_search(uuids.begin(), uuids.end(), PNPINFOMATION_ATTRIBUTE_UUID)) {
        return true;
    }

    return false;
}

void BluezManager::BluezDevice::checkSyncService(const std::vector<std::string> &uuids)
{
    static const char * SYNCML_CLIENT_UUID = "00000002-0000-1000-8000-0002ee000002";
    bool hasSyncService = false;
    Server &server = m_adapter.m_manager.m_server;
    BOOST_FOREACH(const string &uuid, uuids) {
        //if the device has sync service, add it to the device list
        if(boost::iequals(uuid, SYNCML_CLIENT_UUID)) {
            hasSyncService = true;
            if(!m_mac.empty()) {
                SyncConfig::DeviceDescription deviceDesc(m_mac, m_name,
                                                         SyncConfig::MATCH_FOR_SERVER_MODE);
                server.addDevice(deviceDesc);
                if(hasPnpInfoService(uuids)) {
                    DBusClientCall1<ServiceDict> discoverServices(*this,
                                                                  "DiscoverServices");
                    static const std::string PNP_INFO_UUID("0x1200");
                    discoverServices.start(PNP_INFO_UUID,
                                           boost::bind(&BluezDevice::discoverServicesCb,
                                                       this, _1, _2));
                }
            }
            break;
        }
    }
    // if sync service is not available now, possible to remove device
    if(!hasSyncService && !m_mac.empty()) {
        server.removeDevice(m_mac);
    }
}

/*
 * Parse the XML-formatted service record.
 */
bool extractValuefromServiceRecord(const std::string &serviceRecord,
                                   const std::string &attributeId,
                                   std::string &attributeValue)
{
    // Find atribute
    size_t pos  = serviceRecord.find(attributeId);

    // Only proceed if the attribute id was found.
    if(pos != std::string::npos) {
        pos = serviceRecord.find("value", pos + attributeId.size());
        pos = serviceRecord.find("\"", pos) + 1;
        int valLen = serviceRecord.find("\"", pos) - pos;
        attributeValue = serviceRecord.substr(pos, valLen);
        return true;
    }

    return false;
}

void BluezManager::loadBluetoothDeviceLookupTable()
{
    GError *err = NULL;
    string filePath(SyncEvolutionDataDir() + "/bluetooth_products.ini");
    if(!g_key_file_load_from_file(m_lookupTable.bt_key_file, filePath.c_str(),
                                  G_KEY_FILE_NONE, &err)) {
        SE_LOG_DEBUG(NULL, NULL, "%s[%d]: %s - filePath = %s, error = %s",
                     __FILE__, __LINE__, "Bluetooth products file not loaded",
                     filePath.c_str(), err->message);
        m_lookupTable.isLoaded = false;
    } else {
        m_lookupTable.isLoaded = true;
    }
}

/*
 * Get the names of the PnpInformation vendor and product from their
 * respective ids. At a minimum we need a matching vendor id for this
 * function to return true. If the product id is not found then we set
 * it to "", an empty string.
 */
bool BluezManager::getPnpInfoNamesFromValues(const std::string &vendorValue, std::string &vendorName,
                                             const std::string &productValue, std::string &productName)
{
    if(!m_lookupTable.bt_key_file) {
        // If this is the first invocation we then we need to start watching the loopup table.
        if (!m_watchedFile) {
            m_lookupTable.bt_key_file = g_key_file_new();
            string filePath(SyncEvolutionDataDir() + "/bluetooth_products.ini");
            m_watchedFile = boost::shared_ptr<SyncEvo::GLibNotify>(
                new GLibNotify(filePath.c_str(),
                               boost::bind(&BluezManager::loadBluetoothDeviceLookupTable, this)));
        }
        loadBluetoothDeviceLookupTable();
        // Make sure the file was actually loaded
        if(!m_lookupTable.isLoaded) {
            return false;
        }
    }

    const char *VENDOR_GROUP  = "Vendors";
    const char *PRODUCT_GROUP = "Products";

    GStringPtr vendor(g_key_file_get_string(m_lookupTable.bt_key_file, VENDOR_GROUP,
                                            vendorValue.c_str(), NULL));
    if(vendor) {
        vendorName = vendor.get();
    } else {
        // We at least need a vendor id match.
        return false;
    }

    GStringPtr product(g_key_file_get_string(m_lookupTable.bt_key_file, PRODUCT_GROUP,
                                             productValue.c_str(), NULL));
    if(product)  {
        productName = product.get();
    } else {
        // If the product is not in the look-up table, the product is
        // set to an empty string.
        productName = "";
    }

    return true;
}

void BluezManager::BluezDevice::discoverServicesCb(const ServiceDict &serviceDict,
                                                   const string &error)
{
    ServiceDict::const_iterator iter = serviceDict.begin();

    if(iter != serviceDict.end()) {
        std::string serviceRecord = (*iter).second;

        if(!serviceRecord.empty()) {
            static const std::string SOURCE_ATTRIBUTE_ID("0x0205");
            std::string sourceId;
            extractValuefromServiceRecord(serviceRecord, SOURCE_ATTRIBUTE_ID, sourceId);

            // A sourceId of 0x001 indicates that the vendor ID was
            // assigned by the Bluetooth SIG.
            // TODO: A sourceId of 0x002, means the vendor id was assigned by
            // the USB Implementor's forum. We do nothing in this case but
            // should do that look up as well.
            if(!boost::iequals(sourceId, "0x0001")) { return; }

            std::string vendorId, productId;
            static const std::string VENDOR_ATTRIBUTE_ID ("0x0201");
            static const std::string PRODUCT_ATTRIBUTE_ID("0x0202");
            extractValuefromServiceRecord(serviceRecord, VENDOR_ATTRIBUTE_ID,  vendorId);
            extractValuefromServiceRecord(serviceRecord, PRODUCT_ATTRIBUTE_ID, productId);

            std::string vendorName, productName;
            if (!m_adapter.m_manager.getPnpInfoNamesFromValues(vendorId,                   vendorName,
                                                               vendorId + "_" + productId, productName)) {
                return;
            }

            Server &server = m_adapter.m_manager.m_server;
            SyncConfig::DeviceDescription devDesc;
            if (server.getDevice(m_mac, devDesc)) {
                devDesc.m_pnpInformation =
                    boost::shared_ptr<SyncConfig::PnpInformation>(
                        new SyncConfig::PnpInformation(vendorName, productName));
                server.updateDevice(m_mac, devDesc);
            }
        }
    }
}

void BluezManager::BluezDevice::getPropertiesCb(const PropDict &props, const string &error)
{
    m_adapter.m_devReplies++;
    m_reply = true;
    if(!error.empty()) {
        SE_LOG_DEBUG (NULL, NULL, "Error in calling GetProperties of Interface org.bluez.Device: %s", error.c_str());
    } else {
        PropDict::const_iterator it = props.find("Name");
        if(it != props.end()) {
            m_name = boost::get<string>(it->second);
        }
        it = props.find("Address");
        if(it != props.end()) {
            m_mac = boost::get<string>(it->second);
        }

        PropDict::const_iterator uuids = props.find("UUIDs");
        if(uuids != props.end()) {
            const std::vector<std::string> uuidVec = boost::get<std::vector<std::string> >(uuids->second);
            checkSyncService(uuidVec);
        }
    }
    m_adapter.checkDone();
}

void BluezManager::BluezDevice::propertyChanged(const string &name,
                                                const boost::variant<vector<string>, string> &prop)
{
    Server &server = m_adapter.m_manager.m_server;
    if(boost::iequals(name, "Name")) {
        m_name = boost::get<std::string>(prop);
        SyncConfig::DeviceDescription device;
        if(server.getDevice(m_mac, device)) {
            device.m_deviceName = m_name;
            server.updateDevice(m_mac, device);
        }
    } else if(boost::iequals(name, "UUIDs")) {
        const std::vector<std::string> uuidVec = boost::get<std::vector<std::string> >(prop);
        checkSyncService(uuidVec);
    } else if(boost::iequals(name, "Address")) {
        string mac = boost::get<std::string>(prop);
        SyncConfig::DeviceDescription device;
        if(server.getDevice(m_mac, device)) {
            device.m_deviceId = mac;
            server.updateDevice(m_mac, device);
        }
        m_mac = mac;
    }
}

SE_END_CXX
