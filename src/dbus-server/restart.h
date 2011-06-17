/*
 * Copyright (C) 2011 Intel Corporation
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

#ifndef RESTART_H
#define RESTART_H

#include <vector>
#include <string>

#include <errno.h>

#include <syncevo/LogRedirect.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Encapsulates startup environment from main() and can do execve()
 * with it later on. Assumes that argv[0] is the executable to run.
 */
class Restart
{
    std::vector<std::string> m_argv;
    std::vector<std::string> m_env;

    void saveArray(std::vector<std::string> &array, char **p)
    {
        while(*p) {
            array.push_back(*p);
            p++;
        }
    }

    const char **createArray(const std::vector<std::string> &array)
    {
        const char **res = new const char *[(array.size() + 1)];
        size_t i;
        for (i = 0; i < array.size(); i++) {
            res[i] = array[i].c_str();
        }
        res[i] = NULL;
        return res;
    }

public:
    Restart(char **argv, char **env)
    {
        saveArray(m_argv, argv);
        saveArray(m_env, env);
    }

    void restart()
    {
        const char **argv = createArray(m_argv);
        const char **env = createArray(m_env);
        LogRedirect::reset();
        if (execve(argv[0], (char *const *)argv, (char *const *)env)) {
            SE_THROW(StringPrintf("restarting syncevo-dbus-server failed: %s", strerror(errno)));
        }
    }
};

SE_END_CXX

#endif // RESTART_H
