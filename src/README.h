/*
 * Copyright (C) 2008 Patrick Ohly <patrick.ohly@gmx.de>
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
 * @mainpage Getting Started
 *
 * This documentation for SyncEvolution and the Funambol C++ Client
 * API was generated automatically from the source code.
 *
 * While most of the classes in SyncEvolution are documented, very
 * little effort was spent on organizing this information in a coherent
 * way. If you are a developer who wants to write a SyncML client based
 * on the SyncEvolution framework, then you should have a look at
 * the following classes:
 * - TrackingSyncSource is the most convenient class to derive from.
 * - EvolutionSyncSource is a bit more general.
 * - RegisterSyncSource adds additional sources to the framework.
 *
 * The following classes help with testing your derived classes:
 * - RegisterSyncSourceTest is what you have to use.
 * - TestEvolution uses that information.
 * - ClientTest, LocalTests, SyncTests are used by TestEvolution.
 *
 * The FileSyncSource is a good example to get started.
 */
