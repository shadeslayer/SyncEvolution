/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 */

#include "SyncML.h"
#include <sstream>
#include <iomanip>

#include <boost/foreach.hpp>

std::string PrettyPrintSyncMode(SyncMode mode, bool userVisible)
{
    switch (mode) {
    case SYNC_NONE:
        return userVisible ? "disabled" : "SYNC_NONE";
    case SYNC_TWO_WAY:
        return userVisible ? "two-way" : "SYNC_TWO_WAY";
    case SYNC_SLOW:
        return userVisible ? "slow" : "SYNC_SLOW";
    case SYNC_ONE_WAY_FROM_CLIENT:
        return userVisible ? "one-way-from-client" : "SYNC_ONE_WAY_FROM_CLIENT";
    case SYNC_REFRESH_FROM_CLIENT:
        return userVisible ? "refresh-from-client" : "SYNC_REFRESH_FROM_CLIENT";
    case SYNC_ONE_WAY_FROM_SERVER:
        return userVisible ? "one-way-from-server" : "SYNC_ONE_WAY_FROM_SERVER";
    case SYNC_REFRESH_FROM_SERVER:
        return userVisible ? "refresh-from-server" : "SYNC_REFRESH_FROM_SERVER";
    default:
        std::stringstream res;

        res << (userVisible ? "sync-mode-" : "SYNC_") << int(mode);
        return res.str();
    }
}

namespace {
    const int name_width = 18;
    const int number_width = 3;
    const int single_side_width = (number_width * 2 + 1) * 3 + 2;
    const int text_width = single_side_width * 2 + 3 + number_width + 1;

    std::ostream &flushRight(std::ostream &out, const std::string &line) {
        int spaces = name_width + 1 + text_width - line.size();
        if (spaces < 0) {
            spaces = 0;
        }
        out << "|" << std::left << std::setw(spaces) << "" <<
            line <<
            std::setw(0) << "" << " |\n";
        return out;
    }
}



std::ostream &operator << (std::ostream &out, const SyncReport &report)
{

    out << "+-------------------|-------ON CLIENT-------|-------ON SERVER-------|-CON-+\n";
    out << "|                   |    rejected / total   |    rejected / total   | FLI |\n";
    out << "|            Source |  NEW  |  MOD  |  DEL  |  NEW  |  MOD  |  DEL  | CTS |\n";
    const char *sep = 
        "+-------------------+-------+-------+-------+-------+-------+-------+-----+\n";
    out << sep;

    BOOST_FOREACH(const SyncReport::value_type &entry, report) {
        const std::string &name = entry.first;
        const SyncSourceReport &source = entry.second;
        out << "|" << std::right << std::setw(name_width) << name << " |";
        for (SyncSourceReport::ItemLocation location = SyncSourceReport::ITEM_LOCAL;
             location <= SyncSourceReport::ITEM_REMOTE;
             location = SyncSourceReport::ItemLocation(int(location) + 1)) {
            for (SyncSourceReport::ItemState state = SyncSourceReport::ITEM_ADDED;
                 state <= SyncSourceReport::ITEM_REMOVED;
                 state = SyncSourceReport::ItemState(int(state) + 1)) {
                out << std::right << std::setw(number_width) <<
                    source.getItemStat(location, state, SyncSourceReport::ITEM_REJECT);
                out << "/";
                out << std::left << std::setw(number_width) <<
                    source.getItemStat(location, state, SyncSourceReport::ITEM_TOTAL);
                out << "|";
            }
        }
        int total_conflicts =
            source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_CONFLICT_SERVER_WON) +
            source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_CONFLICT_CLIENT_WON) +
            source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_CONFLICT_DUPLICATED);
        out << std::right << std::setw(number_width + 1) << total_conflicts;
        out << " |\n";

        std::stringstream line;

        line <<
            PrettyPrintSyncMode(source.getFinalSyncMode()) << ", " <<
            source.getItemStat(SyncSourceReport::ITEM_LOCAL,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_SENT_BYTES) / 1024 <<
            " KB sent by client, " <<
            source.getItemStat(SyncSourceReport::ITEM_LOCAL,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_RECEIVED_BYTES) / 1024 <<
            " KB received";
        flushRight(out, line.str());

        if (total_conflicts > 0) {
            for (SyncSourceReport::ItemResult result = SyncSourceReport::ITEM_CONFLICT_SERVER_WON;
                 result <= SyncSourceReport::ITEM_CONFLICT_DUPLICATED;
                 result = SyncSourceReport::ItemResult(int(result) + 1)) {
                int count;
                if ((count = source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                                                SyncSourceReport::ITEM_ANY,
                                                result)) != 0 || true) {
                    std::stringstream line;
                    line << count << " " <<
                        (result == SyncSourceReport::ITEM_CONFLICT_SERVER_WON ? "client item(s) discarded" :
                         result == SyncSourceReport::ITEM_CONFLICT_CLIENT_WON ? "server item(s) discarded" :
                     "item(s) duplicated");

                    out << "|" << std::left << std::setw(name_width) << "" << " |" <<
                        std::right << std::setw(text_width - 1) << line.str() <<
                        " |\n";
                }
            }
        }

        int total_matched = source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                                               SyncSourceReport::ITEM_ANY,
                                               SyncSourceReport::ITEM_MATCH);
        if (total_matched) {
            out << "|" << std::left << std::setw(name_width) << "" << "| " << std::left <<
                std::setw(number_width) << total_matched <<
                std::setw(text_width - 1 - number_width) <<
                " item(s) matched" <<
                "|\n";
        }
    }
    out << sep;

    if (report.getStart()) {
        flushRight(out, report.formatSyncTimes());
        out << sep;
    }

    return out;
}

std::string SyncReport::formatSyncTimes() const
{
    std::stringstream out;
    time_t duration = m_end - m_start;

    out << "start ";
    if (!m_start) {
        out << "unknown";
    } else {
        char buffer[160];
        strftime(buffer, sizeof(buffer), "%c", localtime(&m_start));
        out << buffer;
        if (!m_end) {
            out << ", unknown duration (crashed?!)";
        } else {
            out << ", duration " << duration / 60 << ":"
                << std::setw(2) << std::setfill('0') << duration % 60
                << "min";
        }
    }
    return out.str();
}
