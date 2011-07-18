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

#ifndef BLUEZ_MANAGER_H
#define BLUEZ_MANAGER_H

#include <string>

#include "gdbus/gdbus-cxx-bridge.h"

#include <syncevo/declarations.h>

SE_BEGIN_CXX

class Server;

/**
 * Query bluetooth devices from org.bluez
 * The basic workflow is:
 * 1) get default adapter from bluez by calling 'DefaultAdapter' method of org.bluez.Manager
 * 2) get all devices of the adapter by calling 'ListDevices' method of org.bluez.Adapter
 * 3) iterate all devices and get properties for each one by calling 'GetProperties' method of org.bluez.Device.
 *    Then check its UUIDs whether it contains sync services and put it in the sync device list if it is
 *
 * To track changes of devices dynamically, here also listen signals from bluez:
 * org.bluez.Manager - DefaultAdapterChanged: default adapter is changed and thus have to get its devices
 *                                            and update sync device list
 * org.bluez.Adapter - DeviceCreated, DeviceRemoved: device is created or removed and device list is updated
 * org.bluez.Device - PropertyChanged: property is changed and device information is changed and tracked
 *
 * This class is to manage querying bluetooth devices from org.bluez. Also
 * it acts a proxy to org.bluez.Manager.
 */
class BluezManager : public GDBusCXX::DBusRemoteObject {
public:
    BluezManager(Server &server);

    virtual const char *getDestination() const {return "org.bluez";}
    virtual const char *getPath() const {return "/";}
    virtual const char *getInterface() const {return "org.bluez.Manager";}
    virtual DBusConnection *getConnection() const {return m_bluezConn.get();}
    bool isDone() { return m_done; }

private:
    class BluezDevice;

    /**
     * This class acts a proxy to org.bluez.Adapter.
     * Call methods of org.bluez.Adapter and listen signals from it
     * to get devices list and track its changes
     */
    class BluezAdapter: public GDBusCXX::DBusRemoteObject
    {
     public:
        BluezAdapter (BluezManager &manager, const std::string &path);

        virtual const char *getDestination() const {return "org.bluez";}
        virtual const char *getPath() const {return m_path.c_str();}
        virtual const char *getInterface() const {return "org.bluez.Adapter";}
        virtual DBusConnection *getConnection() const {return m_manager.getConnection();}
        void checkDone(bool forceDone = false)
        {
            if(forceDone || m_devReplies >= m_devNo) {
                m_devReplies = m_devNo = 0;
                m_manager.setDone(true);
            } else {
                m_manager.setDone(false);
            }
        }

        std::vector<boost::shared_ptr<BluezDevice> >& getDevices() { return m_devices; }

     private:
        /** callback of 'ListDevices' signal. Used to get all available devices of the adapter */
        void listDevicesCb(const std::vector<GDBusCXX::DBusObject_t> &devices, const std::string &error);

        /** callback of 'DeviceRemoved' signal. Used to track a device is removed */
        void deviceRemoved(const GDBusCXX::DBusObject_t &object);

        /** callback of 'DeviceCreated' signal. Used to track a new device is created */
        void deviceCreated(const GDBusCXX::DBusObject_t &object);

        BluezManager &m_manager;
        /** the object path of adapter */
        std::string m_path;
        /** the number of device for the default adapter */
        int m_devNo;
        /** the number of devices having reply */
        int m_devReplies;

        /** all available devices */
        std::vector<boost::shared_ptr<BluezDevice> > m_devices;

        /** represents 'DeviceRemoved' signal of org.bluez.Adapter*/
        GDBusCXX::SignalWatch1<GDBusCXX::DBusObject_t> m_deviceRemoved;
        /** represents 'DeviceAdded' signal of org.bluez.Adapter*/
        GDBusCXX::SignalWatch1<GDBusCXX::DBusObject_t> m_deviceAdded;

        friend class BluezDevice;
    };

    /**
     * This class acts a proxy to org.bluez.Device.
     * Call methods of org.bluez.Device and listen signals from it
     * to get properties of device and track its changes
     */
    class BluezDevice: public GDBusCXX::DBusRemoteObject
    {
     public:
        typedef std::map<std::string, boost::variant<std::vector<std::string>, std::string > > PropDict;

        BluezDevice (BluezAdapter &adapter, const std::string &path);

        virtual const char *getDestination() const {return "org.bluez";}
        virtual const char *getPath() const {return m_path.c_str();}
        virtual const char *getInterface() const {return "org.bluez.Device";}
        virtual DBusConnection *getConnection() const {return m_adapter.m_manager.getConnection();}
        std::string getMac() { return m_mac; }

        /**
         * check whether the current device has sync service
         * if yes, put it in the adapter's sync devices listn
         */
        void checkSyncService(const std::vector<std::string> &uuids);

     private:
        /** callback of 'GetProperties' method. The properties of the device is gotten */
        void getPropertiesCb(const PropDict &props, const std::string &error);

        /** callback of 'PropertyChanged' signal. Changed property is tracked */
        void propertyChanged(const std::string &name, const boost::variant<std::vector<std::string>, std::string> &prop);

        BluezAdapter &m_adapter;
        /** the object path of the device */
        std::string m_path;
        /** name of the device */
        std::string m_name;
        /** mac address of the device */
        std::string m_mac;
        /** whether the calling of 'GetProperties' is returned */
        bool m_reply;

        typedef GDBusCXX::SignalWatch2<std::string, boost::variant<std::vector<std::string>, std::string> > PropertySignal;
        /** represents 'PropertyChanged' signal of org.bluez.Device */
        PropertySignal m_propertyChanged;

        friend class BluezAdapter;
    };

    /*
     * check whether the data is generated. If errors, force initilization done
     */
    void setDone(bool done) { m_done = done; }

    /** callback of 'DefaultAdapter' method to get the default bluetooth adapter  */
    void defaultAdapterCb(const GDBusCXX::DBusObject_t &adapter, const std::string &error);

    /** callback of 'DefaultAdapterChanged' signal to track changes of the default adapter */
    void defaultAdapterChanged(const GDBusCXX::DBusObject_t &adapter);

    Server &m_server;
    GDBusCXX::DBusConnectionPtr m_bluezConn;
    boost::shared_ptr<BluezAdapter> m_adapter;

    /** represents 'DefaultAdapterChanged' signal of org.bluez.Adapter*/
    GDBusCXX::SignalWatch1<GDBusCXX::DBusObject_t> m_adapterChanged;

    /** flag to indicate whether the calls are all returned */
    bool m_done;
};

SE_END_CXX

#endif // BLUEZ_MANAGER_H
