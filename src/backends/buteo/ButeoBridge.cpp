/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "ButeoBridge.h"
SE_BEGIN_CXX

ButeoBridge::ButeoBridge(const QString &pluginName,
                         const Buteo::SyncProfile &profile,
                         Buteo::PluginCbInterface *cbInterface) :
    ClientPlugin(pluginName, profile, cbInterface)
{
}

bool ButeoBridge::startSync()
{
    return false;
}

bool ButeoBridge::init()
{
    return false;
}

bool ButeoBridge::uninit()
{
    return false;
}

void ButeoBridge::connectivityStateChanged(Sync::ConnectivityType type,
                                           bool state)
{
}

extern "C" ButeoBridge *createPlugin(const QString &pluginName,
                                     const Buteo::SyncProfile &profile,
                                     Buteo::PluginCbInterface *cbInterface)
{
   return new ButeoBridge(pluginName, profile, cbInterface);
}

extern "C" void destroyPlugin(ButeoBridge *client)
{
    delete client;
}

SE_END_CXX
