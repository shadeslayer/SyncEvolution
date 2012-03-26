
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

#include "client.h"
#include "session.h"
#include "server.h"

SE_BEGIN_CXX

Client::~Client()
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s is destructing", m_ID.c_str());

    // explicitly detach all resources instead of just freeing the
    // list, so that the special behavior for sessions in detach() is
    // triggered
    while (!m_resources.empty()) {
        detach(m_resources.front().get());
    }
}

void Client::detach(Resource *resource)
{
    for (Resources_t::iterator it = m_resources.begin();
         it != m_resources.end();
         ++it) {
        if (it->get() == resource) {
            if (it->unique()) {
                // client was the last owner, and thus the session must be idle (otherwise
                // it would also be referenced as active session)
                boost::shared_ptr<Session> session = boost::dynamic_pointer_cast<Session>(*it);
                if (session) {
                    // give clients a chance to query the session
                    m_server.delaySessionDestruction(session);
                    // allow other sessions to start
                    session->done();
                }
            }
            // this will trigger removal of the resource if
            // the client was the last remaining owner
            m_resources.erase(it);
            return;
        }
    }

    SE_THROW_EXCEPTION(InvalidCall, "cannot detach from resource that client is not attached to");
}

SE_END_CXX
