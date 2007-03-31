/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INCL_EVOLUTIONCLIENTCONFIG
#define INCL_EVOLUTIONCLIENTCONFIG

#include <client/DMTClientConfig.h>
#include <EvolutionSmartPtr.h>

/**
 * config class which ensures backwards compatibility
 * and (optionally) prevents writing of properties that the
 * code is not supposed to set
 */
class EvolutionClientConfig : public DMTClientConfig {
  public:
    EvolutionClientConfig(const char *root, bool saveAll = false) :
        DMTClientConfig(root),
        m_saveAll(saveAll)
    {}

  protected:
    /*
     * tweak the base class in two ways:
     * - continue to use the "syncml" node for all non-source properties, as in previous versions
     * - do not save properties which cannot be configured
     */
    virtual int readAuthConfig(ManagementNode& syncMLNode,
                               ManagementNode& authNode) {
        return DMTClientConfig::readAuthConfig(syncMLNode, syncMLNode);
    }
    virtual void saveAuthConfig(ManagementNode& syncMLNode,
                                ManagementNode& authNode) {
        DMTClientConfig::saveAuthConfig(syncMLNode, syncMLNode);
    }
    virtual int readConnConfig(ManagementNode& syncMLNode,
                               ManagementNode& connNode) {
        return DMTClientConfig::readConnConfig(syncMLNode, syncMLNode);
    }
    virtual void saveConnConfig(ManagementNode& syncMLNode,
                                ManagementNode& connNode) {
        DMTClientConfig::saveConnConfig(syncMLNode, syncMLNode);
    }
    virtual int readExtAccessConfig(ManagementNode& syncMLNode,
                                    ManagementNode& extNode) {
        return DMTClientConfig::readExtAccessConfig(syncMLNode, syncMLNode);
    }
    virtual void saveExtAccessConfig(ManagementNode& syncMLNode,
                                     ManagementNode& extNode) {
        DMTClientConfig::saveExtAccessConfig(syncMLNode, syncMLNode);
    }
    virtual int readDevInfoConfig(ManagementNode& syncMLNode,
                                  ManagementNode& devInfoNode) {
        int res = DMTClientConfig::readDevInfoConfig(syncMLNode, syncMLNode);
        
        // always read device ID from the traditional property "deviceId"
        arrayptr<char> tmp(syncMLNode.readPropertyValue("deviceId"));
        if (tmp && tmp[0]) {
            deviceConfig.setDevID(tmp);
        }
        
        return res;
    }
    virtual void saveDevInfoConfig(ManagementNode& syncMLNode,
                                   ManagementNode& devInfoNode) {
        if (m_saveAll) {
            DMTClientConfig::saveDevInfoConfig(syncMLNode, syncMLNode);
        }
    }
    virtual int readDevDetailConfig(ManagementNode& syncMLNode,
                                    ManagementNode& devDetailNode) {
        return DMTClientConfig::readDevDetailConfig(syncMLNode, syncMLNode);
    }
    virtual void saveDevDetailConfig(ManagementNode& syncMLNode,
                                     ManagementNode& devDetailNode) {
        if (m_saveAll) {
            DMTClientConfig::saveDevDetailConfig(syncMLNode, syncMLNode);
        }
    }
    virtual int readExtDevConfig(ManagementNode& syncMLNode,
                                 ManagementNode& extNode) {
        return DMTClientConfig::readExtDevConfig(syncMLNode, syncMLNode);
    }
    virtual void saveExtDevConfig(ManagementNode& syncMLNode,
                                  ManagementNode& extNode) {
        if (m_saveAll) {
            DMTClientConfig::saveExtDevConfig(syncMLNode, syncMLNode);
        }
    }
    
    virtual void saveSourceConfig(int i,
                                  ManagementNode& sourcesNode,
                                  ManagementNode& sourceNode) {
        if (m_saveAll) {
            DMTClientConfig::saveSourceConfig(i, sourcesNode, sourceNode);
        }
    }

  private:
    bool m_saveAll;
};

#endif // INCL_EVOLUTIONCLIENTCONFIG
