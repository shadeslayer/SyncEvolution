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

#include <config.h>

#ifdef ENABLE_OBEX
#ifdef ENABLE_BLUETOOTH
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#endif

#include <synthesis/syerror.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <glib.h>
#include <syncevo/SuspendFlags.h>
#include <syncevo/ObexTransportAgent.h>

#include <syncevo/declarations.h>

SE_BEGIN_CXX

ObexTransportAgent::ObexTransportAgent (OBEX_TRANS_TYPE type, GMainLoop *loop) :
    m_status(INACTIVE),
    m_transType(type),
    m_context(g_main_context_ref(loop ?
                                 g_main_loop_get_context(loop) :
                                 g_main_context_default())),
    m_address(""),
    m_port(-1),
    m_buffer(NULL),
    m_bufferSize(0),
    m_timeoutSeconds(0),
    m_disconnecting(false),
    m_connectStatus(START)
{
}

ObexTransportAgent::~ObexTransportAgent()
{
}

/*
 * Only setURL if the address/port has not been initialized;
 * because the URL is not likely be changed during a
 * sync session. 
 * For Bluetooth devices the URL maybe splitted as two parts:
 * address and channel, the delimiter is '+'.
 **/
void ObexTransportAgent::setURL(const std::string &url)
{
    if(m_address.empty()){
        if(m_transType == OBEX_BLUETOOTH) {
            size_t pos = url.find_last_of('+');
            if (pos != std::string::npos) {
                m_address = url.substr(0,pos);
                m_port = atoi(url.substr(pos+1, std::string::npos).c_str());
                if(!m_port) {
                    SE_THROW_EXCEPTION(TransportException, "ObexTransport: Malformed url");
                }
            } else {
                m_address = url;
            }
            if(m_address.empty()) {
                SE_THROW_EXCEPTION (TransportException, "ObexTransport: Malformed url");
            }
        }
    }
}

void ObexTransportAgent::setContentType(const std::string &type) {
    m_contentType = type;
}


void ObexTransportAgent::setTimeout(int seconds)
{
    m_timeoutSeconds = seconds;
}

void ObexTransportAgent::connect() {
    m_obexReady = false;
    if(m_transType == OBEX_BLUETOOTH) {
        if(m_port == -1) {
            EDSAbiWrapperInit();
            // sdp_connect may be a pointer when EVOLUTION_COMPATIBILITY is enabled.
            // Must check whether we really have an implementation of the sdp_ calls
            // before using them.
            if (!SyncEvoHaveLibbluetooth) {
                SE_THROW_EXCEPTION (TransportException, "no suitable libbluetooth found, try setting Bluetooth channel manually (obex-bt://<mac>+<channel>)");
            }
            
            //use sdp to detect the appropriate channel
            //Do not use BDADDR_ANY to avoid a warning
            bdaddr_t bdaddr, anyaddr ={{0,0,0,0,0,0}};
            str2ba (m_address.c_str(), &bdaddr);
            m_sdp.set(sdp_connect(&anyaddr, &bdaddr, SDP_NON_BLOCKING),
                      "ObexTransport Bluetooth sdp connect failed");
            m_connectStatus = SDP_START;
            GIOChannelPtr sdpIO(g_io_channel_unix_new(sdp_get_socket(m_sdp)),
                                "sdp socket channel");
            m_sdpEvent.set(g_io_add_watch(sdpIO, (GIOCondition) (G_IO_IN|G_IO_OUT|G_IO_HUP|G_IO_ERR|G_IO_NVAL), sdp_source_cb, static_cast <void*> (this)),
                           "IO watch for SDP");
        } else {
            m_connectStatus = ADDR_READY;
            connectInit();
        }
    }
    /*Wait until connection is sucessfully set up*/
    wait(true);
    if (m_connectStatus != CONNECTED) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport: connection setup failed");
    }
}

/*
 * Called when the address of remote peer is available, maybe via some
 * discovery mechanism.
 */
