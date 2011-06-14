/*
 * Copyright (C) 2011 Intel Corporation
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

#ifndef COMMON_H
#define COMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <syncevo/Logging.h>
#include <syncevo/LogStdout.h>
#include <syncevo/LogRedirect.h>
#include <syncevo/util.h>
#include <syncevo/SyncContext.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/SoupTransportAgent.h>
#include <syncevo/SyncSource.h>
#include <syncevo/SyncML.h>
#include <syncevo/FileConfigNode.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/Cmdline.h>
#include <syncevo/GLibSupport.h>

#include <synthesis/san.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>

#include <list>
#include <map>
#include <memory>
#include <iostream>
#include <limits>
#include <cmath>
#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/noncopyable.hpp>

#include <glib-object.h>
#include <glib/gi18n.h>
#ifdef USE_GNOME_KEYRING
extern "C" {
#include <gnome-keyring.h>
}
#endif

// redefining "signals" clashes with the use of that word in gtkbindings.h,
// included via notify.h
#define QT_NO_KEYWORDS

#ifdef USE_KDE_KWALLET
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QLatin1String>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtDBus/QDBusConnection>

#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>

#include <kwallet.h>
#endif

#include "NotificationManagerFactory.h"

#endif // COMMON_H
