/*
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include "config.h"

#ifdef ENABLE_ADDRESSBOOK

#include <CoreFoundation/CoreFoundation.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * constants missing from AddressBook framework on iPhone: use strings
 * as found in SQLite database on iPhone
 *
 * kABTitleProperty is also missing, but the ABRecord constants which
 * are CFStrings on Mac OS seem to be numeric constants on the iPhone so
 * we cannot guess it might be (if it exists at all), so not supported.
 */

#ifdef __arm__
// CFStringRef kABCAIMInstantProperty;
CFStringRef kABCAddressCityKey;
CFStringRef kABCAddressCountryKey;
CFStringRef kABCAddressHomeLabel;
CFStringRef kABCAddressStateKey;
CFStringRef kABCAddressStreetKey;
CFStringRef kABCAddressWorkLabel;
CFStringRef kABCAddressZIPKey;
CFStringRef kABCAssistantLabel;
CFStringRef kABCEmailHomeLabel;
CFStringRef kABCEmailWorkLabel;
CFStringRef kABCHomePageLabel;
// CFStringRef kABCHomePageProperty;
// CFStringRef kABCICQInstantProperty;
CFStringRef kABCJabberHomeLabel;
// CFStringRef kABCJabberInstantProperty;
CFStringRef kABCJabberWorkLabel;
// CFStringRef kABCMSNInstantProperty;
CFStringRef kABCManagerLabel;
// CFStringRef kABCOtherDatesProperty;
CFStringRef kABCPhoneHomeFAXLabel;
CFStringRef kABCPhoneHomeLabel;
CFStringRef kABCPhoneMainLabel;
CFStringRef kABCPhoneMobileLabel;
CFStringRef kABCPhonePagerLabel;
CFStringRef kABCPhoneWorkFAXLabel;
CFStringRef kABCPhoneWorkLabel;
CFStringRef kABCSpouseLabel;
// CFStringRef kABCTitleProperty;
// CFStringRef kABCURLsProperty;
// CFStringRef kABCYahooInstantProperty;
#endif

