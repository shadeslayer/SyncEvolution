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

#include "read-operations.h"
#include "dbus-user-interface.h"
#include "server.h"
#include "dbus-sync.h"

SE_BEGIN_CXX

ReadOperations::ReadOperations(const std::string &config_name, Server &server) :
    m_configName(config_name), m_server(server)
{}

void ReadOperations::getConfigs(bool getTemplates, std::vector<std::string> &configNames)
{
    if (getTemplates) {
        SyncConfig::DeviceList devices;

        // get device list from dbus server, currently only bluetooth devices
        m_server.getDeviceList(devices);

        // also include server templates in search
        devices.push_back(SyncConfig::DeviceDescription("", "", SyncConfig::MATCH_FOR_CLIENT_MODE));

        //clear existing templates in dbus server
        m_server.clearPeerTempls();

        SyncConfig::TemplateList list = SyncConfig::getPeerTemplates(devices);
        std::map<std::string, int> numbers;
        BOOST_FOREACH(const boost::shared_ptr<SyncConfig::TemplateDescription> peer, list) {
            //if it is not a template for device
            if(peer->m_deviceName.empty()) {
                configNames.push_back(peer->m_templateId);
            } else {
                string templName = "Bluetooth_";
                templName += peer->m_deviceId;
                templName += "_";
                std::map<std::string, int>::iterator it = numbers.find(peer->m_deviceId);
                if(it == numbers.end()) {
                    numbers.insert(std::make_pair(peer->m_deviceId, 1));
                    templName += "1";
                } else {
                    it->second++;
                    stringstream seq;
                    seq << it->second;
                    templName += seq.str();
                }
                configNames.push_back(templName);
                m_server.addPeerTempl(templName, peer);
            }
        }
    } else {
        SyncConfig::ConfigList list = SyncConfig::getConfigs();
        BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server, list) {
            configNames.push_back(server.first);
        }
    }
}

boost::shared_ptr<SyncConfig> ReadOperations::getLocalConfig(const string &configName, bool mustExist)
{
    string peer, context;
    SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(configName),
                                  peer, context);

    boost::shared_ptr<SyncConfig> syncConfig(new SyncConfig(configName));

    /** if config was not set temporarily */
    if (!setFilters(*syncConfig)) {
        // the default configuration can always be opened for reading,
        // everything else must exist
        if ((context != "default" || peer != "") &&
            mustExist &&
            !syncConfig->exists()) {
            SE_THROW_EXCEPTION(NoSuchConfig, "No configuration '" + configName + "' found");
        }
    }
    return syncConfig;
}

void ReadOperations::getConfig(bool getTemplate,
                               Config_t &config)
{
    getNamedConfig(m_configName, getTemplate, config);
}

