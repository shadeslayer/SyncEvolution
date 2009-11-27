/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include <syncevo/SyncML.h>
#include <syncevo/ConfigNode.h>
#include <syncevo/util.h>
#include <sstream>
#include <iomanip>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

std::string PrettyPrintSyncMode(SyncMode mode, bool userVisible)
{
    switch (mode) {
    case SYNC_NONE:
        return userVisible ? "disabled" : "SYNC_NONE";
    case SYNC_TWO_WAY:
    case SA_SYNC_TWO_WAY:
        return userVisible ? "two-way" : "SYNC_TWO_WAY";
    case SYNC_SLOW:
        return userVisible ? "slow" : "SYNC_SLOW";
    case SYNC_ONE_WAY_FROM_CLIENT:
    case SA_SYNC_ONE_WAY_FROM_CLIENT:
        return userVisible ? "one-way-from-client" : "SYNC_ONE_WAY_FROM_CLIENT";
    case SYNC_REFRESH_FROM_CLIENT:
    case SA_SYNC_REFRESH_FROM_CLIENT:
        return userVisible ? "refresh-from-client" : "SYNC_REFRESH_FROM_CLIENT";
    case SYNC_ONE_WAY_FROM_SERVER:
    case SA_SYNC_ONE_WAY_FROM_SERVER:
        return userVisible ? "one-way-from-server" : "SYNC_ONE_WAY_FROM_SERVER";
    case SYNC_REFRESH_FROM_SERVER:
    case SA_SYNC_REFRESH_FROM_SERVER:
        return userVisible ? "refresh-from-server" : "SYNC_REFRESH_FROM_SERVER";
    default:
        std::stringstream res;

        res << (userVisible ? "sync-mode-" : "SYNC_") << int(mode);
        return res.str();
    }
}

SyncMode StringToSyncMode(const std::string &mode, bool serverAlerted)
{
    if (boost::iequals(mode, "slow") || boost::iequals(mode, "SYNC_SLOW")) {
        if (serverAlerted) {
            //No server initiated slow sync, fallback to two way sync
            return SA_SYNC_TWO_WAY;
        }
        return SYNC_SLOW;
    } else if (boost::iequals(mode, "two-way") || boost::iequals(mode, "SYNC_TWO_WAY")) {
        return serverAlerted ?SA_SYNC_TWO_WAY: SYNC_TWO_WAY;
    } else if (boost::iequals(mode, "refresh-from-server") || boost::iequals(mode, "SYNC_REFRESH_FROM_SERVER")) {
        return serverAlerted? SA_SYNC_REFRESH_FROM_SERVER: SYNC_REFRESH_FROM_SERVER;
    } else if (boost::iequals(mode, "refresh-from-client") || boost::iequals(mode, "SYNC_REFRESH_FROM_CLIENT")) {
        return serverAlerted? SA_SYNC_REFRESH_FROM_CLIENT: SYNC_REFRESH_FROM_CLIENT;
    } else if (boost::iequals(mode, "one-way-from-server") || boost::iequals(mode, "SYNC_ONE_WAY_FROM_SERVER")) {
        return serverAlerted? SA_SYNC_ONE_WAY_FROM_SERVER: SYNC_ONE_WAY_FROM_SERVER;
    } else if (boost::iequals(mode, "one-way-from-client") || boost::iequals(mode, "SYNC_ONE_WAY_FROM_CLIENT")) {
        return serverAlerted? SA_SYNC_ONE_WAY_FROM_CLIENT: SYNC_ONE_WAY_FROM_CLIENT;
    } else if (boost::iequals(mode, "disabled") || boost::iequals(mode, "SYNC_NONE")) {
        return SYNC_NONE;
    } else {
        return SYNC_INVALID;
    }
}


