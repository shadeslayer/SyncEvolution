/*
 * Copyright (C) 2003-2007 Funambol, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY, TITLE, NONINFRINGEMENT or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */

#ifndef INCL_POSIX_DEVICE_MANAGEMENT_NODE
#define INCL_POSIX_DEVICE_MANAGEMENT_NODE
/** @cond DEV */

#include "base/util/ArrayElement.h"
#include "spdm/ManagementNode.h"
#include <stdlib.h>

namespace spdm {
#ifndef BOOL
# define BOOL int
#endif


/*
 * File-based implementation of ManagementNode.
 * Each node is mapped to one file, located in
 *    $HOME/.sync4j/<node>
 * with entries of the type
 * <property>\s*=\s*<value>\n
 *
 * Comments look like:
 * \s*# <comment>
 *
 * This is an extended version of the same class also
 * found in the POSIX part of the C++ client library.
 */
class DeviceManagementNode : public ManagementNode {
    ArrayList *lines;
    BOOL modified;
    char *prefix;
    BOOL autosave;

    class line : public ArrayElement {
        char *str;

        public:
            line(const char *newStr = NULL) { str = NULL; setLine(newStr); }
            ~line() { free(str); }
            ArrayElement *clone() { return new line(str); }

            const char *getLine() { return str; }
            void setLine(const char *newStr) { if (str) { free(str); } str = strdup(newStr ? newStr : ""); }
    };

    // the application's working directory
    int cwdfd;

    // change into directory which holds config file,
    // creating directories if necessary for writing
    //
    // @return TRUE for success, FALSE for error - call returnFromDir() in both cases
    BOOL gotoDir(BOOL read);

    // return to original directory after a gotoDir()
    void returnFromDir();

    public:

        // ------------------------------------------ Constructors & destructors

        /**
         * Constructor.
         *
         * @param parent - a ManagementNode is usually under the context of a
         *                 parent node.
         * @param name - the node name
         *
         */
        DeviceManagementNode(const char* parent, const char *leafName);
        DeviceManagementNode(const char* fullName);

        DeviceManagementNode(const DeviceManagementNode &other);
        virtual ~DeviceManagementNode();


        // ----------------------------------------------------- Virtual methods

        /*
         * Returns the value of the given property
         *
         * @param property - the property name
         */
        virtual char* readPropertyValue(const char* property);


        /*
         * Sets a property value.
         *
         * @param property - the property name
         * @param value - the property value (zero terminated string)
         */
        virtual void setPropertyValue(const char* property, const char* value);

        /**
         * Extract all currently defined properties.
         * The strings are stored in StringBuffer instances owned by the arrays.
         * Clearing the arrays frees the memory.
         *
         * @retval properties       if non-NULL, then it is filled with the name of all properties
         * @retval values           if non-NULL, then it is filled with the corresponding values
         */
        virtual void readProperties(ArrayList *properties, ArrayList *values);

        /**
         * Remove a certain property.
         *
         * @param property    the name of the property which is to be removed
         */
        virtual void removeProperty(const char *property);

        /*
         * Returns the children's name of the parent node.
         */
        char **getChildrenNames();

        /*
         * Find how many children are defined for this node in the underlying
         * config system.
         */
        virtual int getChildrenMaxCount();


        // if autosave is TRUE, then destructor writes modified content to file
        BOOL getAutosave() { return autosave; }
        void setAutosave(BOOL as) { autosave = as; }


        // copy content of "lines" to or from file
        void update(BOOL read);

        /*
         * Creates a new ManagementNode with the exact content of this object.
         * The new instance MUST be created with the C++ new opertator.
         */
        virtual ArrayElement* clone();


};

}

/** @endcond */
#endif
