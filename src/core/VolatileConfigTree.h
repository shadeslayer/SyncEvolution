/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 */

#ifndef INCL_EVOLUTION_VOLATILE_CONFIG_TREE
# define INCL_EVOLUTION_VOLATILE_CONFIG_TREE

# include "FileConfigTree.h"

/**
 * This class can store properties while in memory, but will never
 * save them persistently. Implemented by instantiating a FileConfigTree
 * with invalid path and intercepting its flush() method.
 */
class VolatileConfigTree : public FileConfigTree {
 public:
 VolatileConfigTree() :
    FileConfigTree("/dev/null", false)
        {}

    virtual void flush() {}
};

#endif
