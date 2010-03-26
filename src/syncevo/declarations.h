/*
 * Copyright (C) 2009 Intel Corporation
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

/**
 * Header file that is included by all SyncEvolution files.
 * Sets up the "SyncEvo" namespace.
 */

#ifndef INCL_DECLARATIONS
#define INCL_DECLARATIONS

#define SE_BEGIN_CXX namespace SyncEvo {
#define SE_END_CXX }

SE_BEGIN_CXX
/*
 * SyncEvolution should never use standard IO directly. Either use the
 * logging facilities or use variables that point towards the real
 * output channels.  In particular the command line code then can be
 * run as pointing towards real std::cout, a string stream, or redirected
 * via D-Bus.
 *
 * These dummy declarations trip up code inside SyncEvo namespace or using it
 * which use plain "cout << something" after a "using namespace std".
 * They don't help catching code which references std::cout.
 */
struct DontUseStandardIO;
extern DontUseStandardIO *cout;
extern DontUseStandardIO *cerr;
SE_END_CXX

#endif /** INCL_DECLARATIONS */
