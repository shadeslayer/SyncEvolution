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

#define EDS_ABI_WRAPPER_NO_REDEFINE 1
#include <syncevo/eds_abi_wrapper.h>
#include <syncevo/SyncContext.h>

#include <string>
#include <sstream>
#include <dlfcn.h>
#include <stdarg.h>

#include <syncevo/declarations.h>

namespace {

std::string lookupDebug, lookupInfo;

}

int EDSAbiHaveEbook, EDSAbiHaveEcal, EDSAbiHaveEdataserver;

#ifdef EVOLUTION_COMPATIBILITY

struct EDSAbiWrapper EDSAbiWrapperSingleton;

namespace {

/**
 * Opens a <basename>.<num> shared object with <num> coming from a
 * range of known compatible versions, falling back to even more
 * recent ones only after warning about it. Then searches for
 * function pointers.
 *
 * Either all or none of the function pointers are set.
 *
 * End user information and debug information are added to
 * lookupDebug and lookupInfo.
 *
 * @param libname   full name including .so suffix; .<num> gets appended
 * @param minver    first known compatible version
 * @param maxver    last known compatible version
 * @return dlhandle which must be kept or freed by caller
 */
void *findSymbols(const char *libname, int minver, int maxver, ... /* function pointer address, name, ..., (void *)0 */)
{
    void *dlhandle = NULL;
    std::ostringstream debug, info;

    if (!dlhandle) {
        for (int ver = maxver;
             ver >= minver;
             --ver) {
            std::ostringstream soname;
            soname << libname << "." << ver;
            dlhandle = dlopen(soname.str().c_str(), RTLD_GLOBAL|RTLD_LAZY);
            if (dlhandle) {
                info << "using " << soname.str() << std::endl;
                break;
            }
        }
    }

    if (!dlhandle) {
        for (int ver = maxver + 1;
             ver < maxver + 50;
             ++ver) {
            std::ostringstream soname;
            soname << libname << "." << ver;
            dlhandle = dlopen(soname.str().c_str(), RTLD_GLOBAL|RTLD_LAZY);
            if (dlhandle) {
                info << "using " << soname.str() << " - might not be compatible!" << std::endl;
                break;
            }
        }
    }
    
    if (!dlhandle) {
        debug << libname << " not found (tried major versions " << minver << " to " << maxver + 49 << ")" << std::endl;
    } else {
        bool allfound = true;

        va_list ap;
        va_start(ap, maxver);
        void **funcptr = va_arg(ap, void **);
        const char *symname = NULL;
        while (funcptr && allfound) {
            symname = va_arg(ap, const char *);
            *funcptr = dlsym(dlhandle, symname);
            if (!*funcptr) {
                debug << symname << " not found" << std::endl;
                allfound = false;
            }
            funcptr = va_arg(ap, void **);
        }
        va_end(ap);

        if (!allfound) {
            /* unusable, clear symbols and free handle */
            va_start(ap, maxver);
            funcptr = va_arg(ap, void **);
            while (funcptr) {
                va_arg(ap, const char *);
                *funcptr = NULL;
                funcptr = va_arg(ap, void **);
            }
            va_end(ap);

            info << libname << " unusable, required function no longer available" << std::endl;
            dlclose(dlhandle);
            dlhandle = NULL;
        }
    }

    lookupInfo += info.str();
    lookupDebug += info.str();
    lookupDebug += debug.str();
    return dlhandle;
}

# ifdef HAVE_EDS
    void *edshandle;
# endif
# ifdef ENABLE_EBOOK
    void *ebookhandle;
# endif
# ifdef ENABLE_ECAL
    void *ecalhandle;
# endif

}

#endif // EVOLUTION_COMPATIBILITY

extern "C" int EDSAbiHaveEbook, EDSAbiHaveEcal, EDSAbiHaveEdataserver;

