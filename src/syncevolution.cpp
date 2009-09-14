/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include "config.h"
#include <stddef.h>
#include <iostream>
#include <memory>
using namespace std;

#include <libgen.h>
#ifdef HAVE_GLIB
#include <glib-object.h>
#endif

#include <syncevo/Cmdline.h>
#include "EvolutionSyncSource.h"
#include <syncevo/SyncContext.h>
#include <syncevo/LogRedirect.h>
#include "CmdlineSyncClient.h"

#include <dlfcn.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

#if defined(ENABLE_MAEMO) && defined (ENABLE_EBOOK)

// really override the symbol, even if redefined by EDSAbiWrapper
#undef e_contact_new_from_vcard
extern "C" EContact *e_contact_new_from_vcard(const char *vcard)
{
    static typeof(e_contact_new_from_vcard) *impl;

    if (!impl) {
        impl = (typeof(impl))dlsym(RTLD_NEXT, "e_contact_new_from_vcard");
    }

    // Old versions of EDS-DBus parse_changes_array() call
    // e_contact_new_from_vcard() with a pointer which starts
    // with a line break; Evolution is not happy with that and
    // refuses to parse it. This code forwards until it finds
    // the first non-whitespace, presumably the BEGIN:VCARD.
    while (*vcard && isspace(*vcard)) {
        vcard++;
    }

    return impl ? impl(vcard) : NULL;
}
#endif

/**
 * This is a class derived from Cmdline. The purpose
 * is to implement the factory method 'createSyncClient' to create
 * new implemented 'CmdlineSyncClient' objects.
 */
class KeyringSyncCmdline : public Cmdline {
 public:
    KeyringSyncCmdline(int argc, const char * const * argv, ostream &out, ostream &err):
        Cmdline(argc, argv, out, err) 
    {}
    /**
     * create a user implemented sync client.
     */
    SyncContext* createSyncClient() {
        return new CmdlineSyncClient(m_server, true, m_keyring);
    }
};

extern "C"
int main( int argc, char **argv )
{
#ifdef ENABLE_MAEMO
    // EDS-DBus uses potentially long-running calls which may fail due
    // to the default 25s timeout. Some of these can be replaced by
    // their async version, but e_book_async_get_changes() still
    // triggered it.
    //
    // The workaround for this is to link the binary against a libdbus
    // which has the dbus-timeout.patch and thus let's users and
    // the application increase the default timeout.
    setenv("DBUS_DEFAULT_TIMEOUT", "600000", 0);
#endif

    // Intercept stderr and route it through our logging.
    // stdout is printed normally. Deconstructing it when
    // leaving main() does one final processing of pending
    // output.
    LogRedirect redirect(false);

#if defined(HAVE_GLIB)
    // this is required when using glib directly or indirectly
    g_type_init();
    g_thread_init(NULL);
#endif

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Expand PATH to cover the directory we were started from?
    // This might be needed to find normalize_vcard.
    char *exe = strdup(argv[0]);
    if (strchr(exe, '/') ) {
        char *dir = dirname(exe);
        string path;
        char *oldpath = getenv("PATH");
        if (oldpath) {
            path += oldpath;
            path += ":";
        }
        path += dir;
        setenv("PATH", path.c_str(), 1);
    }
    free(exe);

    try {
        EDSAbiWrapperInit();

        /*
         * don't log errors to cerr: LogRedirect cannot distinguish
         * between our valid error messages and noise from other
         * libs, therefore it would get suppressed (logged at
         * level DEVELOPER, while output is at most INFO)
         */
        KeyringSyncCmdline cmdline(argc, argv, cout, cout);
        if (cmdline.parse() &&
            cmdline.run()) {
            return 0;
        } else {
            return 1;
        }
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}

SE_END_CXX
