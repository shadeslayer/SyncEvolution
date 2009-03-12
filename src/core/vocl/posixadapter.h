/*
 * Copyright (C) 2005-2006 Funambol
 * Author Patrick Ohly
 */

#ifndef INCL_POSIX_ADAPTER
#define INCL_POSIX_ADAPTER


/*
 * POSIX environment, configured and compiled with automake/autoconf
 */

#include <config.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include <string>
#include <cwchar>
#include <iostream>

// For ntoh functions
#include <netinet/in.h>

// Cygwin version of gcc does have these builtin
#ifndef __CYGWIN__
# define __declspec(x)
# define __cdecl
#endif

//#ifdef ENABLE_NLS
//# include <libintl.h>
//# define TEXT(String) gettext (String)
//#else
//# define TEXT(String) (String)
//# endif
#define TEXT(_x) _x
#define CHR(_x)  _x
#define T(_x) _x

#define EXTRA_SECTION_00
#define EXTRA_SECTION_01
#define EXTRA_SECTION_02
#define EXTRA_SECTION_03
#define EXTRA_SECTION_04
#define EXTRA_SECTION_05
#define EXTRA_SECTION_06

#define BOOL int
#define TRUE 1
#define FALSE 0

#define SYNC4J_LINEBREAK "\n"

/* map wchar_t and its functions back to standard functions */
#undef wchar_t
#define wchar_t char
#undef BCHAR
typedef char BCHAR;
typedef char WCHAR;

#define bsprintf sprintf 

#define bstrlen strlen
#define bstrcpy strcpy
#define bstrcat strcat
#define bstrstr strstr
#define bstrchr strchr
#define bstrrchr strrchr
#define bscanf scanf
#define bstrcmp strcmp
#define bstricmp _stricmp
#define bstrncpy strncpy
#define bstrncmp strncmp
#define bstrtol strtol
#define bstrtoul strtoul

#define wsprintf sprintf
#define _wfopen fopen
#define wprintf printf
#define fwprintf fprintf
#define wsprintf sprintf
#define swprintf sprintf
#define wcscpy strcpy
#define wcsncpy strncpy
#define wcsncmp strncmp
#define wcslen strlen
#define wcstol strtol
#define wcstoul strtoul
#define wcsstr strstr
#define wcscmp strcmp
#define wcstok strtok
inline char towlower(char x) { return tolower(x); }
inline char towupper(char x) { return toupper(x); }
#define wmemmove memmove
#define wmemcpy memcpy
#define wmemcmp memcmp
#define wmemset memset
#define wcschr strchr
#define wcsrchr strrchr
#define wcscat strcat
#define wcsncat strncat
#define _wtoi atoi
#define wcstod strtod
#define wcsicmp strcasecmp
#define _wcsicmp strcasecmp
#define _stricmp strcasecmp

/* some of the code compares NULL against integers, which
   fails if NULL is defined as (void *)0 */
#undef NULL
#define NULL 0

template <class T> T min(T x, T y) { return x < y ? x : y; }
template <class T> T max(T x, T y) { return x > y ? x : y; }

#endif

