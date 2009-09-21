/*
 * Copyright (C) 2009 Intel Corporation
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

#include <syncevo/TransportAgent.h>
#include <syncevo/SyncConfig.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

const char * const TransportAgent::m_contentTypeSyncML = "application/vnd.syncml+xml";
const char * const TransportAgent::m_contentTypeSyncWBXML = "application/vnd.syncml+wbxml";
const char * const TransportAgent::m_contentTypeURLEncoded = "application/x-www-form-urlencoded";
const char * const TransportAgent::m_contentTypeServerAlertedNotificationDS = "application/vnd.syncml.ds.notification";

void HTTPTransportAgent::setConfig(SyncConfig &config)
{
    if (config.getUseProxy()) {
        setProxy(config.getProxyHost());
        setProxyAuth(config.getProxyUsername(),
                     config.getProxyPassword());
    }
    setUserAgent(config.getUserAgent());
    setSSL(config.findSSLServerCertificate(),
           config.getSSLVerifyServer(),
           config.getSSLVerifyHost());
}

SE_END_CXX