ContentType StringToContentType(const std::string &type) {
    if (boost::iequals (type, "text/x-vcard") || boost::iequals (type, "text/x-vcard:2.1")) {
        return WSPCTC_XVCARD;
    } else if (boost::iequals (type, "text/vcard") ||boost::iequals (type, "text/vcard:3.0")) {
        return WSPCTC_VCARD;
    } else if (boost::iequals (type, "text/x-vcalendar") ||boost::iequals (type, "text/x-vcalendar:1.0")
              ||boost::iequals (type, "text/x-calendar") || boost::iequals (type, "text/x-calendar:1.0")) {
        return WSPCTC_XVCALENDAR;
    } else if (boost::iequals (type, "text/calendar") ||boost::iequals (type, "text/calendar:2.0")) {
        return WSPCTC_ICALENDAR;
    } else if (boost::iequals (type, "text/plain") ||boost::iequals (type, "text/plain:1.0")) {
        return WSPCTC_TEXT_PLAIN;
    } else {
        return WSPCTC_UNKNOWN;
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

std::ostream &operator << (std::ostream &out, const SyncReport &report)
{
    report.prettyPrint(out, 0);
    return out;
}

namespace {
    string fill(char sep, size_t width) {
        string res;
        res.resize(width - 1, sep);
        return res;
    }
    string center(char sep, const string &str, size_t width) {
        if (str.size() + 1 >= width) {
            return str;
        } else {
            string res;
            res.resize(width - 1, sep);
            res.replace((width - 1 - str.size()) / 2, str.size(), str);
            return res;
        }
    }
    string right(char sep, const string &str, size_t width) {
        if (str.size() + 1 >= width) {
            return str;
        } else {
            string res;
            res.resize(width - 1, sep);
            res.replace(width - 2 - str.size(), str.size(), str);
            return res;
        }
    }
    string left(char sep, const string &str, size_t width) {
        if (str.size() + 1 >= width) {
            return str;
        } else {
            string res;
            res.resize(width - 1, sep);
            res.replace(1, str.size(), str);
            return res;
        }
    }

    // insert string at column if it fits, otherwise flush right
    string align(char sep, const string &str, size_t width, size_t column) {
        if (column + str.size() + 1 >= width) {
            return right(sep, str, width);
        } else {
            string res;
            res.resize(width - 1, sep);
            res.replace(column, str.size(), str);
            return res;
        }
    }
}

void SyncReport::prettyPrint(std::ostream &out, int flags) const
{
    // table looks like this:
    // +-------------------|-------ON CLIENT---------------|-------ON SERVER-------|-CON-+
    // |                   |       rejected / total        |    rejected / total   | FLI |
    // |            Source |  NEW  |  MOD  |  DEL  | TOTAL |  NEW  |  MOD  |  DEL  | CTS |
    // +-------------------+-------+-------+-------+-------+-------+-------+-------+-----+
    //
    // Most of the columns can be turned on or off dynamically.
    // Their width is calculated once (including right separators and spaces):
    // | name_width        |count_width|                   |                       |conflict_width|
    //                     |client_width                   | server_width          |
    // | text_width                                                                      |

    // name column is sized dynamically based on column header and actual names
    size_t name_width = strlen("Source");
    BOOST_FOREACH(const SyncReport::value_type &entry, *this) {
        const std::string &name = entry.first;
        if (name_width < name.size()) {
            name_width = name.size();
        }
    }
    name_width += 1; // separator
    if (name_width < 20) {
        // enough room for spaces
        name_width += 2;
    }

    int count_width = 8;
    int client_width = (flags & WITHOUT_CLIENT) ? 0 :
        (flags & WITH_TOTAL) ? 4 * count_width :
        3 * count_width;
    int server_width = (flags & WITHOUT_SERVER) ? 0 :
        (flags & WITH_TOTAL) ? 4 * count_width :
        3 * count_width;
    int conflict_width = (flags & WITHOUT_CONFLICTS) ? 0 : 6;
    int text_width = name_width + client_width + server_width + conflict_width;

    if (text_width < 70) {
        // enlarge name column to make room for long lines of text
        name_width += 70 - text_width;
        text_width = 70;
    }

    out << "+" << fill('-', name_width);
    if (!(flags & WITHOUT_CLIENT)) {
        out << '|' << center('-', "ON CLIENT", client_width);
    }
    if (!(flags & WITHOUT_SERVER)) {
        out << '|' << center('-', "ON SERVER", server_width);
    }
    if (!(flags & WITHOUT_CONFLICTS)) {
        out << '|' << center('-', "CON", conflict_width);
    }
    out << "+\n";

    if (!(flags & WITHOUT_REJECTS) || !(flags & WITHOUT_CONFLICTS)) {
        out << "|" << fill(' ', name_width);
        string header = (flags & WITHOUT_REJECTS) ? "total" : "rejected / total";
        if (!(flags & WITHOUT_CLIENT)) {
            out << '|' << center(' ', header, client_width);
        }
        if (!(flags & WITHOUT_SERVER)) {
            out << '|' << center(' ', header, server_width);
        }
        if (!(flags & WITHOUT_CONFLICTS)) {
            out << '|' << center(' ', "FLI", conflict_width);
        }
        out << "|\n";
    }

    out << '|' << right(' ', "Source", name_width);
    if (!(flags & WITHOUT_CLIENT)) {
        out << '|' << center(' ', "NEW", count_width);
        out << '|' << center(' ', "MOD", count_width);
        out << '|' << center(' ', "DEL", count_width);
        if (flags & WITH_TOTAL) {
            out << '|' << center(' ', "TOTAL", count_width);
        }
    }
    if (!(flags & WITHOUT_SERVER)) {
        out << '|' << center(' ', "NEW", count_width);
        out << '|' << center(' ', "MOD", count_width);
        out << '|' << center(' ', "DEL", count_width);
        if (flags & WITH_TOTAL) {
            out << '|' << center(' ', "TOTAL", count_width);
        }
    }
    if (!(flags & WITHOUT_CONFLICTS)) {
        out << '|' << center(' ', "CTS", conflict_width);
    }
    out << "|\n";

    stringstream sepstream;
    sepstream << '+' << fill('-', name_width);
    if (!(flags & WITHOUT_CLIENT)) {
        sepstream << '+' << fill('-', count_width);
        sepstream << '+' << fill('-', count_width);
        sepstream << '+' << fill('-', count_width);
        if (flags & WITH_TOTAL) {
            sepstream << '+' << fill('-', count_width);
        }
    }
    if (!(flags & WITHOUT_SERVER)) {
        sepstream << '+' << fill('-', count_width);
        sepstream << '+' << fill('-', count_width);
        sepstream << '+' << fill('-', count_width);
        if (flags & WITH_TOTAL) {
            sepstream << '+' << fill('-', count_width);
        }
    }
    if (!(flags & WITHOUT_CONFLICTS)) {
        sepstream << '+' << fill('-', conflict_width);
    }
    sepstream << "+\n";
    string sep = sepstream.str();
    out << sep;

    BOOST_FOREACH(const SyncReport::value_type &entry, *this) {
        const std::string &name = entry.first;
        const SyncSourceReport &source = entry.second;
        out << '|' << right(' ', name, name_width);
        ssize_t name_column = name_width - 2 - name.size();
        if (name_column < 0) {
            name_column = 0;
        }
        for (SyncSourceReport::ItemLocation location =
                 ((flags & WITHOUT_CLIENT) ? SyncSourceReport::ITEM_REMOTE : SyncSourceReport::ITEM_LOCAL);
             location <= ((flags & WITHOUT_SERVER) ? SyncSourceReport::ITEM_LOCAL : SyncSourceReport::ITEM_REMOTE);
             location = SyncSourceReport::ItemLocation(int(location) + 1)) {
            for (SyncSourceReport::ItemState state = SyncSourceReport::ITEM_ADDED;
                 state <= ((flags & WITH_TOTAL) ? SyncSourceReport::ITEM_ANY : SyncSourceReport::ITEM_REMOVED);
                 state = SyncSourceReport::ItemState(int(state) + 1)) {
                stringstream count;
                if (!(flags & WITHOUT_REJECTS)) {
                    count << source.getItemStat(location, state, SyncSourceReport::ITEM_REJECT)
                          << '/';
                }
                count << source.getItemStat(location, state, SyncSourceReport::ITEM_TOTAL);
                out << '|' << center(' ', count.str(), count_width);
            }
        }

        int total_conflicts = 0;
        if (!(flags & WITHOUT_CONFLICTS)) {
            total_conflicts =
                source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                                   SyncSourceReport::ITEM_ANY,
                                   SyncSourceReport::ITEM_CONFLICT_SERVER_WON) +
                source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                                   SyncSourceReport::ITEM_ANY,
                                   SyncSourceReport::ITEM_CONFLICT_CLIENT_WON) +
                source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                                   SyncSourceReport::ITEM_ANY,
                                   SyncSourceReport::ITEM_CONFLICT_DUPLICATED);
            stringstream conflicts;
            conflicts << total_conflicts;
            out << '|' << center(' ', conflicts.str(), conflict_width);
        }
        out << "|\n";

        std::stringstream line;

        if (source.getFinalSyncMode() != SYNC_NONE ||
            source.getItemStat(SyncSourceReport::ITEM_LOCAL,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_SENT_BYTES) ||
            source.getItemStat(SyncSourceReport::ITEM_LOCAL,
                               SyncSourceReport::ITEM_ANY,
                               SyncSourceReport::ITEM_RECEIVED_BYTES)) {
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
            out << '|' << align(' ', line.str(), text_width, name_column) << "|\n";
        }

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

                    out << '|' << align(' ', line.str(), text_width, name_column) << "|\n";
                }
            }
        }

        int total_matched = source.getItemStat(SyncSourceReport::ITEM_REMOTE,
                                               SyncSourceReport::ITEM_ANY,
                                               SyncSourceReport::ITEM_MATCH);
        if (total_matched) {
            stringstream line;
            line << total_matched << " item(s) matched";
            out << '|' << align(' ', line.str(), text_width, name_column)
                << "|\n";
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
            out << '|' << align(' ', backup.str(), text_width, name_column) << "|\n";
        }
        out << sep;
    }

    if (getStart()) {
        out << '|' << center(' ', formatSyncTimes(), text_width) << "|\n";
    }
    if (getStatus()) {
        out << '|' << center(' ',
                             getStatus() != STATUS_HTTP_OK ?
                             StringPrintf("synchronization failed (status code %d)", static_cast<int>(getStatus())) :
                             "synchronization completed successfully",
                             text_width)
            << "|\n";
    }
    if (getStatus() || getStart()) {
        out << sep;
    }
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

