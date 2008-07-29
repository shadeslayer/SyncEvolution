#ifndef INCL_VIRTUAL_CONVERTER
#define INCL_VIRTUAL_CONVERTER

#include "VObject.h"

namespace vocl {

class VConverter{

public:
    static VObject* parse(char* buffer);

private:
    static VProperty* readFieldHeader(char* buffer);
    static bool readFieldBody(VObject &vo, char* buffer, VProperty* property);

    // Extract the parameter of certain properties, e.g. "BEGIN:" or "VERSION:".
    // The result is a pointer into buffCopy, which is expected to have
    // buffCopyLen wchars and will be reallocated if necessary.
    static char* extractObjectProperty(char* buffer, const char *property,
                                          char * &buffCopy, size_t &buffCopyLen);

    // extractObjectType() and extractObjectVersion() contain static buffers,
    // copy the result before calling these functions again!
    static char* extractObjectType(char* buffer);
    static char* extractObjectVersion(char* buffer);
    static bool extractGroup(char* propertyName, char* propertyGroup);
    
};

};
#endif
