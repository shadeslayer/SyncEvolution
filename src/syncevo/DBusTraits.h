/*
 * Copyright (C) 2011-12 Intel Corporation
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

/**
 * GDBus traits for sending some common structs over D-Bus.
 * Needed by by syncevo-local-sync and syncevo-dbus-helper.
 */

#ifndef INCL_SYNCEVO_DBUS_TRAITS
# define INCL_SYNCEVO_DBUS_TRAITS

#include <syncevo/ConfigFilter.h>
#include <syncevo/UserInterface.h>
#include <syncevo/SyncML.h>
#include <synthesis/engine_defs.h>

#include <gdbus-cxx-bridge.h>

namespace GDBusCXX {
    template <> struct dbus_traits<sysync::TProgressEventEnum> :
        dbus_enum_traits<sysync::TProgressEventEnum, uint32_t>
    {};

    template <> struct dbus_traits<SyncEvo::SyncMode> :
        dbus_enum_traits<SyncEvo::SyncMode, uint32_t>
    {};

    /** for InitState or InitStateClass: like a pair of two values, but with different storage class on the host */
    template <class I> struct dbus_init_state_traits :
        public dbus_traits< std::pair<typename I::value_type, bool> >
    {
        typedef dbus_traits< std::pair<typename I::value_type, bool> > base_traits;
        typedef I host_type;
        typedef const I &arg_type;

        static void get(connection_type *conn,
                        message_type *msg,
                        reader_type &reader, host_type &value)
        {
            typename base_traits::host_type tmp;
            base_traits::get(conn, msg, reader, tmp);
            value = host_type(tmp.first, tmp.second);
        }

        static void append(builder_type &builder, arg_type value)
        {
            base_traits::append(builder, typename base_traits::host_type(value.get(), value.wasSet()));
        }
    };

    template <class T> struct dbus_traits< SyncEvo::InitStateClass<T> > :
        public dbus_init_state_traits< SyncEvo::InitStateClass<T> > {};
    template <class T> struct dbus_traits< SyncEvo::InitState<T> > :
        public dbus_init_state_traits< SyncEvo::InitState<T> > {};

    /**
     * Actual content is a std::map, so serialization can be done using that.
     * We only have to ensure that instances and parameters use FullProps.
     */
    template <> struct dbus_traits<SyncEvo::FullProps> :
        public dbus_traits < std::map<std::string, SyncEvo::ContextProps, SyncEvo::Nocase<std::string> > >
    {
        typedef SyncEvo::FullProps host_type;
        typedef const SyncEvo::FullProps &arg_type;
    };

    /**
     * Similar to SyncEvo::FullProps.
     */
    template <> struct dbus_traits<SyncEvo::SourceProps> :
        public dbus_traits < std::map<std::string, SyncEvo::ConfigProps, SyncEvo::Nocase<std::string> > >
    {
        typedef SyncEvo::SourceProps host_type;
        typedef const SyncEvo::SourceProps &arg_type;
    };
    template <> struct dbus_traits<SyncEvo::ConfigProps> :
        public dbus_traits < std::map<std::string, SyncEvo::InitStateString, SyncEvo::Nocase<std::string> > >
    {
        typedef SyncEvo::ConfigProps host_type;
        typedef const SyncEvo::ConfigProps &arg_type;
    };

    /**
     * a struct containing ConfigProps + SourceProps
     */
    template <> struct dbus_traits<SyncEvo::ContextProps> :
        public dbus_struct_traits<SyncEvo::ContextProps,
                                  GDBusCXX::dbus_member<SyncEvo::ContextProps, SyncEvo::ConfigProps, &SyncEvo::ContextProps::m_syncProps,
                                                        GDBusCXX::dbus_member_single<SyncEvo::ContextProps, SyncEvo::SourceProps, &SyncEvo::ContextProps::m_sourceProps> > >
    {};

    /**
     * a struct containing various strings and an integer
     */
    template <> struct dbus_traits<SyncEvo::ConfigPasswordKey> :
        public dbus_struct_traits<SyncEvo::ConfigPasswordKey,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::user,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::server,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::domain,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::object,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::protocol,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::authtype,
                                  GDBusCXX::dbus_member_single<SyncEvo::ConfigPasswordKey, unsigned int, &SyncEvo::ConfigPasswordKey::port> > > > > > > >
    {};
}

#endif // INCL_SYNCEVO_DBUS_TRAITS