ConfigNode &operator << (ConfigNode &node, const SyncReport &report)
{
    node.setProperty("start", static_cast<long>(report.getStart()));
    node.setProperty("end", static_cast<long>(report.getEnd()));
    node.setProperty("status", static_cast<int>(report.getStatus()));

    BOOST_FOREACH(const SyncReport::value_type &entry, report) {
        const std::string &name = entry.first;
        const SyncSourceReport &source = entry.second;

        string prefix = name;
        boost::replace_all(prefix, "_", "__");
        boost::replace_all(prefix, "-", "_+");
        prefix = "source-" + prefix;

        string key;
        key = prefix + "-mode";
        node.setProperty(key, PrettyPrintSyncMode(source.getFinalSyncMode()));
        key = prefix + "-first";
        node.setProperty(key, source.isFirstSync());
        key = prefix + "-resume";
        node.setProperty(key, source.isResumeSync());
        key = prefix + "-status";
        node.setProperty(key, static_cast<long>(source.getStatus()));
        key = prefix + "-backup-before";
        node.setProperty(key, source.m_backupBefore.getNumItems());
        key = prefix + "-backup-after";
        node.setProperty(key, source.m_backupAfter.getNumItems());

        for (int location = 0;
             location < SyncSourceReport::ITEM_LOCATION_MAX;
             location++) {
            for (int state = 0;
                 state < SyncSourceReport::ITEM_STATE_MAX;
                 state++) {
                for (int result = 0;
                     result < SyncSourceReport::ITEM_RESULT_MAX;
                     result++) {
                    int intval = source.getItemStat(SyncSourceReport::ItemLocation(location),
                                                    SyncSourceReport::ItemState(state),
                                                    SyncSourceReport::ItemResult(result));
                    if (intval) {
                        key = prefix + "-stat-" +
                            SyncSourceReport::StatTupleToString(SyncSourceReport::ItemLocation(location),
                                                                SyncSourceReport::ItemState(state),
                                                                SyncSourceReport::ItemResult(result));
                        node.setProperty(key, intval);
                    }
                }
            }
        }
    }

    return node;
}

