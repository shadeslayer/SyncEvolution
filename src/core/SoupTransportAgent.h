/*
 * Copyright (C) 2009 Intel Corporation
 */

#ifndef INCL_SOUPTRANSPORTAGENT
#define INCL_SOUPTRANSPORTAGENT

#include <config.h>

#ifdef ENABLE_LIBSOUP

#include "TransportAgent.h"
#include "EvolutionSmartPtr.h"
#include <libsoup/soup.h>
#include <glib/gmain.h>

namespace SyncEvolution {

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
class SoupTransportAgent : public TransportAgent
{
 public:
    /**
     *  @param loop     the glib loop to use when waiting for IO;
     *                  will be owned and unref'ed by the new instance;
     *                  if NULL a new loop in the default context is used
     */
    SoupTransportAgent(GMainLoop *loop = NULL);
    ~SoupTransportAgent();

    virtual void setURL(const std::string &url);
    virtual void setProxy(const std::string &proxy);
    virtual void setProxyAuth(const std::string &user, const std::string &password);
    virtual void setContentType(const std::string &type);
    virtual void setUserAgent(const std::string &agent);
    virtual void send(const char *data, size_t len);
    virtual void cancel();
    virtual Status wait();
    virtual void getReply(const char *&data, size_t &len);

 private:
    std::string m_proxyUser;
    std::string m_proxyPassword;
    std::string m_URL;
    std::string m_contentType;
    eptr<SoupSession, GObject> m_session;
    eptr<GMainLoop, GMainLoop, GLibUnref> m_loop;
    Status m_status;
    std::string m_failure;

    /** response, copied from SoupMessage */
    eptr<SoupBuffer, SoupBuffer, GLibUnref> m_response;

    /** SoupSessionCallback, redirected into user_data->HandleSessionCallback() */
    static void SessionCallback(SoupSession *session,
                                SoupMessage *msg,
                                gpointer user_data);
    void HandleSessionCallback(SoupSession *session,
                               SoupMessage *msg);
};

} // namespace SyncEvolution

#endif // ENABLE_LIBSOUP
#endif // INCL_TRANSPORTAGENT
