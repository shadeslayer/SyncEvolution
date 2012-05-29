/*
 * Copyright (C) 2012 Intel Corporation
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

#include <syncevo/UserInterface.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static bool CheckKeyring(const InitStateTri &keyring)
{
    // Default slot, registered with higher priority
    // than any other keyring backend. If we get here
    // no other backend was chosen by the keyring
    // property. If it is a string, then the string
    // must have been invalid or unsupported.
    if (keyring.wasSet() &&
        keyring.getValue() == InitStateTri::VALUE_STRING &&
        !keyring.get().empty()) {
        SE_THROW("Unsupported value for the \"keyring\" property, no such keyring found: " + keyring.get());
    }
    return false;
}

static bool PreventPlainText(const InitStateTri &keyring,
                             const std::string &passwordName)
{
    // Another slot, called after CheckKeyring when saving.
    // Ensures that if keyring was meant to be used and
    // couldn't be used, an error is throw instead of
    // silently storing as plain text password.
    if (keyring.getValue() != InitStateTri::VALUE_FALSE &&
        !keyring.get().empty()) {
        SE_THROW(StringPrintf("Cannot save %s as requested in %s."
                              "This SyncEvolution binary was compiled without support for storing "
                              "passwords in a keyring or wallet, or none of the backends providing that "
                              "functionality were usable. Either store passwords in your configuration "
                              "files or enter them interactively on each program run.\n",
                              passwordName.c_str(),
                              (keyring.getValue() == InitStateTri::VALUE_TRUE ||
                               keyring.get().empty()) ? "a secure keyring" :
                              keyring.get().c_str()));
    }
    return false;
}


LoadPasswordSignal &GetLoadPasswordSignal()
{
    static class Signal : public LoadPasswordSignal {
    public:
        Signal() {
            connect(100, boost::bind(CheckKeyring, _1));
        }
    } loadPasswordSignal;
    return loadPasswordSignal;
}

SavePasswordSignal &GetSavePasswordSignal()
{
    static class Signal : public SavePasswordSignal {
    public:
        Signal() {
            connect(100, boost::bind(CheckKeyring, _1));
            connect(101, boost::bind(PreventPlainText, _1, _2));
        }
    } savePasswordSignal;
    return savePasswordSignal;
}

void UserInterface::askPasswordAsync(const std::string &passwordName,
                                     const std::string &descr,
                                     const ConfigPasswordKey &key,
                                     const boost::function<void (const std::string &)> &success,
                                     const boost::function<void ()> &failureException)
{
    try {
        success(askPassword(passwordName, descr, key));
    } catch (...) {
        failureException();
    }
}


SE_END_CXX
