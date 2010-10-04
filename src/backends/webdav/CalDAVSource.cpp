/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"

#ifdef ENABLE_DAV

#include <syncevo/declarations.h>
SE_BEGIN_CXX

CalDAVSource::CalDAVSource(const SyncSourceParams &params) :
    WebDAVSource(params)
{
}

SE_END_CXX

#endif // ENABLE_DAV