ConfigNode &operator >> (ConfigNode &node, SyncReport &report)
{
    long ts;
    if (node.getProperty("start", ts)) {
        report.setStart(ts);
    }
    if (node.getProperty("end", ts)) {
        report.setEnd(ts);
    }
    int status;
    if (node.getProperty("status", status)) {
        report.setStatus(static_cast<SyncMLStatus>(status));
    }

    ConfigNode::PropsType props;
    node.readProperties(props);
    BOOST_FOREACH(const ConfigNode::PropsType::value_type &prop, props) {
        string key = prop.first;
        if (boost::starts_with(key, "source-")) {
            key.erase(0, strlen("source-"));
            size_t off = key.find('-');
            if (off != key.npos) {
                string sourcename = key.substr(0, off);
                boost::replace_all(sourcename, "_+", "-");
                boost::replace_all(sourcename, "__", "_");
                SyncSourceReport &source = report.getSyncSourceReport(sourcename);
                key.erase(0, off + 1);
                if (boost::starts_with(key, "stat-")) {
                    key.erase(0, strlen("stat-"));
                    SyncSourceReport::ItemLocation location;
                    SyncSourceReport::ItemState state;
                    SyncSourceReport::ItemResult result;
                    SyncSourceReport::StringToStatTuple(key, location, state, result);
                    stringstream in(prop.second);
                    int intval;
                    in >> intval;
                    source.setItemStat(location, state, result, intval);
                } else if (key == "mode") {
                    source.recordFinalSyncMode(StringToSyncMode(prop.second));
                } else if (key == "first") {
                    bool value;
                    if (node.getProperty(prop.first, value)) {
                        source.recordFirstSync(value);
                    }
                } else if (key == "resume") {
                    bool value;
                    if (node.getProperty(prop.first, value)) {
                        source.recordResumeSync(value);
                    }
                } else if (key == "status") {
                    long value;
                    if (node.getProperty(prop.first, value)) {
                        source.recordStatus(static_cast<SyncMLStatus>(value));
                    }
                } else if (key == "backup-before") {
                    long value;
                    if (node.getProperty(prop.first, value)) {
                        source.m_backupBefore.setNumItems(value);
                    }
                } else if (key == "backup-after") {
                    long value;
                    if (node.getProperty(prop.first, value)) {
                        source.m_backupAfter.setNumItems(value);
                    }
                }
            }
        }
    }

    return node;
}

SE_END_CXX