void ObexTransportAgent::connectInit () {
    if (m_connectStatus != ADDR_READY) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport: address info for remote peer not ready");
    }
    if(m_transType == OBEX_BLUETOOTH) {
        bdaddr_t bdaddr;
        str2ba (m_address.c_str(), &bdaddr);
        if(m_port == -1) {
            SE_THROW_EXCEPTION (TransportException, 
                    "ObexTransport: no channel found for Bluetooth peer");
        }
        int sockfd = socket (AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
        if (sockfd == -1) {
            SE_THROW_EXCEPTION (TransportException, "Error creating Bluetooth socket");
        }
        cxxptr<Socket> sockObj (new Socket (sockfd));  
        SE_LOG_DEV(NULL,NULL, "Connecting Bluetooth device with address %s and channel %d",
                m_address.c_str(), m_port);
        // Init the OBEX handle
        ObexPtr handle(OBEX_Init (OBEX_TRANS_FD, obex_event, 0),
                       "Obex Handle");

        OBEX_SetUserData (handle, static_cast<void *> (this));
        // Connect the device, do not use BtOBEX_TransportConnect as it is
        // blocking.
        sockaddr_rc any;
        bdaddr_t anyaddr ={{0,0,0,0,0,0}};
        memset(&any, 0, sizeof(any));
        any.rc_family = AF_BLUETOOTH;
        bacpy (&any.rc_bdaddr, &anyaddr);
        any.rc_channel = 0;
        if (bind (sockfd, (struct sockaddr *) &any, sizeof (sockaddr_rc)) <0){
            SE_THROW_EXCEPTION (TransportException, "ObexTransport: Socket bind failed");
        }
        //set socket to non-blocking
        int flags = fcntl (sockfd, F_GETFL);
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
        //create the io channel
        GIOChannelPtr channel(g_io_channel_unix_new (sockfd),
                              "socket io channel");
        GLibEvent obexEvent(g_io_add_watch (channel, (GIOCondition) (G_IO_IN|G_IO_OUT|G_IO_HUP|G_IO_ERR|G_IO_NVAL), obex_fd_source_cb, static_cast<void *> (this)),
                            "socket io channel watch");

        //connect to remote device
        sockaddr_rc peer;
        memset(&peer, 0, sizeof(peer));
        peer.rc_family = AF_BLUETOOTH;
        bacpy (&peer.rc_bdaddr , &bdaddr);
        peer.rc_channel = m_port;

        if (::connect (sockfd, (struct sockaddr *) &peer, sizeof (sockaddr_rc)) == -1) {
            //check the error status
            if (errno == EINPROGRESS || errno == EAGAIN){
                m_connectStatus = INIT0;
                //copy sockobj so that it will not close the underlying socket
                m_sock = sockObj;
                m_obexEvent = obexEvent;
                m_channel = channel;
                m_handle = handle;
                return;
            } else {
                SE_LOG_ERROR (NULL, NULL, "connect failed with error code %d", errno);
                SE_THROW_EXCEPTION (TransportException, "ObexTransport: connect request failed with error");
            }
        }

        // Put Connect Cmd to init the session
        m_connectStatus = INIT1;
        m_sock = sockObj;
        m_obexEvent = obexEvent;
        m_channel = channel;
        m_handle = handle;
        connectReq();
    }//Bluetooth
    else {
        m_status = FAILED;
        SE_LOG_ERROR (NULL, NULL, "ObexTransport: unsuported transport type");
        return;
    }
}

/* After OBEX Handle is inited and the device is connected, 
 * send the connect cmd to initialize the session 
 */
void ObexTransportAgent::connectReq() {
    if (m_connectStatus!=INIT1 || !m_handle) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport: OBEX Handle not inited or device not connected");
    }

    //set up transport mtu
    OBEX_SetTransportMTU (m_handle, DEFAULT_RX_MTU, DEFAULT_TX_MTU);

    // set up the fd transport
    if (FdOBEX_TransportSetup (m_handle, m_sock->get(), m_sock->get(), OBEX_MAXIMUM_MTU) <0) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport: Fd transport set up failed");
    }

    obex_object_t *connect = newCmd(OBEX_CMD_CONNECT);
    /* Add the header for the sync target */
    obex_headerdata_t header;
    header.bs = (unsigned char *) "SYNCML-SYNC";
    OBEX_ObjectAddHeader(m_handle, connect, OBEX_HDR_TARGET, header, strlen ((char *) header.bs), OBEX_FL_FIT_ONE_PACKET);
    m_obexReady = false;
    m_requestStart = time (NULL);
    if (OBEX_Request (m_handle, connect) <0) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport: OBEX connect init failed");
    }
    m_connectStatus = INIT2;
}

