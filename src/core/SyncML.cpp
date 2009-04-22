/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 */

#include "SyncML.h"
#include <sstream>
#include <iomanip>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>


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

SyncMode StringToSyncMode(const std::string &mode)
{
    if (boost::iequals(mode, "slow") || boost::iequals(mode, "SYNC_SLOW")) {
        return SYNC_SLOW;
    } else if (boost::iequals(mode, "two-way") || boost::iequals(mode, "SYNC_TWO_WAY")) {
        return SYNC_TWO_WAY;
    } else if (boost::iequals(mode, "refresh-from-server") || boost::iequals(mode, "SYNC_REFRESH_FROM_SERVER")) {
        return SYNC_REFRESH_FROM_SERVER;
    } else if (boost::iequals(mode, "refresh-from-client") || boost::iequals(mode, "SYNC_REFRESH_FROM_CLIENT")) {
        return SYNC_REFRESH_FROM_CLIENT;
    } else if (boost::iequals(mode, "one-way-from-server") || boost::iequals(mode, "SYNC_ONE_WAY_FROM_SERVER")) {
        return SYNC_REFRESH_FROM_SERVER;
    } else if (boost::iequals(mode, "one-way-from-client") || boost::iequals(mode, "SYNC_ONE_WAY_FROM_CLIENT")) {
        return SYNC_REFRESH_FROM_CLIENT;
    } else if (boost::iequals(mode, "disabled") || boost::iequals(mode, "SYNC_NONE")) {
        return SYNC_NONE;
    } else {
        return SYNC_MODE_MAX;
    }
}


namespace {
    const char * const locNames[] = { "local", "remote", NULL };
    const char * const stateNames[] = { "added", "updated", "removed", "any", NULL };
    const char * const resultNames[] = { "total", "reject", "match",
                                         "conflict_server_won",
                                         "conflict_client_won",
                                         "conflict_duplicated",
                                         "sent",
                                         "received",
                                         NULL };

    int toIndex(const char * const names[],
                const std::string &name) {
        int i;
        for (i = 0;
             names[i] && name != names[i];
             i++)
            {}
        return i;
    }
    std::string toString(const char * const names[],
                    int index) {
        for (int i = 0;
             names[i];
             i++) {
            if (i == index) {
                return names[i];
            }
        }
        return "unknown";
    }
}

std::string SyncSourceReport::LocationToString(ItemLocation location) { return toString(locNames, location); }
SyncSourceReport::ItemLocation SyncSourceReport::StringToLocation(const std::string &location) { return static_cast<ItemLocation>(toIndex(locNames, location)); }
std::string SyncSourceReport::StateToString(ItemState state) { return toString(stateNames, state); }
SyncSourceReport::ItemState SyncSourceReport::StringToState(const std::string &state) { return static_cast<ItemState>(toIndex(stateNames, state)); }
std::string SyncSourceReport::ResultToString(ItemResult result) { return toString(resultNames, result); }
SyncSourceReport::ItemResult SyncSourceReport::StringToResult(const std::string &result) { return static_cast<ItemResult>(toIndex(resultNames, result)); }

std::string SyncSourceReport::StatTupleToString(ItemLocation location, ItemState state, ItemResult result)
{
    return std::string("") +
        LocationToString(location) + "-" +
        StateToString(state) + "-" +
        ResultToString(result);
}
void SyncSourceReport::StringToStatTuple(const std::string &str, ItemLocation &location, ItemState &state, ItemResult &result)
{
    std::vector< std::string > tokens;
    boost::split(tokens, str, boost::is_any_of("-"));
    location = tokens.size() > 0 ? StringToLocation(tokens[0]) : ITEM_LOCATION_MAX;
    state = tokens.size() > 1 ? StringToState(tokens[1]) : ITEM_STATE_MAX;
    result = tokens.size() > 2 ? StringToResult(tokens[2]) : ITEM_RESULT_MAX;
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

        if (source.m_backupBefore.isAvailable() ||
            source.m_backupAfter.isAvailable()) {
            std::stringstream backup;
            backup << "item(s) in database backup: ";
            if (source.m_backupBefore.isAvailable()) {
                backup << source.m_backupBefore.getNumItems() << " before sync, ";
            } else {
                backup << "no backup before sync, ";
            }
            if (source.m_backupAfter.isAvailable()) {
                backup << source.m_backupAfter.getNumItems() << " after it";
            } else {
                backup << "no backup after it";
            }
            flushRight(out, backup.str());
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
