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

#ifndef INCL_SOUPTRANSPORTAGENT
#define INCL_SOUPTRANSPORTAGENT

#include "config.h"

#ifdef ENABLE_LIBSOUP

#include <syncevo/TransportAgent.h>
#include <syncevo/SmartPtr.h>
#include <libsoup/soup.h>
#include <glib.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX


class GLibUnref {
 public:
    static void unref(GMainLoop *pointer) { g_main_loop_unref(pointer); }
    static void unref(SoupMessageBody *pointer) { soup_message_body_free(pointer); }
    static void unref(SoupBuffer *pointer) { soup_buffer_free(pointer); }
    static void unref(SoupURI *pointer) { soup_uri_free(pointer); }
};

/**
 * message send/receive with libsoup
 *
 * An asynchronous soup session is used and the main loop
 * is invoked in the wait() method to make progress.
 */
class SoupTransportAgent : public HTTPTransportAgent
{
 public:
    /**
     *  @param loop     the glib loop to use when waiting for IO;
     *                  transport will increase the reference count;
     *                  if NULL a new loop in the default context is used
     */
    SoupTransportAgent(GMainLoop *loop = NULL);
    ~SoupTransportAgent();

    virtual void setURL(const std::string &url);
    virtual void setProxy(const std::string &proxy);
    virtual void setProxyAuth(const std::string &user, const std::string &password);
    virtual void setSSL(const std::string &cacerts,
                        bool verifyServer,
                        bool verifyHost);
    virtual void setContentType(const std::string &type);
    virtual void setUserAgent(const std::string &agent);
    virtual void shutdown() { m_status = CLOSED; }
    virtual void send(const char *data, size_t len);
    virtual void cancel();
    virtual Status wait(bool noReply = false);
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
    virtual void setTimeout(int seconds);
    gboolean processCallback();
 private:
    std::string m_proxyUser;
    std::string m_proxyPassword;
    std::string m_cacerts;
    bool m_verifySSL;
    std::string m_URL;
    std::string m_contentType;
    eptr<SoupSession, GObject> m_session;
    eptr<GMainLoop, GMainLoop, GLibUnref> m_loop;
    Status m_status;
    std::string m_failure;

    SoupMessage *m_message;
    GLibEvent m_timeoutEventSource;
    int m_timeoutSeconds;

    /** This function is called regularly to detect timeout */
    static gboolean TimeoutCallback (gpointer data);

    /** response, copied from SoupMessage */
    eptr<SoupBuffer, SoupBuffer, GLibUnref> m_response;
    std::string m_responseContentType;

    /** SoupSessionCallback, redirected into user_data->HandleSessionCallback() */
    static void SessionCallback(SoupSession *session,
                                SoupMessage *msg,
                                gpointer user_data);
    void HandleSessionCallback(SoupSession *session,
                               SoupMessage *msg);
};

SE_END_CXX

#endif // ENABLE_LIBSOUP
#endif // INCL_TRANSPORTAGENT