void ObexTransportAgent::shutdown() {
    //reset up obex fd soruce 
    GLibEvent obexEventSource;
    if (m_channel) {
        obexEventSource.set(g_io_add_watch(m_channel,
                                           (GIOCondition) (G_IO_IN|G_IO_OUT|G_IO_HUP|G_IO_ERR|G_IO_NVAL),
                                           obex_fd_source_cb, static_cast<void *> (this)),
                            "OBEX shutdown event");
    }

    if (m_handle) {
        /* It might be true there is an ongoing OBEX request undergoing, 
         * must cancel it before sending another cmd */
        OBEX_CancelRequest (m_handle, 0);
    }

    //block a while waiting for the disconnect response, we will disconnects
    //always even without a response.
    m_obexReady = false;
    if (!m_disconnecting) {
        m_disconnecting = true;
        if (m_handle) {
            obex_object_t *disconnect = newCmd(OBEX_CMD_DISCONNECT);
            //add header "connection id"
            obex_headerdata_t header;
            header.bq4 = m_connectId;
            OBEX_ObjectAddHeader (m_handle, disconnect, OBEX_HDR_CONNECTION, header, sizeof
                                  (m_connectId), OBEX_FL_FIT_ONE_PACKET);
            if (OBEX_Request (m_handle, disconnect) <0) {
                m_status = FAILED;
                SE_THROW_EXCEPTION (TransportException, "ObexTransport: OBEX disconnect cmd failed");
            }
        }
    }
    m_obexEvent = obexEventSource;
}

/**
 * Send the request to peer
 */
void ObexTransportAgent::send(const char *data, size_t len) {
    SE_LOG_DEV (NULL, NULL, "ObexTransport send is called");
    cxxptr<Socket> sockObj = m_sock;
    GIOChannelPtr channel = m_channel;
    if(m_connectStatus != CONNECTED) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport send: underlying transport is not conncted");
    }
    obex_object_t *put = newCmd(OBEX_CMD_PUT);
    //add header "connection id"
    obex_headerdata_t header;
    header.bq4 = m_connectId;
    OBEX_ObjectAddHeader (m_handle, put, OBEX_HDR_CONNECTION, header, sizeof
            (m_connectId), OBEX_FL_FIT_ONE_PACKET);
    //add header "target"
    header.bs =  reinterpret_cast<unsigned char *> (const_cast <char *>(m_contentType.c_str()));
    OBEX_ObjectAddHeader (m_handle, put, OBEX_HDR_TYPE, header, m_contentType.size()+1, 0);
    //add header "length"
    header.bq4 = len;
    OBEX_ObjectAddHeader (m_handle, put, OBEX_HDR_LENGTH, header, sizeof (size_t), 0);
    //add header "body"
    header.bs = reinterpret_cast <const uint8_t *> (data);
    OBEX_ObjectAddHeader (m_handle, put, OBEX_HDR_BODY, header, len, 0);

    //reset up the OBEX fd source 
    GLibEvent obexEventSource(g_io_add_watch (channel, (GIOCondition) (G_IO_IN|G_IO_OUT|G_IO_HUP|G_IO_ERR|G_IO_NVAL), obex_fd_source_cb, static_cast<void *> (this)),
                              "OBEX fd event");

    //send the request
    m_status = ACTIVE;
    m_requestStart = time (NULL);
    m_obexReady = false;
    if (OBEX_Request (m_handle, put) < 0) {
        SE_THROW_EXCEPTION (TransportException, "ObexTransport: send failed");
    }
    m_sock = sockObj;
    m_channel = channel;
    m_obexEvent = obexEventSource;
}

/*
 * Abort the transport session; don't wait for anything.
 */