extern "C" void EDSAbiWrapperInit()
{
    static bool initialized;

    if (initialized) {
        return;
    } else {
        initialized = true;
    }

#ifdef EVOLUTION_COMPATIBILITY
# ifdef HAVE_EDS
    edshandle =
    findSymbols("libedataserver-1.2.so", 7, 11,
                &EDSAbiWrapperSingleton.e_source_get_type, "e_source_get_type",
                &EDSAbiWrapperSingleton.e_source_get_uri, "e_source_get_uri",
                &EDSAbiWrapperSingleton.e_source_group_get_type, "e_source_group_get_type",
                &EDSAbiWrapperSingleton.e_source_group_peek_sources, "e_source_group_peek_sources",
                &EDSAbiWrapperSingleton.e_source_list_peek_groups, "e_source_list_peek_groups",
                &EDSAbiWrapperSingleton.e_source_peek_name, "e_source_peek_name",
                (void *)0);
    EDSAbiHaveEdataserver = EDSAbiWrapperSingleton.e_source_group_peek_sources != 0;
# endif // HAVE_EDS

# ifdef ENABLE_EBOOK
    ebookhandle =
    findSymbols("libebook-1.2.so", 5, 9,
                &EDSAbiWrapperSingleton.e_book_add_contact, "e_book_add_contact",
                &EDSAbiWrapperSingleton.e_book_authenticate_user, "e_book_authenticate_user",
                &EDSAbiWrapperSingleton.e_book_commit_contact, "e_book_commit_contact",
                &EDSAbiWrapperSingleton.e_contact_duplicate, "e_contact_duplicate",
                &EDSAbiWrapperSingleton.e_contact_get_const, "e_contact_get_const",
                &EDSAbiWrapperSingleton.e_contact_get, "e_contact_get",
                &EDSAbiWrapperSingleton.e_contact_name_free, "e_contact_name_free",
                &EDSAbiWrapperSingleton.e_contact_get_type, "e_contact_get_type",
                &EDSAbiWrapperSingleton.e_contact_new_from_vcard, "e_contact_new_from_vcard",
                &EDSAbiWrapperSingleton.e_contact_set, "e_contact_set",
                &EDSAbiWrapperSingleton.e_book_error_quark, "e_book_error_quark",
                &EDSAbiWrapperSingleton.e_book_get_addressbooks, "e_book_get_addressbooks",
                &EDSAbiWrapperSingleton.e_book_get_changes, "e_book_get_changes",
                &EDSAbiWrapperSingleton.e_book_get_contact, "e_book_get_contact",
                &EDSAbiWrapperSingleton.e_book_get_contacts, "e_book_get_contacts",
                &EDSAbiWrapperSingleton.e_book_get_supported_auth_methods, "e_book_get_supported_auth_methods",
                &EDSAbiWrapperSingleton.e_book_get_uri, "e_book_get_uri",
                &EDSAbiWrapperSingleton.e_book_new, "e_book_new",
                &EDSAbiWrapperSingleton.e_book_new_default_addressbook, "e_book_new_default_addressbook",
                &EDSAbiWrapperSingleton.e_book_new_from_uri, "e_book_new_from_uri",
                &EDSAbiWrapperSingleton.e_book_new_system_addressbook, "e_book_new_system_addressbook",
                &EDSAbiWrapperSingleton.e_book_open, "e_book_open",
                &EDSAbiWrapperSingleton.e_book_query_any_field_contains, "e_book_query_any_field_contains",
                &EDSAbiWrapperSingleton.e_book_query_unref, "e_book_query_unref",
                &EDSAbiWrapperSingleton.e_book_remove_contact, "e_book_remove_contact",
                &EDSAbiWrapperSingleton.e_vcard_to_string, "e_vcard_to_string",
                (void *)0);
    EDSAbiHaveEbook = EDSAbiWrapperSingleton.e_book_new != 0;
# endif // ENABLE_EBOOK

# ifdef ENABLE_ECAL
    ecalhandle =
    findSymbols("libecal-1.2.so", 3, 7,
                &EDSAbiWrapperSingleton.e_cal_add_timezone, "e_cal_add_timezone",
                &EDSAbiWrapperSingleton.e_cal_component_get_icalcomponent, "e_cal_component_get_icalcomponent",
                &EDSAbiWrapperSingleton.e_cal_component_get_last_modified, "e_cal_component_get_last_modified",
                &EDSAbiWrapperSingleton.e_cal_component_get_type, "e_cal_component_get_type",
                &EDSAbiWrapperSingleton.e_cal_create_object, "e_cal_create_object",
                &EDSAbiWrapperSingleton.e_calendar_error_quark, "e_calendar_error_quark",
                &EDSAbiWrapperSingleton.e_cal_get_component_as_string, "e_cal_get_component_as_string",
                &EDSAbiWrapperSingleton.e_cal_get_object, "e_cal_get_object",
                &EDSAbiWrapperSingleton.e_cal_get_object_list_as_comp, "e_cal_get_object_list_as_comp",
                &EDSAbiWrapperSingleton.e_cal_get_sources, "e_cal_get_sources",
                &EDSAbiWrapperSingleton.e_cal_get_timezone, "e_cal_get_timezone",
                &EDSAbiWrapperSingleton.e_cal_modify_object, "e_cal_modify_object",
                &EDSAbiWrapperSingleton.e_cal_new, "e_cal_new",
                &EDSAbiWrapperSingleton.e_cal_new_from_uri, "e_cal_new_from_uri",
                &EDSAbiWrapperSingleton.e_cal_new_system_calendar, "e_cal_new_system_calendar",
                &EDSAbiWrapperSingleton.e_cal_new_system_tasks, "e_cal_new_system_tasks",
                &EDSAbiWrapperSingleton.e_cal_get_uri, "e_cal_get_uri",
                &EDSAbiWrapperSingleton.e_cal_open, "e_cal_open",
                &EDSAbiWrapperSingleton.e_cal_remove_object, "e_cal_remove_object",
                &EDSAbiWrapperSingleton.e_cal_remove_object_with_mod, "e_cal_remove_object_with_mod",
                &EDSAbiWrapperSingleton.e_cal_set_auth_func, "e_cal_set_auth_func",
                &EDSAbiWrapperSingleton.icalcomponent_add_component, "icalcomponent_add_component",
                &EDSAbiWrapperSingleton.icalcomponent_as_ical_string, "icalcomponent_as_ical_string",
                &EDSAbiWrapperSingleton.icalcomponent_free, "icalcomponent_free",
                &EDSAbiWrapperSingleton.icalcomponent_get_first_component, "icalcomponent_get_first_component",
                &EDSAbiWrapperSingleton.icalcomponent_get_first_property, "icalcomponent_get_first_property",
                &EDSAbiWrapperSingleton.icalcomponent_get_next_component, "icalcomponent_get_next_component",
                &EDSAbiWrapperSingleton.icalcomponent_get_next_property, "icalcomponent_get_next_property",
                &EDSAbiWrapperSingleton.icalcomponent_get_recurrenceid, "icalcomponent_get_recurrenceid",
                &EDSAbiWrapperSingleton.icalcomponent_get_timezone, "icalcomponent_get_timezone",
                &EDSAbiWrapperSingleton.icalcomponent_get_location, "icalcomponent_get_location",
                &EDSAbiWrapperSingleton.icalcomponent_get_summary, "icalcomponent_get_summary",
                &EDSAbiWrapperSingleton.icalcomponent_get_uid, "icalcomponent_get_uid",
                &EDSAbiWrapperSingleton.icalcomponent_isa, "icalcomponent_isa",
                &EDSAbiWrapperSingleton.icalcomponent_new_clone, "icalcomponent_new_clone",
                &EDSAbiWrapperSingleton.icalcomponent_new_from_string, "icalcomponent_new_from_string",
                &EDSAbiWrapperSingleton.icalcomponent_remove_property, "icalcomponent_remove_property",
                &EDSAbiWrapperSingleton.icalcomponent_set_uid, "icalcomponent_set_uid",
                &EDSAbiWrapperSingleton.icalcomponent_vanew, "icalcomponent_vanew",
                &EDSAbiWrapperSingleton.icalparameter_get_tzid, "icalparameter_get_tzid",
                &EDSAbiWrapperSingleton.icalparameter_set_tzid, "icalparameter_set_tzid",
                &EDSAbiWrapperSingleton.icalproperty_get_description, "icalproperty_get_description",
                &EDSAbiWrapperSingleton.icalproperty_get_first_parameter, "icalproperty_get_first_parameter",
                &EDSAbiWrapperSingleton.icalproperty_get_lastmodified, "icalproperty_get_lastmodified",
                &EDSAbiWrapperSingleton.icalproperty_get_next_parameter, "icalproperty_get_next_parameter",
                &EDSAbiWrapperSingleton.icalproperty_get_summary, "icalproperty_get_summary",
                &EDSAbiWrapperSingleton.icalproperty_new_description, "icalproperty_new_description",
                &EDSAbiWrapperSingleton.icalproperty_new_summary, "icalproperty_new_summary",
                &EDSAbiWrapperSingleton.icalproperty_set_value_from_string, "icalproperty_set_value_from_string",
                &EDSAbiWrapperSingleton.icaltime_as_ical_string, "icaltime_as_ical_string",
                &EDSAbiWrapperSingleton.icaltimezone_free, "icaltimezone_free",
                &EDSAbiWrapperSingleton.icaltimezone_get_builtin_timezone, "icaltimezone_get_builtin_timezone",
                &EDSAbiWrapperSingleton.icaltimezone_get_builtin_timezone_from_tzid, "icaltimezone_get_builtin_timezone_from_tzid",
                &EDSAbiWrapperSingleton.icaltimezone_get_component, "icaltimezone_get_component",
                &EDSAbiWrapperSingleton.icaltimezone_get_tzid, "icaltimezone_get_tzid",
                &EDSAbiWrapperSingleton.icaltimezone_new, "icaltimezone_new",
                &EDSAbiWrapperSingleton.icaltimezone_set_component, "icaltimezone_set_component",
                (void *)0);
    EDSAbiHaveEcal = EDSAbiWrapperSingleton.e_cal_new != 0;
# endif // ENABLE_ECAL
#else // EVOLUTION_COMPATIBILITY
# ifdef HAVE_EDS
    EDSAbiHaveEdataserver = true;
# endif
# ifdef ENABLE_EBOOK
    EDSAbiHaveEbook = true;
# endif
# ifdef ENABLE_ECAL
    EDSAbiHaveEcal = true;
# endif
#endif // EVOLUTION_COMPATIBILITY
}

extern "C" const char *EDSAbiWrapperInfo() { return lookupInfo.c_str(); }
extern "C" const char *EDSAbiWrapperDebug() { return lookupDebug.c_str(); }

