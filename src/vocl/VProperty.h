/**
 * Copyright (C) 2005-2006 Funambol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef INCL_VIRTUAL_PROPERTY
#define INCL_VIRTUAL_PROPERTY

#include "base/fscapi.h"
#include "base/util/WKeyValuePair.h"
#include "base/util/ArrayList.h"

namespace vocl {

#define VPROPETY_BUFFER 500

class VProperty : public ArrayElement {

private:

    char* name;
    char* value;
    void set(char** p, const char* v);
    ArrayList* parameters;

 public:       
	
    VProperty(char* propName , char* propValue  = NULL);
    ~VProperty();
    ArrayElement* clone();
    void setName (const char* name);
    void setValue (const char* value);
    char* getName(char* buf = NULL, int size = -1);
    char* getValue(char* buf = NULL, int size = -1);
    void addParameter(const char* paramName, const char* paramValue);
    void removeParameter(char* paramName);
    void removeParameter(int index);
    bool containsParameter(char* paramName);
    // Warning: the name does not always uniquely identify
    // the parameter, some of them may occur multiple times.
    // Use getParameterValue(int index) to get the value which
    // corresponds to a specific parameter.
    char* getParameterValue(char* paramName);
    char* getParameterValue(int index);
    char* getParameter(int index);
    int parameterCount();
    bool equalsEncoding(char* encoding);
    char* getPropComponent(int i);
    bool isType(char* type);
    char* toString();

 };

};
    
#endif
