/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
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

#include <config.h>

#include <base/Log.h>
#include <posix/base/posixlog.h>

#include <iostream>
using namespace std;

#include <libgen.h>
#ifdef HAVE_GLIB
#include <glib-object.h>
#endif

#include "EvolutionContactSource.h"
#include "EvolutionCalendarSource.h"
#include "EvolutionSyncClient.h"

/**
 * list all known data sources of a certain type
 */
static void listSources( EvolutionSyncSource &syncSource, const string &header )
{
    cout << header << ":\n";
    EvolutionSyncSource::sources sources = syncSource.getSyncBackends();

    for( EvolutionSyncSource::sources::const_iterator it = sources.begin();
         it != sources.end();
         it++ ) {
        cout << it->m_name << " (" << it->m_uri << ")\n";
    }
}

int main( int argc, char **argv )
{
#ifdef HAVE_GLIB
    // this is required on Maemo and does not harm either on a normal
    // desktop system with Evolution
    g_type_init();
#endif

    setLogFile("-");
    LOG.reset();
    LOG.setLevel(LOG_LEVEL_INFO);
    resetError();

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
        if ( argc == 1 ) {
#ifdef ENABLE_EBOOK
            EvolutionContactSource contactSource( string( "list" ), NULL );
            listSources( contactSource, "address books" );
            cout << "\n";
#endif

#ifdef ENABLE_ECAL
            EvolutionCalendarSource eventSource(E_CAL_SOURCE_TYPE_EVENT,
                                                string("list"),
                                                NULL);
            listSources(eventSource, "calendars");
            cout << "\n";

            EvolutionCalendarSource todoSource(E_CAL_SOURCE_TYPE_TODO,
                                               string("list"),
                                               NULL);
            listSources(todoSource, "tasks");
            cout << "\n";

            EvolutionCalendarSource journalSource(E_CAL_SOURCE_TYPE_JOURNAL,
                                                  string("list"),
                                                  NULL);
            listSources(todoSource, "memos");
#endif

            fprintf( stderr, "\nusage: %s <server>\n", argv[0] );
        } else {
            set<string> sources;

            for (int source = 2; source < argc; source++ ) {
                sources.insert(argv[source]);
            }

            EvolutionSyncClient client(argv[1], true, sources);
            client.sync();
        }
        return 0;
    } catch ( const std::exception &ex ) {
        LOG.error( "%s", ex.what() );
    } catch (...) {
        LOG.error( "unknown error" );
    }

    return 1;
}
