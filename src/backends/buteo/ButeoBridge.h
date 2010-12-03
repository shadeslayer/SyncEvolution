/*
 * Copyright (C) 2010 Intel Corporation
 */

#ifndef INCL_SYNCEVO_BUTEOBRIDGE
#define INCL_SYNCEVO_BUTEOBRIDGE

#include <config.h>

#include <libsyncpluginmgr/ClientPlugin.h>

#include <syncevo/declarations.h>
namespace SyncEvo {

class ButeoBridge : public Buteo::ClientPlugin
{
    Q_OBJECT;

 public:
    ButeoBridge(const QString &pluginName,
                 const Buteo::SyncProfile &profile,
                 Buteo::PluginCbInterface *cbInterface);

    // implementation of ClientPlugin interface
    virtual bool startSync();
    virtual bool init();
    virtual bool uninit();

public slots:
    virtual void connectivityStateChanged(Sync::ConnectivityType type,
                                          bool state);

 private:
    /** config name to be used by sync, set in init() */
    std::string m_config;
};

extern "C" ButeoBridge *createPlugin(const QString &pluginName,
                                     const Buteo::SyncProfile &profile,
                                     Buteo::PluginCbInterface *cbInterface);
extern "C" void destroyPlugin(ButeoBridge *client);

} // namespace SyncEvo

#endif // INCL_SYNCEVO_BUTEOBRIDGE