void ObexTransportAgent::cancel() {
    m_requestStart = 0;
    m_connectStatus = END;
    if (m_handle) {
        OBEX_TransportDisconnect(m_handle);
    }
    if (m_disconnecting) {
        SE_LOG_WARNING (NULL, NULL, "Cancel disconncting process");
        if (m_status != CLOSED) {
            m_status = FAILED;
        }
    } else {
        // called during normal operations;
        // set m_disconnecting like the abort handler
        // did before it was removed
        m_disconnecting = true;
        m_status = CANCELED;
        // remove the event source from the agent before calling shutdown(),
        // will be freed when this function returns
        GLibEvent obexEventSource = m_obexEvent;
        shutdown();
    }
}

/**
 * 1)Wait until the connection is set up.
 * 2)Wait until the response is ready, which means:
 * waits for the Put request being successfully sent
 * sends the Get request to pull response 
 * waits until Get response is successfully received
 *
 * Runs the main loop manually so that it does not block other components.
 */
TransportAgent::Status ObexTransportAgent::wait(bool noReply) {
    while (!m_obexReady) {
        g_main_context_iteration (m_context, TRUE);
        if (m_status == FAILED ||
            m_status == CANCELED) {
            m_obexEvent.set(0);
            m_sock.set(0);
            m_channel.set(0);
            if (m_status == FAILED) {
                SE_THROW_EXCEPTION(TransportException, "OBEX/Bluetooth transport error or problem on the peer");
            } else {
                SE_THROW_EXCEPTION_STATUS(StatusException,
                                          "transport aborted",
                                          SyncMLStatus(sysync::LOCERR_USERABORT));
            }
        }
    }

    //remove the obex event source here
    //only at this point we can be sure obexEvent is propertely set up
    cxxptr<Socket> sockObj = m_sock;
    GLibEvent obexEvent = m_obexEvent;
    GIOChannelPtr channel = m_channel;

    if (!noReply) {
        //send the Get request
        obex_object_t *get = newCmd(OBEX_CMD_GET);
        //add header "connection id"
        obex_headerdata_t header;
        header.bq4 = m_connectId;
        OBEX_ObjectAddHeader (m_handle, get, OBEX_HDR_CONNECTION, header, sizeof
                (m_connectId), OBEX_FL_FIT_ONE_PACKET);
        //add header "target"
        header.bs =  reinterpret_cast <const unsigned char *> (m_contentType.c_str());
        OBEX_ObjectAddHeader (m_handle, get, OBEX_HDR_TYPE, header, m_contentType.size()+1, 0);

        //send the request
        m_obexReady = false;
        if (OBEX_Request (m_handle, get) < 0) {
            SE_THROW_EXCEPTION (TransportException, "ObexTransport: get failed");
        }

        while (!m_obexReady) {
            g_main_context_iteration (m_context, TRUE);
            if (m_status == FAILED) {
                SE_THROW_EXCEPTION (TransportException, 
                        "ObexTransprotAgent: Underlying transport error");
            }
        }
    }

    if(m_status != CLOSED) {
        m_sock = sockObj;
        m_channel = channel;
    } 
    return m_status;
}

/**
 * read the response from the buffer
 */
void ObexTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType){
   cxxptr<Socket> sockObj = m_sock;
   GIOChannelPtr channel = m_channel;
   if (m_status != GOT_REPLY || !m_buffer || !m_bufferSize) {
       SE_THROW_EXCEPTION (TransportException, "");
   }
   data = m_buffer;
   len = m_bufferSize;
   // There is no content type sent back from the peer according to the spec 
   contentType = "";
   m_sock = sockObj;
   m_channel = channel;
}

gboolean ObexTransportAgent::sdp_source_cb (GIOChannel *io, GIOCondition cond, void *udata)
{
    return static_cast <ObexTransportAgent*> (udata) -> sdp_source_cb_impl(io, cond);
}

gboolean ObexTransportAgent::obex_fd_source_cb (GIOChannel *io, GIOCondition cond, void *udata)
{
    return static_cast <ObexTransportAgent*> (udata) -> obex_fd_source_cb_impl(io, cond);
}