void ReadOperations::getNamedConfig(const std::string &configName,
                                    bool getTemplate,
                                    Config_t &config)
{
    map<string, string> localConfigs;
    boost::shared_ptr<SyncConfig> dbusConfig;
    SyncConfig *syncConfig;
    string syncURL;
    /** get server template */
    if(getTemplate) {
        string peer, context;

        boost::shared_ptr<SyncConfig::TemplateDescription> peerTemplate =
            m_server.getPeerTempl(configName);
        if(peerTemplate) {
            SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(peerTemplate->m_templateId),
                    peer, context);
            dbusConfig = SyncConfig::createPeerTemplate(peerTemplate->m_path);
            // if we have cached template information, add match information for it
            localConfigs.insert(pair<string, string>("description", peerTemplate->m_description));

            stringstream score;
            score << peerTemplate->m_rank;
            localConfigs.insert(pair<string, string>("score", score.str()));
            // Actually this fingerprint is transferred by getConfigs, which refers to device name
            localConfigs.insert(pair<string, string>("deviceName", peerTemplate->m_deviceName));
            // This is the reliable device info obtained from the bluetooth
            // device id profile (DIP) or emtpy if DIP not supported.
            if (!peerTemplate->m_hardwareName.empty()) {
                localConfigs.insert(pair<string, string>("hardwareName", peerTemplate->m_hardwareName));
            }
            // This is the fingerprint of the template
            localConfigs.insert(pair<string, string>("fingerPrint", peerTemplate->m_matchedModel));
            // This is the template name presented to UI (or device class)
            if (!peerTemplate->m_templateName.empty()) {
                localConfigs.insert(pair<string,string>("templateName", peerTemplate->m_templateName));
            }

            // if the peer is client, then replace syncURL with bluetooth
            // MAC address
            syncURL = "obex-bt://";
            syncURL += peerTemplate->m_deviceId;
        } else {
            SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(configName),
                    peer, context);
            dbusConfig = SyncConfig::createPeerTemplate(peer);
        }

        if(!dbusConfig.get()) {
            SE_THROW_EXCEPTION(NoSuchConfig, "No template '" + configName + "' found");
        }

        // use the shared properties from the right context as filter
        // so that the returned template preserves existing properties
        boost::shared_ptr<SyncConfig> shared = getLocalConfig(string("@") + context, false);

        ConfigProps props;
        shared->getProperties()->readProperties(props);
        dbusConfig->setConfigFilter(true, "", props);
        BOOST_FOREACH(std::string source, shared->getSyncSources()) {
            SyncSourceNodes nodes = shared->getSyncSourceNodes(source, "");
            props.clear();
            nodes.getProperties()->readProperties(props);
            // Special case "type" property: the value in the context
            // is not preserved. Every new peer must ensure that
            // its own value is compatible (= same backend) with
            // the other peers.
            props.erase("type");
            dbusConfig->setConfigFilter(false, source, props);
        }
        syncConfig = dbusConfig.get();
    } else {
        DBusUserInterface ui;
        dbusConfig = getLocalConfig(configName);
        //try to check password and read password from gnome keyring if possible
        ConfigPropertyRegistry& registry = SyncConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, registry) {
            prop->checkPassword(ui, configName, *dbusConfig->getProperties());
        }
        list<string> configuredSources = dbusConfig->getSyncSources();
        BOOST_FOREACH(const string &sourceName, configuredSources) {
            ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
            SyncSourceNodes sourceNodes = dbusConfig->getSyncSourceNodes(sourceName);

            BOOST_FOREACH(const ConfigProperty *prop, registry) {
                prop->checkPassword(ui, configName, *dbusConfig->getProperties(),
                        sourceName, sourceNodes.getProperties());
            }
        }
        syncConfig = dbusConfig.get();
    }

    /** get sync properties and their values */
    ConfigPropertyRegistry &syncRegistry = SyncConfig::getRegistry();
    BOOST_FOREACH(const ConfigProperty *prop, syncRegistry) {
        InitStateString value = prop->getProperty(*syncConfig->getProperties());
        if (boost::iequals(prop->getMainName(), "syncURL") && !syncURL.empty() ) {
            localConfigs.insert(pair<string, string>(prop->getMainName(), syncURL));
        } else if (value.wasSet()) {
            localConfigs.insert(pair<string, string>(prop->getMainName(), value));
        }
    }

    // Set ConsumerReady for existing SyncEvolution < 1.2 configs
    // if not set explicitly,
    // because in older releases all existing configurations where
    // shown. SyncEvolution 1.2 is more strict and assumes that
    // ConsumerReady must be set explicitly. The sync-ui always has
    // set the flag for configs created or modified with it, but the
    // command line did not. Matches similar code in the Cmdline.cpp
    // migration code.
    //
    // This does not apply to templates which always have ConsumerReady
    // set explicitly (to on or off) or not set (same as off).
    if (!getTemplate &&
        syncConfig->getConfigVersion(CONFIG_LEVEL_PEER, CONFIG_CUR_VERSION) == 0 /* SyncEvolution < 1.2 */) {
        localConfigs.insert(make_pair("ConsumerReady", "1"));
    }

    // insert 'configName' of the chosen config (configName is not normalized)
    localConfigs.insert(pair<string, string>("configName", syncConfig->getConfigName()));

    config.insert(pair<string,map<string, string> >("", localConfigs));

    /* get configurations from sources */
    list<string> sources = syncConfig->getSyncSources();
    BOOST_FOREACH(const string &name, sources) {
        localConfigs.clear();
        SyncSourceNodes sourceNodes = syncConfig->getSyncSourceNodes(name);
        ConfigPropertyRegistry &sourceRegistry = SyncSourceConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, sourceRegistry) {
            InitStateString value = prop->getProperty(*sourceNodes.getProperties());
            if (value.wasSet()) {
                localConfigs.insert(pair<string, string>(prop->getMainName(), value));
            }
        }
        config.insert(pair<string, map<string, string> >( "source/" + name, localConfigs));
    }
}

