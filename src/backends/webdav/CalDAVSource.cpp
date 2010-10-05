/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"

#ifdef ENABLE_DAV

#include <syncevo/declarations.h>
SE_BEGIN_CXX

CalDAVSource::CalDAVSource(const SyncSourceParams &params,
                           const boost::shared_ptr<Neon::Settings> &settings) :
    WebDAVSource(params, settings)
{
}

SE_END_CXX

#endif // ENABLE_DAV