class constants {
public:
    constants() {
#ifdef __arm__
        // kABCAIMInstantProperty = CFStringCreateWithCString(NULL, "AIMInstant", kCFStringEncodingUTF8);
        kABCAddressCityKey = CFStringCreateWithCString(NULL, "City", kCFStringEncodingUTF8);
        kABCAddressCountryKey = CFStringCreateWithCString(NULL, "Country", kCFStringEncodingUTF8);
        kABCAddressHomeLabel = CFStringCreateWithCString(NULL, "_$!<Home>!$_", kCFStringEncodingUTF8);
        kABCAddressStateKey = CFStringCreateWithCString(NULL, "State", kCFStringEncodingUTF8);
        kABCAddressStreetKey = CFStringCreateWithCString(NULL, "Street", kCFStringEncodingUTF8);
        kABCAddressWorkLabel = CFStringCreateWithCString(NULL, "_$!<Work>!$_", kCFStringEncodingUTF8);
        kABCAddressZIPKey = CFStringCreateWithCString(NULL, "ZIP", kCFStringEncodingUTF8);
        kABCAssistantLabel = CFStringCreateWithCString(NULL, "_$!<Assistant>!$_", kCFStringEncodingUTF8);
        kABCEmailHomeLabel = CFStringCreateWithCString(NULL, "_$!<Home>!$_", kCFStringEncodingUTF8);
        kABCEmailWorkLabel = CFStringCreateWithCString(NULL, "_$!<Work>!$_", kCFStringEncodingUTF8);
        kABCHomePageLabel = CFStringCreateWithCString(NULL, "_$!<HomePage>!$_", kCFStringEncodingUTF8);
        // kABCHomePageProperty = CFStringCreateWithCString(NULL, "HomePage", kCFStringEncodingUTF8);
        // kABCICQInstantProperty = CFStringCreateWithCString(NULL, "ICQInstant", kCFStringEncodingUTF8);
        kABCJabberHomeLabel = CFStringCreateWithCString(NULL, "_$!<Home>!$_", kCFStringEncodingUTF8);
        // kABCJabberInstantProperty = CFStringCreateWithCString(NULL, "JabberInstant", kCFStringEncodingUTF8);
        kABCJabberWorkLabel = CFStringCreateWithCString(NULL, "_$!<Work>!$_", kCFStringEncodingUTF8);
        // kABCMSNInstantProperty = CFStringCreateWithCString(NULL, "MSNInstant", kCFStringEncodingUTF8);
        kABCManagerLabel = CFStringCreateWithCString(NULL, "_$!<Manager>!$_", kCFStringEncodingUTF8);
        // kABCOtherDatesProperty = CFStringCreateWithCString(NULL, "ABDate", kCFStringEncodingUTF8);
        kABCPhoneHomeFAXLabel = CFStringCreateWithCString(NULL, "_$!<HomeFAX>!$_", kCFStringEncodingUTF8);
        kABCPhoneHomeLabel = CFStringCreateWithCString(NULL, "_$!<Home>!$_", kCFStringEncodingUTF8);
        kABCPhoneMainLabel = CFStringCreateWithCString(NULL, "_$!<Main>!$_", kCFStringEncodingUTF8);
        kABCPhoneMobileLabel = CFStringCreateWithCString(NULL, "_$!<Mobile>!$_", kCFStringEncodingUTF8);
        kABCPhonePagerLabel = CFStringCreateWithCString(NULL, "_$!<Pager>!$_", kCFStringEncodingUTF8);
        kABCPhoneWorkFAXLabel = CFStringCreateWithCString(NULL, "_$!<WorkFAX>!$_", kCFStringEncodingUTF8);
        kABCPhoneWorkLabel = CFStringCreateWithCString(NULL, "_$!<Work>!$_", kCFStringEncodingUTF8);
        kABCSpouseLabel = CFStringCreateWithCString(NULL, "_$!<Spouse>!$_", kCFStringEncodingUTF8);
        // kABCTitleProperty = CFStringCreateWithCString(NULL, "Title", kCFStringEncodingUTF8);
        // kABCURLsProperty = CFStringCreateWithCString(NULL, "URLs", kCFStringEncodingUTF8);
        // kABCYahooInstantProperty = CFStringCreateWithCString(NULL, "YahooInstant", kCFStringEncodingUTF8);
#endif

#if 0
#define printconstant(_x) printf(#_x ": %s\n", CFString2Std((CFStringRef)_x).c_str())
       printconstant(kABAIMInstantProperty);
       printconstant(kABAddressCityKey);
       printconstant(kABAddressCountryKey);
       printconstant(kABAddressHomeLabel);
       printconstant(kABAddressStateKey);
       printconstant(kABAddressStreetKey);
       printconstant(kABAddressWorkLabel);
       printconstant(kABAddressZIPKey);
       printconstant(kABAssistantLabel);
       printconstant(kABEmailHomeLabel);
       printconstant(kABEmailWorkLabel);
       printconstant(kABHomePageLabel);
       printconstant(kABHomePageProperty);
       printconstant(kABICQInstantProperty);
       printconstant(kABJabberHomeLabel);
       printconstant(kABJabberInstantProperty);
       printconstant(kABJabberWorkLabel);
       printconstant(kABMSNInstantProperty);
       printconstant(kABManagerLabel);
       printconstant(kABOtherDatesProperty);
       printconstant(kABPhoneHomeFAXLabel);
       printconstant(kABPhoneHomeLabel);
       printconstant(kABPhoneMainLabel);
       printconstant(kABPhoneMobileLabel);
       printconstant(kABPhonePagerLabel);
       printconstant(kABPhoneWorkFAXLabel);
       printconstant(kABPhoneWorkLabel);
       printconstant(kABSpouseLabel);
       // printconstant(kABTitleProperty);
       printconstant(kABURLsProperty);
       printconstant(kABYahooInstantProperty);
#endif
    }
} constants;

SE_END_CXX

#endif // ENABLE_ADDRESSBOOK
