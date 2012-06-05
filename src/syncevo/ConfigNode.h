/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_EVOLUTION_ABSTRACT_CONFIG_NODE
# define INCL_EVOLUTION_ABSTRACT_CONFIG_NODE

#include <map>
#include <utility>
#include <string>
#include <sstream>

#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>

#include <syncevo/declarations.h>
#include <syncevo/util.h>
#include <syncevo/ConfigFilter.h>
SE_BEGIN_CXX

// see ConfigFilter.h
class ConfigProps;

/**
 * This class corresponds to the Funambol C++ client
 * DeviceManagementNode, but offers a slightly different API.  See
 * ConfigTree for details.
 */
class ConfigNode {
 public:
    /** free resources without saving */
    virtual ~ConfigNode() {}

    /** creates a file-backed config node which accepts arbitrary key/value pairs */
    static boost::shared_ptr<ConfigNode> createFileNode(const std::string &filename);

    /** a name for the node that the user can understand */
    virtual std::string getName() const = 0;

    /**
     * save all changes persistently
     */
    virtual void flush() = 0;

    /** reload from background storage, discarding in-memory changes */
    virtual void reload() {}

    /**
     * Returns the value of the given property
     *
     * @param property - the property name
     * @return value of the property or empty string if not set;
     *         also includes whether the property was set
     */
    virtual InitStateString readProperty(const std::string &property) const = 0;

    /**
     * Sets a property value. Overloaded, with variations providing
     * convenience wrappers around it.
     *
     * @param property   the property name
     * @param value      the property value and whether it is considered
     *                   "explicitly set"; if it is not, then the property
     *                   shall be removed from the list of properties
     * @param comment    a comment explaining what the property is about, with
     *                   \n separating lines; might be used by the backend 
     *                   when adding a new property
     * @param defValue   Default value in case that value isn't set.
     *                   Can be be used by an implementation to annotate
     *                   unset properties.
     */
    void setProperty(const std::string &property,
                     const InitStateString &value,
                     const std::string &comment = std::string("")) {
        writeProperty(property, value, comment);
    }
    void setProperty(const std::string &property,
                     const char *value) {
        setProperty(property, InitStateString(value, true));
    }
    void setProperty(const std::string &property,
                     char *value) {
        setProperty(property, InitStateString(value, true));
    }

    /**
     * Sets a boolean property, using "true/false".
     */
    void setProperty(const std::string &property, const InitState<bool> &value) {
        setProperty(property,
                    InitStateString(value ? "true" : "false",
                                    value.wasSet()));
    }
    void setProperty(const std::string &property, bool value) {
        setProperty(property, InitState<bool>(value, true));
    }

    /**
     * Sets a property value with automatic conversion to the underlying string,
     * using stream formatting.
     */
    template <class T> void setProperty(const std::string &property,
                                        const InitState<T> &value) {
        std::stringstream strval;
        strval << value.get();
        setProperty(property,
                    InitStateString(strval.str(),
                                    value.wasSet()));
    }
    template <class T> void setProperty(const std::string &property,
                                        const T &value) {
        setProperty(property,
                    InitState<T>(value, true));
    }

    bool getProperty(const std::string &property,
                     std::string &value) const {
        InitStateString str = readProperty(property);
        if (str.wasSet()) {
            value = str;
            return true;
        } else {
            return false;
        }
    }

    bool getProperty(const std::string &property,
                     bool &value) const {
        InitStateString str = readProperty(property);
        if (!str.wasSet() ||
            str.empty()) {
            return false;
        }

        /* accept keywords */
        if (boost::iequals(str, "true") ||
            boost::iequals(str, "yes") ||
            boost::iequals(str, "on")) {
            value = true;
            return true;
        }
        if (boost::iequals(str, "false") ||
            boost::iequals(str, "no") ||
            boost::iequals(str, "off")) {
            value = false;
            return true;
        }

        /* zero means false */
        double number;
        if (getProperty(property, number)) {
            value = number != 0;
            return true;
        }

        return false;
    }

    template <class T> bool getProperty(const std::string &property,
                                        T &value) const {
        InitStateString str = readProperty(property);
        if (!str.wasSet() ||
            str.empty()) {
            return false;
        } else {
            std::stringstream strval(str);
            strval >> value;
            return !strval.bad();
        }
    }

    // defined here for source code backwards compatibility
    typedef ConfigProps PropsType;

    /**
     * Actual implementation of setProperty(). Uses different
     * different name, to avoid shadowing the setProperty()
     * variations in derived classes.
     */
    virtual void writeProperty(const std::string &property,
                               const InitStateString &value,
                               const std::string &comment = std::string("")) = 0;


    /**
     * Extract all list of all currently defined properties
     * and their values. Does not include values which were
     * initialized with their defaults, if the implementation
     * remembers that.
     *
     * @retval props    to be filled with key/value pairs; guaranteed
     *                  to be empty before the call
     */
    virtual void readProperties(ConfigProps &props) const = 0;

    /**
     * Add the given properties. To replace the content of the
     * node, call clear() first.
     */
    virtual void writeProperties(const ConfigProps &props);

    /**
     * Remove a certain property.
     *
     * @param property    the name of the property which is to be removed
     */
    virtual void removeProperty(const std::string &property) = 0;

    /**
     * Remove all properties.
     */
    virtual void clear() = 0;

    /**
     * Node exists in backend storage.
     */
    virtual bool exists() const = 0;

    /**
     * Node is read-only. Otherwise read-write.
     */
    virtual bool isReadOnly() const = 0;
};


SE_END_CXX
#endif
