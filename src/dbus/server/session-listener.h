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

#ifndef SESSION_LISTENER_H
#define SESSION_LISTENER_H

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * a listener to listen changes of session
 * currently only used to track changes of running a sync in a session
 */
class SessionListener
{
public:
    /**
     * method is called when a sync is successfully started.
     * Here 'successfully started' means the synthesis engine starts
     * to access the sources.
     */
    virtual void syncSuccessStart() {}

    /**
     * method is called when a sync is done. Also
     * sync status are passed.
     */
    virtual void syncDone(SyncMLStatus status) {}

    virtual ~SessionListener() {}
};

SE_END_CXX

#endif // SESSION_LISTENER_H
