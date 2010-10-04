/*
 * Copyright (C) 2010 Intel Corporation
 */

#ifndef INCL_CALDAVSOURCE
#define INCL_CALDAVSOURCE

#include <config.h>

#ifdef ENABLE_DAV

#include "WebDAVSource.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class CalDAVSource : public WebDAVSource
{
 public:
    CalDAVSource(const SyncSourceParams &params);

    /* implementation of SyncSource interface */
    virtual const char *getMimeType() const { return "text/calendar"; }
    virtual const char *getMimeVersion() const { return "2.0"; }
};

SE_END_CXX

#endif // ENABLE_DAV
#endif // INCL_CALDAVSOURCE
