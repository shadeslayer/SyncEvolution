/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 */

#ifndef INCL_EVOLUTION_VOLATILE_CONFIG_NODE
# define INCL_EVOLUTION_VOLATILE_CONFIG_NODE

#include "FilterConfigNode.h"
#include "FileConfigNode.h"

/**
 * This class can store properties while in memory, but will never
 * save them persistently. Implemented by instantiating a FileConfigNode
 * with invalid path and never calling its flush() method.
 */
class VolatileConfigNode : public FilterConfigNode {
 public:
 VolatileConfigNode() :
    FilterConfigNode(boost::shared_ptr<ConfigNode>(new FileConfigNode("/dev/null", "dummy.ini", true)))
        {}

    virtual string getName() const { return "intermediate configuration"; }
    virtual void flush() {}
};

#endif