void ObexTransportAgent::sdp_callback (uint8_t type, uint16_t status, uint8_t *rsp, size_t size, void *udata) 
{
    return static_cast <ObexTransportAgent *> (udata) -> sdp_callback_impl(type, status, rsp, size);
}

void ObexTransportAgent::obex_event (obex_t *handle, obex_object_t *object, int mode, int event, int obex_cmd, int obex_rsp){
    (static_cast<ObexTransportAgent *> (OBEX_GetUserData (handle)))
        ->obex_callback(object, mode, event, obex_cmd, obex_rsp);
}

gboolean ObexTransportAgent::sdp_source_cb_impl (GIOChannel *io, GIOCondition cond)
{
    try {
        if(cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)){
            SE_THROW_EXCEPTION (TransportException, "SDP connection end unexpectedly");
        } else if ((cond &G_IO_OUT) && m_connectStatus == SDP_START) {
            m_connectStatus = SDP_REQ;
            sdp_set_notify (m_sdp, sdp_callback, static_cast <void *> (this));
            const unsigned char syncml_client_uuid[] = {
                0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x10, 0x00,
                0x80, 0x00, 0x00, 0x02, 0xEE, 0x00, 0x00, 0x02};
            uint32_t range = 0x0000ffff;
            uuid_t uuid;
            sdp_list_t *services, *attrs;
            sdp_uuid128_create(&uuid, syncml_client_uuid);
            services = sdp_list_append(NULL, &uuid);
            attrs = sdp_list_append(NULL, &range);
            if (sdp_service_search_attr_async(m_sdp, services, SDP_ATTR_REQ_RANGE, attrs) < 0) {
                sdp_list_free (attrs, NULL);
                sdp_list_free (services, NULL);
                SE_THROW_EXCEPTION(TransportException, "ObexTransport: Bluetooth sdp service search failed");
            }
            sdp_list_free(attrs, NULL);
            sdp_list_free(services, NULL);
            return TRUE;
        } else if ((cond &G_IO_IN) && m_connectStatus == SDP_REQ) {
            sdp_process (m_sdp);
            m_sdp.set(0);
            return FALSE;
        } else {
            return TRUE;
        }
    } catch (...) {
        handleException("sdp_source_cb");
    }
    m_sdp.set(0);
    return FALSE;
}

void ObexTransportAgent::sdp_callback_impl (uint8_t type, uint16_t status, uint8_t *rsp, size_t size)
{
	size_t scanned;
    int bufSize = size;
	int channel = -1;

    try {
        m_connectStatus = SDP_DONE;
        if (status || type != SDP_SVC_SEARCH_ATTR_RSP) {
            SE_THROW_EXCEPTION (TransportException, "ObexTransportAgent: Bluetooth service search failed");
        }

	    int seqSize = 0;
        uint8_t dtdp;
#if defined(HAVE_BLUEZ_SAFE) || defined(EVOLUTION_COMPATIBILITY)
        scanned = sdp_extract_seqtype_safe(rsp, bufSize, &dtdp, &seqSize);
#elif defined(HAVE_BLUEZ_BUFSIZE)
        scanned = sdp_extract_seqtype(rsp, bufSize, &dtdp, &seqSize);
#else
        scanned = sdp_extract_seqtype(rsp, &dtdp, &seqSize);
#endif
        if (!scanned || !seqSize) {
            SE_THROW_EXCEPTION (TransportException, "ObexTransportAgent: Bluetooth service search failed");
        }

        rsp += scanned;
        bufSize -= scanned;
        do {
            sdp_record_t *rec;
            sdp_list_t *protos;
            int recSize;

            recSize = 0;
#if defined(HAVE_BLUEZ_SAFE) || defined(EVOLUTION_COMPATIBILITY) 
            rec = sdp_extract_pdu_safe(rsp, bufSize, &recSize);
#elif defined(HAVE_BLUEZ_BUFSIZE)
            rec = sdp_extract_pdu(rsp, bufSize, &recSize);
#else
            rec = sdp_extract_pdu(rsp, &recSize);
#endif
            if (!rec) {
                SE_THROW_EXCEPTION (TransportException, "ObexTransportAgent: sdp_extract_pdu failed");
            }
            if (!recSize) {
                sdp_record_free(rec);
                SE_THROW_EXCEPTION (TransportException, "ObexTransportAgent: sdp_extract_pdu failed");
            }
            if (!sdp_get_access_protos(rec, &protos)) {
                channel = sdp_get_proto_port(protos, RFCOMM_UUID);
                sdp_list_foreach(protos,
                        (sdp_list_func_t) sdp_list_free, NULL);
                sdp_list_free(protos, NULL);
            }
            sdp_record_free(rec);
            if (channel > 0) {
                break;
            }
            scanned += recSize;
            rsp += recSize;
            bufSize -= recSize;
        } while (scanned < size && bufSize > 0);

        if (channel <= 0) {
            SE_THROW_EXCEPTION (TransportException, "ObexTransportAgent: Bluetooth service search failed");
        }

        m_port = channel;
        m_connectStatus = ADDR_READY;
        connectInit();
    } catch (...) {
        handleException("sdp_callback_impl");
    }
}

