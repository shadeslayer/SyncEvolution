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

#ifndef INCL_OBEXTRANSPORTAGENT
#define INCL_OBEXTRANSPORTAGENT

#include <config.h>

#ifdef ENABLE_OBEX

#include <syncevo/TransportAgent.h>
#include <syncevo/Logging.h>
#include <syncevo/declarations.h>
#include <syncevo/SmartPtr.h>

#ifdef  ENABLE_BLUETOOTH
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#endif
#include <openobex/obex.h>

SE_BEGIN_CXX

/**
 * utility class for various enties stored by
 * ObexTransportAgent in SmartPtr
 */
class ObexUnref {
 public:
    static void unref(GMainContext *context) { g_main_context_unref(context); }
    static void unref(sdp_session_t *sdp) { sdp_close(sdp); }
    static void unref(GIOChannel *channel) { g_io_channel_unref(channel); }
    static void unref(obex_t *handle) { OBEX_Cleanup(handle); }
};

typedef eptr<GMainContext, GMainContext, ObexUnref> GMainContextPtr;
typedef eptr<sdp_session_t, sdp_session_t, ObexUnref> SDPSessionPtr;
typedef eptr<GIOChannel, GIOChannel, ObexUnref> GIOChannelPtr;
typedef eptr<obex_t, obex_t, ObexUnref> ObexPtr;

class Socket {
    int socketfd;
 public:
     Socket() {socketfd = -1;}
     Socket(int fd) {socketfd = fd;}
     ~Socket() { if (socketfd !=-1) {::close (socketfd);} }
     int get() {return socketfd;}
};



/**
 * message send/receive with libopenobex
 * should work with a transport binding (Bluetooth, USB, etc.)
 */
class ObexTransportAgent : public TransportAgent 
{
    public:
        enum OBEX_TRANS_TYPE{
            OBEX_BLUETOOTH,
            OBEX_USB,
            INVALID
        };

        /**
         * @param loop     the glib loop to use when waiting for IO;
         *                 transport will increase the reference count;
         *                 if NULL a new loop in the default context is used
         */
        ObexTransportAgent(OBEX_TRANS_TYPE type, GMainLoop *loop);
        ~ObexTransportAgent();

        virtual void setURL (const std::string &url);
        virtual void setContentType(const std::string &type);
        virtual void shutdown();
        virtual void send(const char *data, size_t len);
        virtual void cancel();
        virtual Status wait(bool noReply);
        virtual void getReply(const char *&data, size_t &len, std::string &contentType);
        virtual void setTimeout(int seconds);
        /* Obex specific api: connecting the underlying transport */
        void connect();

    private:
        /*call back used by libopenobex, will route to member function obex_callback*/
        static void obex_event (obex_t *handle, obex_object_t *object, int mode, int event, int obex_cmd, int obex_rsp);
        /* callback used by obex fd poll, will route to member function
         * obex_fd_source_cb_impl */
        static gboolean obex_fd_source_cb (GIOChannel *io, GIOCondition cond, void *udata);
        /* callback used by Bluetooth sdp poll, will route to member function
         * sdp_source_cb_impl */
        static gboolean sdp_source_cb (GIOChannel *io, GIOCondition cond, void *udata);
        /* callback called when a sdp async transaction is finished, route to
         * member function sdp_callback_impl*/
        static void sdp_callback (uint8_t type, uint16_t status, uint8_t *rsp, size_t size, void *user_data);

        void obex_callback (obex_object_t *object, int mode, int event, int obex_cmd, int obex_rsp);
        gboolean obex_fd_source_cb_impl (GIOChannel *io, GIOCondition cond);
        gboolean sdp_source_cb_impl (GIOChannel *io, GIOCondition cond);
        void sdp_callback_impl (uint8_t type, uint16_t status, uint8_t *rsp, size_t size);

        /**
         * Handle exception thrown by any of the C callbacks.
         * Exception must not escape into calling C function.
         * Instead, set bad status and wait for that to
         * be discovered in wait().
         */
        void handleException(const char *where);

        /* First phase of OBEX connect: connect to remote peer */
        void connectInit ();
        /* Second phase of OBEX connect: send connect cmd to initalize */
        void connectReq ();

        /* wrapper of OBEX_ObjectNew*/
        obex_object_t * newCmd (uint8_t cmd);

        static const int DEFAULT_RX_MTU=32767;
        static const int DEFAULT_TX_MTU=32767;

        /*Indicates when the OBEX transport has finished it's part of working,
         * it's the application to turn to do something */
        bool m_obexReady;
        Status m_status;

        /*
         * The underlying transport type: Bluetooth, USB.
         */
        OBEX_TRANS_TYPE m_transType;

        /** context that needs to be kept alive while waiting for OBEX */
        GMainContextPtr m_context;

        /* The address of the remote device  
         * macadd for Bluetooth; device name for usb; host name for
         * tcp/ip
         */
        std::string m_address;
        /*
         * Service port for the remote device 
         * channel for Bluetooth, port for tcp/ip
         */
        int m_port;

        /*The underlying socket fd*/
        cxxptr<Socket> m_sock;
        GLibEvent m_obexEvent;
        GIOChannelPtr m_channel;

        std::string m_contentType;
        arrayptr<char> m_buffer;
        int m_bufferSize;

        SDPSessionPtr m_sdp;
        GLibEvent m_sdpEvent;

        int m_timeoutSeconds;
        time_t m_requestStart;
        /** OBEX poll interval */
        static const int OBEX_POLL_INTERVAL = 1;

        uint32_t m_connectId;
        //already fired disconnect
        bool m_disconnecting;

        ObexPtr m_handle;
        enum CONNECT_STATUS {
            START, 
            SDP_START, //sdp transaction start
            SDP_REQ, //sdp request has been sent
            SDP_DONE, //sdp transaction finished
            ADDR_READY, //address is prepared
            INIT0,  //connect is called but not finished
            INIT1,  //connect is finished. 
            INIT2,  //connect cmd is sent, but not finished.
            CONNECTED, //connection sucessfully setup
            ERROR,  //connection in error state
            END
        };
        CONNECT_STATUS m_connectStatus;
};

SE_END_CXX
#endif //ENABLE_OBEX
#endif