void ReadOperations::getReports(uint32_t start, uint32_t count,
                                Reports_t &reports)
{
    SyncContext client(m_configName, false);
    std::vector<string> dirs;
    client.getSessions(dirs);

    uint32_t index = 0;
    // newest report firstly
    for( int i = dirs.size() - 1; i >= 0; --i) {
        /** if start plus count is bigger than actual size, then return actual - size reports */
        if(index >= start && index - start < count) {
            const string &dir = dirs[i];
            std::map<string, string> aReport;
            // insert a 'dir' as an ID for the current report
            aReport.insert(pair<string, string>("dir", dir));
            SyncReport report;
            // peerName is also extracted from the dir
            string peerName = client.readSessionInfo(dir,report);
            boost::shared_ptr<SyncConfig> config(new SyncConfig(m_configName));
            string storedPeerName = config->getPeerName();
            //if can't find peer name, use the peer name from the log dir
            if(!storedPeerName.empty()) {
                peerName = storedPeerName;
            }

            /** serialize report to ConfigProps and then copy them to reports */
            HashFileConfigNode node("/dev/null","",true);
            node << report;
            ConfigProps props;
            node.readProperties(props);

            BOOST_FOREACH(const ConfigProps::value_type &entry, props) {
                aReport.insert(entry);
            }
            // a new key-value pair <"peer", [peer name]> is transferred
            aReport.insert(pair<string, string>("peer", peerName));
            reports.push_back(aReport);
        }
        index++;
    }
}

void ReadOperations::checkSource(const std::string &sourceName)
{
    boost::shared_ptr<SyncConfig> config(new SyncConfig(m_configName));
    setFilters(*config);

    list<std::string> sourceNames = config->getSyncSources();
    list<std::string>::iterator it;
    for(it = sourceNames.begin(); it != sourceNames.end(); ++it) {
        if(*it == sourceName) {
            break;
        }
    }
    if(it == sourceNames.end()) {
        SE_THROW_EXCEPTION(NoSuchSource, "'" + m_configName + "' has no '" + sourceName + "' source");
    }
    bool checked = false;
    try {
        // this can already throw exceptions when the config is invalid
        SyncSourceParams params(sourceName, config->getSyncSourceNodes(sourceName), config);
        auto_ptr<SyncSource> syncSource(SyncSource::createSource(params, false, config.get()));

        if (syncSource.get()) {
            syncSource->open();
            // success!
            checked = true;
        }
    } catch (...) {
        Exception::handle();
    }

    if (!checked) {
        SE_THROW_EXCEPTION(SourceUnusable, "The source '" + sourceName + "' is not usable");
    }
}
void ReadOperations::getDatabases(const string &sourceName, SourceDatabases_t &databases)
{
    boost::shared_ptr<SyncConfig> config(new SyncConfig(m_configName));
    setFilters(*config);

    SyncSourceParams params(sourceName, config->getSyncSourceNodes(sourceName), config);
    const SourceRegistry &registry(SyncSource::getSourceRegistry());
    BOOST_FOREACH(const RegisterSyncSource *sourceInfo, registry) {
        auto_ptr<SyncSource> source(sourceInfo->m_create(params));
        if (!source.get()) {
            continue;
        } else if (source->isInactive()) {
            SE_THROW_EXCEPTION(NoSuchSource, "'" + m_configName + "' backend of source '" + sourceName + "' is not supported");
        } else {
            databases = source->getDatabases();
            return;
        }
    }

    SE_THROW_EXCEPTION(NoSuchSource, "'" + m_configName + "' has no '" + sourceName + "' source");
}

SE_END_CXX