/* obex event source callbackg*/
gboolean ObexTransportAgent::obex_fd_source_cb_impl (GIOChannel *io, GIOCondition cond)
{
    cxxptr<Socket> sockObj = m_sock;
    GIOChannelPtr channel = m_channel;

    if (m_status == CLOSED) {
        return TRUE;
    }

    try {
        if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
            SE_THROW_EXCEPTION (TransportException,
                                StringPrintf("obex_fd_source_cb_impl: got event %s%s%s", 
                                             (cond & G_IO_HUP) ?"HUP":"",
                                             (cond & G_IO_ERR) ?"ERR":"",
                                             (cond & G_IO_NVAL) ?"NVAL":""));
        }

        if (m_connectStatus == INIT0 && (cond & G_IO_OUT)) {
            int status = -1;
            socklen_t optLen = sizeof (int);
            if (!getsockopt (sockObj->get(), SOL_SOCKET, SO_ERROR, &status, &optLen) && status == 0) {
                m_connectStatus = INIT1;
                m_sock = sockObj;
                m_channel = channel;
                connectReq();
                return TRUE;
            } else {
                SE_THROW_EXCEPTION (TransportException, "OBEXTransport: socket connect failed");
            }
        }

        time_t now = time(NULL);
        if (m_timeoutSeconds &&
            (m_requestStart != 0) &&
            (now >= m_timeoutSeconds +m_requestStart)) {
            m_sock = sockObj;
            m_channel = channel;
            //timeout
            m_status = TIME_OUT;
            //currently we will not support transport resend for 
            //OBEX transport ??
            cancel();
            return TRUE;
        }

        if (OBEX_HandleInput (m_handle, OBEX_POLL_INTERVAL) <0 && errno != EINTR) {
            //transport error
            //no way to recovery, simply abort
            //disconnect without sending disconnect request
            //The failure may already has been processed during
            //OBEX callback, thus no need to handle it again if status is
            //already marked as FAILED
            if (m_status != FAILED){
                cancel();
            }
        }
        m_sock = sockObj;
        m_channel = channel;
        return TRUE;
    } catch (...) {
        handleException("obex_fd_source_cb_impl");
    }
    return FALSE;
}


/**
 * Obex Event Callback
 */
