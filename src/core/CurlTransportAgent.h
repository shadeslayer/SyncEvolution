/*
 * Copyright (C) 2009 Patrick Ohly
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

#ifndef INCL_CURLTRANSPORTAGENT
#define INCL_CURLTRANSPORTAGENT

#include <config.h>

#ifdef ENABLE_LIBCURL

#include "TransportAgent.h"
#include <curl/curl.h>

namespace SyncEvolution {

/**
 * message send/receive with curl
 *
 * The simple curl API is used, so sending blocks until the
 * reply is ready.
 */
class CurlTransportAgent : public TransportAgent
{
 public:
    CurlTransportAgent();
    ~CurlTransportAgent();

    virtual void setURL(const std::string &url);
    virtual void setProxy(const std::string &proxy);
    virtual void setProxyAuth(const std::string &user, const std::string &password);
    virtual void setContentType(const std::string &type);
    virtual void send(const char *data, size_t len);
    virtual void cancel();
    virtual Status wait();
    virtual void getReply(const char *&data, size_t &len);

 private:
    CURL *m_easyHandle;
    curl_slist *m_slist;
    std::string m_contentType;
    Status m_status;

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

    /** check curl error code and turn into exception */
    void checkCurl(CURLcode code);

    /**
     * initialize curl if necessary, return new handle
     *
     * Never returns NULL, instead throws exceptions.
     */
    static CURL *easyInit();
};

} // namespace SyncEvolution


#endif // ENABLE_LIBCURL
#endif // INCL_TRANSPORTAGENT
