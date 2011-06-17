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

#ifndef INCL_CURLTRANSPORTAGENT
#define INCL_CURLTRANSPORTAGENT

#include "config.h"

#ifdef ENABLE_LIBCURL

#include <syncevo/TransportAgent.h>
#include <curl/curl.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX


/**
 * message send/receive with curl
 *
 * The simple curl API is used, so sending blocks until the
 * reply is ready.
 */
class CurlTransportAgent : public HTTPTransportAgent
{
 public:
    CurlTransportAgent();
    ~CurlTransportAgent();

    virtual void setURL(const std::string &url);
    virtual void setProxy(const std::string &proxy);
    virtual void setProxyAuth(const std::string &user, const std::string &password);
    virtual void setSSL(const std::string &cacerts,
                        bool verifyServer,
                        bool verifyHost);
    virtual void setContentType(const std::string &type);
    virtual void setUserAgent(const std::string &agent);
    virtual void shutdown();
    virtual void send(const char *data, size_t len);
    virtual void cancel();
    virtual Status wait(bool noReply = false);
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
    virtual void setTimeout(int seconds);
    int processCallback();
    void setAborting(bool aborting) {m_aborting = aborting;}

 private:
    CURL *m_easyHandle;
    curl_slist *m_slist;
    std::string m_contentType;
    Status m_status;
    bool m_aborting;

    Timespec m_sendStartTime;
    int m_timeoutSeconds;

    /**
     * libcurl < 7.17.0 does not copy strings passed into curl_easy_setopt().
     * These are local copies that remain valid as long as needed.
     */
    std::string m_url, m_proxy, m_auth, m_agent,
        m_cacerts;

    /** message buffer (owned by caller) */
    const char *m_message;
    /** number of valid bytes in m_message */
    size_t m_messageLen;
    /** number of sent bytes in m_message */
    size_t m_messageSent;

    /** reply buffer */
    char *m_reply;
    /** number of valid bytes in m_reply */
    size_t m_replyLen;
    /** total buffer size */
    size_t m_replySize;

    /** error text from curl, set via CURLOPT_ERRORBUFFER */
    char m_curlErrorText[CURL_ERROR_SIZE];

    /** CURLOPT_READFUNCTION, stream == CurlTransportAgent */
    static size_t readDataCallback(void *buffer, size_t size, size_t nmemb, void *stream) throw();
    size_t readData(void *buffer, size_t size) throw();

    /** CURLOPT_WRITEFUNCTION, stream == CurlTransportAgent */
    static size_t writeDataCallback(void *ptr, size_t size, size_t nmemb, void *stream) throw();
    size_t writeData(void *buffer, size_t size) throw();

    /** CURLOPT_PROGRESS callback, use this function to detect user abort */
    static int progressCallback (void *ptr, double dltotal, double dlnow, double uptotal, double upnow);

    /** check curl error code and turn into exception */
    void checkCurl(CURLcode code, bool exception = true);

    /**
     * initialize curl if necessary, return new handle
     *
     * Never returns NULL, instead throws exceptions.
     */
    static CURL *easyInit();
};


SE_END_CXX

#endif // ENABLE_LIBCURL
#endif // INCL_TRANSPORTAGENT
