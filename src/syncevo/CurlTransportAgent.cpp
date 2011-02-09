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

#include <syncevo/CurlTransportAgent.h>
#include <syncevo/SyncContext.h>

#ifdef ENABLE_LIBCURL

#include <algorithm>
#include <ctime>
#include <syncevo/util.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX


CurlTransportAgent::CurlTransportAgent() :
    m_easyHandle(easyInit()),
    m_slist(NULL),
    m_status(INACTIVE),
    m_timeoutSeconds(0),
    m_reply(NULL),
    m_replyLen(0),
    m_replySize(0)
{
    /*
     * set up for post where message is pushed into curl via
     * its read callback and reply is stored in write callback
     */
    CURLcode code;
    if ((code = curl_easy_setopt(m_easyHandle, CURLOPT_NOPROGRESS, false)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_PROGRESSFUNCTION, progressCallback)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_WRITEFUNCTION, writeDataCallback)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_WRITEDATA, (void *)this)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_READFUNCTION, readDataCallback)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_READDATA, (void *)this)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_ERRORBUFFER, this->m_curlErrorText )) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_AUTOREFERER, true)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_POST, true)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_FOLLOWLOCATION, true))) {
        /* error encountered, throw exception */
        curl_easy_cleanup(m_easyHandle);
        checkCurl(code);
    }
}

CURL *CurlTransportAgent::easyInit()
{
    static bool initialized = false;
    static CURLcode initres;

    if (!initialized) {
        initres = curl_global_init(CURL_GLOBAL_ALL);
        initialized = true;
    }

    if (initres) {
        SE_THROW_EXCEPTION(TransportException, "global curl initialization failed");
    }
    CURL *handle = curl_easy_init();
    if (!handle) {
        SE_THROW_EXCEPTION(TransportException, "no curl handle");
    }
    return handle;
}

CurlTransportAgent::~CurlTransportAgent()
{
    if (m_reply) {
        free(m_reply);
    }
    curl_easy_cleanup(m_easyHandle);
    curl_slist_free_all(m_slist);
}

void CurlTransportAgent::setURL(const std::string &url)
{
    m_url = url;
    CURLcode code = curl_easy_setopt(m_easyHandle, CURLOPT_URL, m_url.c_str());
    checkCurl(code);
}

void CurlTransportAgent::setProxy(const std::string &proxy)
{
    m_proxy = proxy;
    CURLcode code = curl_easy_setopt(m_easyHandle, CURLOPT_PROXY, m_proxy.c_str());
    checkCurl(code);
}

void CurlTransportAgent::setProxyAuth(const std::string &user, const std::string &password)
{
    m_auth = user + ":" + password;
    CURLcode code = curl_easy_setopt(m_easyHandle, CURLOPT_PROXYUSERPWD,
                                     m_auth.c_str());
    checkCurl(code);
}

void CurlTransportAgent::setContentType(const std::string &type)
{
    m_contentType = type;
}

void CurlTransportAgent::setUserAgent(const std::string &agent)
{
    m_agent = agent;
    CURLcode code = curl_easy_setopt(m_easyHandle, CURLOPT_USERAGENT,
                                     m_agent.c_str());
    checkCurl(code);
}

void CurlTransportAgent::setSSL(const std::string &cacerts,
                                bool verifyServer,
                                bool verifyHost)
{
    m_cacerts = cacerts;
    CURLcode code = CURLE_OK;

    if (!m_cacerts.empty()) {
        code = curl_easy_setopt(m_easyHandle, CURLOPT_CAINFO, m_cacerts.c_str());
    }
    if (!code) {
        code = curl_easy_setopt(m_easyHandle, CURLOPT_SSL_VERIFYPEER, (long)verifyServer);
    }
    if (!code) {
        code = curl_easy_setopt(m_easyHandle, CURLOPT_SSL_VERIFYHOST, (long)(verifyHost ? 2 : 0));
    }
    checkCurl(code);
}

void CurlTransportAgent::setTimeout(int seconds)
{
    m_timeoutSeconds = seconds;
}

void CurlTransportAgent::shutdown()
{
}