void ObexTransportAgent::obex_callback (obex_object_t *object, int mode, int event, int obex_cmd, int obex_rsp) {
    try {
        switch (event) {
            case OBEX_EV_PROGRESS:
                SE_LOG_DEV (NULL, NULL, "OBEX progress");
                break;
            case OBEX_EV_REQDONE:
                m_obexReady = true;
                m_requestStart = 0;
                if (obex_rsp != OBEX_RSP_SUCCESS) {
                    SE_LOG_ERROR (NULL, NULL, "OBEX Request %d got a failed response %s",
                            obex_cmd,OBEX_ResponseToString(obex_rsp));
                    m_status = FAILED;
                    return;
                } else {
                    switch (obex_cmd) {
                        case OBEX_CMD_CONNECT:
                            {
                                uint8_t headertype = 0;
                                obex_headerdata_t header;
                                uint32_t len;

                                while (OBEX_ObjectGetNextHeader (m_handle, object, &headertype, &header, &len)) {
                                    if (headertype == OBEX_HDR_CONNECTION) {
                                        m_connectId = header.bq4;
                                    } else if (headertype == OBEX_HDR_WHO) { 
                                        SE_LOG_DEV (NULL, NULL, 
                                                "OBEX Transport: get header who from connect response with value %.*s",
                                                len, header.bs);
                                    } else {
                                        SE_LOG_WARNING (NULL, NULL, 
                                                "OBEX Transport: Unknow header from connect response");
                                    }
                                }
                                if (m_connectId == 0) {
                                    m_status = FAILED;
                                    SE_LOG_ERROR(NULL, NULL, 
                                            "No connection id received from connect response");
                                }
                                m_connectStatus = CONNECTED;

                                break;
                            }
                        case OBEX_CMD_DISCONNECT:
                            {
                                if (m_connectStatus == CONNECTED) {
                                    m_connectStatus = END;
                                    OBEX_TransportDisconnect (m_handle);
                                    m_status = CLOSED;
                                }
                                break;
                            }
                        case OBEX_CMD_GET:
                            {
                                int length = 0;
                                uint8_t headertype = 0;
                                obex_headerdata_t header;
                                uint32_t len;
                                while (OBEX_ObjectGetNextHeader (m_handle, object, &headertype, &header, &len)) {
                                    if (headertype == OBEX_HDR_LENGTH) {
                                        length = header.bq4;
                                    } else if (headertype == OBEX_HDR_BODY) {
                                        if (length ==0) {
                                            length = len;
                                            SE_LOG_DEV (NULL, NULL, 
                                                    "No length header for get response is recevied, using body size %d", len);
                                        }
                                        if (length ==0) {
                                            m_status = FAILED;
                                            SE_LOG_ERROR (NULL, NULL, 
                                                    "ObexTransport: Get zero sized response body for Get");
                                        }
                                        m_buffer.set(new char[length], "buffer");
                                        m_bufferSize = length;
                                        memcpy (m_buffer, header.bs, length);
                                    } else {
                                        SE_LOG_WARNING (NULL, NULL, "Unknow header received for Get cmd");
                                    }
                                }
                                if( !length || !m_buffer) {
                                    m_status = FAILED;
                                    SE_LOG_ERROR (NULL, NULL, "Get Cmd response have no body");
                                }
                                m_status = GOT_REPLY;
                                break;
                            }
                    }
                }//else
                break;
            case OBEX_EV_LINKERR:
                {
                    if (obex_rsp == 0 && m_disconnecting) {
                        //disconnct event
                        m_connectStatus = END;
                        OBEX_TransportDisconnect (m_handle);
                        //a normal case, same as OBEX_EV_DONE
                        m_obexReady = true;
                        m_status = CLOSED;
                    } else if (obex_rsp !=0) {
                        SE_LOG_ERROR (NULL, NULL, "ObexTransport Error %d", obex_rsp);
                        m_status = FAILED;
                        return;
                    }
                    break;
                }
            case OBEX_EV_STREAMEMPTY:
            case OBEX_EV_STREAMAVAIL:
                break;
        }
    } catch (...) {
        handleException("obex_callback");
    }
}

void ObexTransportAgent::handleException(const char *where)
{
    SE_LOG_DEBUG(NULL, NULL, "ObexTransport: exception thrown in %s", where);
    if (m_status == FAILED ||
        m_status == CANCELED ) {
        // don't change anything, don't bother the user with error
        // messages - something went already wrong
    } else {
        // log error and remember put transport into error state
        Exception::handle();
        m_status = FAILED;
    }
}

obex_object_t* ObexTransportAgent::newCmd(uint8_t cmd) {
    obex_object_t *cmdObject = OBEX_ObjectNew (m_handle, cmd);
    if(!cmdObject) {
        m_status = FAILED;
        SE_LOG_ERROR (NULL, NULL, "ObexTransport: OBEX Object New failed");
        return NULL;
    } else {
        return cmdObject;
    }
}

#endif //ENABLE_OBEX
SE_END_CXX
