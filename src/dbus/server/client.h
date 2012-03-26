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

#ifndef CLIENT_H
#define CLIENT_H

#include <list>

#include "gdbus-cxx-bridge.h"

#include <syncevo/declarations.h>

SE_BEGIN_CXX

class Server;
class Resource;

/**
 * Tracks a single client and all sessions and connections that it is
 * connected to. Referencing them ensures that they stay around as
 * long as needed.
 */
class Client
{
    Server &m_server;

    typedef std::list< boost::shared_ptr<Resource> > Resources_t;
    Resources_t m_resources;

    /** counts how often a client has called Attach() without Detach() */
    int m_attachCount;

    /** current client setting for notifications (see HAS_NOTIFY) */
    bool m_notificationsEnabled;

public:
    const GDBusCXX::Caller_t m_ID;

    Client(Server &server,
           const GDBusCXX::Caller_t &ID) :
        m_server(server),
        m_attachCount(0),
        m_notificationsEnabled(true),
        m_ID(ID)
    {}
    ~Client();

    void increaseAttachCount() { ++m_attachCount; }
    void decreaseAttachCount() { --m_attachCount; }
    int getAttachCount() const { return m_attachCount; }

    void setNotificationsEnabled(bool enabled) { m_notificationsEnabled = enabled; }
    bool getNotificationsEnabled() const { return m_notificationsEnabled; }

    /**
     * Attach a specific resource to this client. As long as the
     * resource is attached, it cannot be freed. Can be called
     * multiple times, which means that detach() also has to be called
     * the same number of times to finally detach the resource.
     */
    void attach(boost::shared_ptr<Resource> resource)
    {
        m_resources.push_back(resource);
    }

    /**
     * Detach once from the given resource. Has to be called as
     * often as attach() to really remove all references to the
     * session. It's an error to call detach() more often than
     * attach().
     */
    void detach(Resource *resource);

    void detach(boost::shared_ptr<Resource> resource)
    {
        detach(resource.get());
    }

    /**
     * Remove all references to the given resource, regardless whether
     * it was referenced not at all or multiple times.
     */
    void detachAll(Resource *resource) {
        Resources_t::iterator it = m_resources.begin();
        while (it != m_resources.end()) {
            if (it->get() == resource) {
                it = m_resources.erase(it);
            } else {
                ++it;
            }
        }
    }
    void detachAll(boost::shared_ptr<Resource> resource)
    {
        detachAll(resource.get());
    }

    /**
     * return corresponding smart pointer for a certain resource,
     * empty pointer if not found
     */
    boost::shared_ptr<Resource> findResource(Resource *resource)
    {
        for (Resources_t::iterator it = m_resources.begin();
             it != m_resources.end();
             ++it) {
            if (it->get() == resource) {
                // got it
                return *it;
            }
        }
        return boost::shared_ptr<Resource>();
    }
};

SE_END_CXX

#endif // CLIENT_H
