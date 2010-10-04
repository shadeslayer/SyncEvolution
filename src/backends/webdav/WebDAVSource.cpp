/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "WebDAVSource.h"

#ifdef ENABLE_DAV

SE_BEGIN_CXX

WebDAVSource::WebDAVSource(const SyncSourceParams &params) :
    TrackingSyncSource(params)
{
}

void WebDAVSource::open()
{
    // TODO
}

bool WebDAVSource::isEmpty()
{
    // TODO
    return false;
}

void WebDAVSource::close()
{
}

WebDAVSource::Databases WebDAVSource::getDatabases()
{
    Databases result;

    // TODO: scan for right collections
    result.push_back(Database("select database via relative URI",
                              "<path>"));
    return result;
}

void WebDAVSource::listAllItems(RevisionMap_t &revisions)
{
    // TODO
}

void WebDAVSource::readItem(const string &uid, std::string &item, bool raw)
{
    // TODO
}

TrackingSyncSource::InsertItemResult WebDAVSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    // TODO
    return InsertItemResult("",
                            "",
                            false /* true if adding item was turned into update */);
}


void WebDAVSource::removeItem(const string &uid)
{
    // TODO
}

SE_END_CXX

#endif /* ENABLE_DAV */

#ifdef ENABLE_MODULES
# include "WebDAVSourceRegister.cpp"
#endif