void CurlTransportAgent::send(const char *data, size_t len)
{
    CURLcode code;

    m_replyLen = 0;
    m_message = data;
    m_messageSent = 0;
    m_messageLen = len;

    curl_slist_free_all(m_slist);
    m_slist = NULL;
    
    // Setting Expect explicitly prevents problems with certain
    // proxies: if curl is allowed to depend on Expect, then it will
    // send the POST header and wait for the servers reply that it is
    // allowed to continue. This will always be the case with a correctly
    // configured SyncML and because some proxies reject unknown Expect
    // requests, it is better not used.
    m_slist = curl_slist_append(m_slist, "Expect:");

    std::string contentHeader("Content-Type: ");
    contentHeader += m_contentType;
    m_slist = curl_slist_append(m_slist, contentHeader.c_str());

    m_status = ACTIVE;
    if (m_timeoutSeconds) {
        m_sendStartTime = time(NULL);
    }
    m_aborting = false;
    if ((code = curl_easy_setopt(m_easyHandle, CURLOPT_PROGRESSDATA, static_cast<void *> (this)))||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_HTTPHEADER, m_slist)) ||
        (code = curl_easy_setopt(m_easyHandle, CURLOPT_POSTFIELDSIZE, len))
       ){
        m_status = CANCELED;
        checkCurl(code);
    }

    if ((code = curl_easy_perform(m_easyHandle))) {
        m_status = FAILED;
        checkCurl(code, false);
    } else {
        m_status = GOT_REPLY;
    }
}

void CurlTransportAgent::cancel()
{
    /* nothing to do */
}

TransportAgent::Status CurlTransportAgent::wait(bool noReply)
{
    return m_status;
}

void CurlTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType)
{
    data = m_reply;
    len = m_replyLen;
    const char *curlContentType;
    if (!curl_easy_getinfo(m_easyHandle, CURLINFO_CONTENT_TYPE, &curlContentType) &&
        curlContentType) {
        contentType = curlContentType;
    } else {
        // unknown
        contentType = "";
    }
}

size_t CurlTransportAgent::writeDataCallback(void *buffer, size_t size, size_t nmemb, void *stream) throw()
{
    return static_cast<CurlTransportAgent *>(stream)->writeData(buffer, size * nmemb);
}

size_t CurlTransportAgent::writeData(void *buffer, size_t size) throw()
{
    bool increase = false;
    while (m_replyLen + size > m_replySize) {
        m_replySize = m_replySize ? m_replySize * 2 : 64 * 1024;
        increase = true;
    }

    if (increase) {
        m_reply = (char *)realloc(m_reply, m_replySize);
        if (!m_reply) {
            m_replySize = 0;
            m_replyLen = 0;
            return 0;
        }
    }

    memcpy(m_reply + m_replyLen,
           buffer,
           size);
    m_replyLen += size;
    return size;
}

size_t CurlTransportAgent::readDataCallback(void *buffer, size_t size, size_t nmemb, void *stream) throw()
{
    return static_cast<CurlTransportAgent *>(stream)->readData(buffer, size * nmemb);
}

size_t CurlTransportAgent::readData(void *buffer, size_t size) throw()
{
    size_t curr = std::min(size, m_messageLen - m_messageSent);

    memcpy(buffer, m_message + m_messageSent, curr);
    m_messageSent += curr;
    return curr;
}

void CurlTransportAgent::checkCurl(CURLcode code, bool exception)
{
    if (code) {
        if(exception){
            SE_THROW_EXCEPTION(TransportException, m_curlErrorText);
        }else {
            SE_LOG_INFO(NULL, NULL, "CurlTransport Failure: %s", m_curlErrorText);
        }
    }
}

int CurlTransportAgent::progressCallback(void* transport, double, double, double, double)
{
    CurlTransportAgent *agent = static_cast<CurlTransportAgent *> (transport);
    const SuspendFlags &s_flags = SyncContext::getSuspendFlags();
    //abort transfer
    if (s_flags.state == SuspendFlags::CLIENT_ABORT){
        agent->setAborting (true);
        return -1;
    }
    return agent->processCallback();
}

int CurlTransportAgent::processCallback()
{
    if (m_timeoutSeconds) {
        time_t curTime = time(NULL);
        if (curTime - m_sendStartTime > m_timeoutSeconds) {
            m_status = TIME_OUT;
            return -1;
        }
    }
    return 0;
}

SE_END_CXX

#endif // ENABLE_LIBCURL

