// GTFS Fares v1 (fare_attributes.txt + fare_rules.txt) pricing for rides and journeys.
//
//      build() reads both files once per load (they're small, so there are no sorted copies) and
//      groups each fare_id's rules the way OpenTripPlanner does: its origin/destination pairs,
//      routes and contains zones each become one set. price() then splits a journey's rides
//      into tickets: one ticket covers consecutive rides when a fare matches all of them (every
//      route allowed, first origin zone -> last destination zone, exactly the zones passed
//      through, the transfer count and transfer_duration), and the cheapest split wins.

#ifndef FARES_HPP
#define FARES_HPP

#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
#include "fast-gtfs.hpp"

namespace fares {

struct fare_option {
    string fare_id, currency, agency_id;
    double price = 0;
    int payment_method = 0;          // 0 on board, 1 before boarding
    int transfers = -1;              // -1 unlimited (an empty transfers field)
    int transfer_duration = -1;      // seconds from the first boarding to the last, -1 for no limit
    vector<pair<string, string>> od; // origin_id, destination_id; "" matches any zone
    std::set<string> routes;         // empty: any route
    std::set<string> contains;       // empty: no contains rule
};

struct index {
    vector<fare_option> fares;
    std::unordered_map<string, string> stopZone;    // stop_id -> zone_id
    std::unordered_map<string, string> routeAgency; // route_id -> agency_id
    std::unordered_map<string, vector<int>> byOrigin; // origin_id ("" for any) -> fares with an origin/destination rule from it
    vector<int> anyOrigin;           // fares with no origin/destination rule
    bool available = false;          // false: no fare files, or every price is 0 (placeholder fares, e.g. TTC's)
    bool usesContains = false;       // only then do callers need every zone a ride passes through

    const string& zoneOf(const string& stop_id) const {
        static const string none;
        auto find = stopZone.find(stop_id);
        return find == stopZone.end() ? none : find->second;
    }
};

// one vehicle ride as far as fares care
struct ride {
    string route_id, fromZone, toZone;
    std::set<string> zones; // every zone passed through, ends included (only filled when the index usesContains)
    int depart = 0;         // seconds, for transfer_duration
};

struct ticket { int fare = -1, first = 0, last = 0; }; // rides [first, last] on one fare
struct priced { bool ok = false; double total = 0; string currency; vector<ticket> tickets; };

inline index build(const string& attributesPath, const string& rulesPath,
                   const vector<pair<string, vector<string>>>& stoplines, const std::unordered_map<string, int>& stoprefs,
                   const vector<pair<string, vector<string>>>& routelines, const std::unordered_map<string, int>& routerefs) {
    index idx;
    std::unordered_map<string, int> byId;

    {
        ifstream in(attributesPath);
        string line;
        if (!in.good() || !std::getline(in, line)) return idx;
        std::unordered_map<string, int> refs = gtfs::createMapFromVector(gtfs::parseDataCSV(line));
        if (!refs.count("fare_id") || !refs.count("price") || !refs.count("currency_type")) return idx;
        while (std::getline(in, line)) {
            vector<string> row = gtfs::parseDataCSV(line);
            if (!gtfs::normalizeRow(row, refs)) continue;
            fare_option f;
            f.fare_id = row[refs["fare_id"]];
            f.price = gtfs::to_double(row[refs["price"]]);
            f.currency = row[refs["currency_type"]];
            { auto find = refs.find("payment_method");
            if (find != refs.end()) f.payment_method = gtfs::to_integer(row[find->second]); }
            // required column, but an empty value is meaningful: unlimited transfers
            { auto find = refs.find("transfers");
            if (find != refs.end() && !row[find->second].empty() && row[find->second] != " ") f.transfers = gtfs::to_integer(row[find->second]); }
            { auto find = refs.find("agency_id");
            if (find != refs.end()) f.agency_id = row[find->second]; }
            { auto find = refs.find("transfer_duration");
            if (find != refs.end() && !row[find->second].empty() && row[find->second] != " ") f.transfer_duration = gtfs::to_integer(row[find->second]); }
            if (f.price > 0) idx.available = true;
            byId.emplace(f.fare_id, static_cast<int>(idx.fares.size()));
            idx.fares.push_back(std::move(f));
        }
    }

    {
        ifstream in(rulesPath);
        string line;
        if (in.good() && std::getline(in, line)) {
            std::unordered_map<string, int> refs = gtfs::createMapFromVector(gtfs::parseDataCSV(line));
            auto col = [&](const char* key) { auto find = refs.find(key); return find == refs.end() ? -1 : find->second; };
            const int fareCol = col("fare_id"), routeCol = col("route_id"), originCol = col("origin_id"),
                      destCol = col("destination_id"), containsCol = col("contains_id");
            while (fareCol >= 0 && std::getline(in, line)) {
                vector<string> row = gtfs::parseDataCSV(line);
                if (!gtfs::normalizeRow(row, refs)) continue;
                auto f = byId.find(row[fareCol]);
                if (f == byId.end()) continue;
                fare_option& fare = idx.fares[f->second];
                const string origin = originCol >= 0 ? row[originCol] : "", dest = destCol >= 0 ? row[destCol] : "";
                if (!origin.empty() || !dest.empty()) fare.od.emplace_back(origin, dest);
                if (routeCol >= 0 && !row[routeCol].empty()) fare.routes.insert(row[routeCol]);
                if (containsCol >= 0 && !row[containsCol].empty()) fare.contains.insert(row[containsCol]);
            }
        }
    }

    for (int i = 0; i < static_cast<int>(idx.fares.size()); i++) {
        const fare_option& f = idx.fares[i];
        if (!f.contains.empty()) idx.usesContains = true;
        if (f.od.empty()) { idx.anyOrigin.push_back(i); continue; }
        std::set<string> origins;
        for (const auto& [o, d] : f.od) origins.insert(o);
        for (const string& o : origins) idx.byOrigin[o].push_back(i);
    }

    { auto zoneCol = stoprefs.find("zone_id");
    if (zoneCol != stoprefs.end()) for (const auto& [id, row] : stoplines) {
        if (!row[zoneCol->second].empty()) idx.stopZone.emplace(id, row[zoneCol->second]);
    } }
    { auto agencyCol = routerefs.find("agency_id");
    if (agencyCol != routerefs.end()) for (const auto& [id, row] : routelines) {
        if (!row[agencyCol->second].empty()) idx.routeAgency.emplace(id, row[agencyCol->second]);
    } }

    return idx;
}

// does fare f cover rides [first, last] on one ticket?
inline bool covers(const index& idx, const fare_option& f, const vector<ride>& rides, const int first, const int last) {
    if (f.transfers >= 0 && last - first > f.transfers) return false;
    if (f.transfer_duration >= 0 && rides[last].depart - rides[first].depart > f.transfer_duration) return false;
    if (!f.od.empty()) {
        const string& from = rides[first].fromZone;
        const string& to = rides[last].toZone;
        bool any = false;
        for (const auto& [o, d] : f.od) if ((o.empty() || o == from) && (d.empty() || d == to)) { any = true; break; }
        if (!any) return false;
    }
    for (int r = first; r <= last; r++) {
        if (!f.routes.empty() && !f.routes.count(rides[r].route_id)) return false;
        if (!f.agency_id.empty()) {
            auto a = idx.routeAgency.find(rides[r].route_id);
            if (a != idx.routeAgency.end() && a->second != f.agency_id) return false;
        }
    }
    if (!f.contains.empty()) {
        std::set<string> zones;
        for (int r = first; r <= last; r++) zones.insert(rides[r].zones.begin(), rides[r].zones.end());
        if (zones != f.contains) return false;
    }
    return true;
}

// cheapest fare covering rides [first, last], or -1
inline int cheapest(const index& idx, const vector<ride>& rides, const int first, const int last) {
    int best = -1;
    auto consider = [&](const vector<int>& candidates) {
        for (int i : candidates) {
            const fare_option& f = idx.fares[i];
            if (best >= 0 && f.price >= idx.fares[best].price) continue;
            if (covers(idx, f, rides, first, last)) best = i;
        }
    };
    consider(idx.anyOrigin);
    for (const string& origin : {rides[first].fromZone, string()}) {
        auto find = idx.byOrigin.find(origin);
        if (find != idx.byOrigin.end()) consider(find->second);
        if (origin.empty()) break; // the zone was "" already
    }
    return best;
}

// the cheapest way to buy tickets for these rides, in order
inline priced price(const index& idx, const vector<ride>& rides) {
    priced out;
    if (!idx.available || rides.empty()) return out;
    const int n = static_cast<int>(rides.size());
    vector<double> cost(n + 1, 1e18);
    vector<ticket> last(n + 1);
    cost[0] = 0;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i <= j; i++) {
            if (cost[i] >= 1e18) continue;
            const int f = cheapest(idx, rides, i, j);
            if (f < 0) continue;
            const double c = cost[i] + idx.fares[f].price;
            if (c < cost[j + 1]) { cost[j + 1] = c; last[j + 1] = {f, i, j}; }
        }
    }
    if (cost[n] >= 1e18) return out;
    for (int j = n; j > 0; j = last[j].first) out.tickets.push_back(last[j]);
    std::reverse(out.tickets.begin(), out.tickets.end());
    out.currency = idx.fares[out.tickets.front().fare].currency;
    for (const ticket& t : out.tickets) {
        if (idx.fares[t.fare].currency != out.currency) return priced{}; // can't add up mixed currencies
    }
    out.total = cost[n];
    out.ok = true;
    return out;
}

// prices print as plain numbers, rounded to 1/10000 so float sums don't leak digits ("24.25", not "24.249999")
inline string priceJson(const double price) {
    std::ostringstream out;
    out << std::defaultfloat << std::setprecision(12) << std::round(price * 10000) / 10000;
    return out.str();
}

// {"price", "currency", "tickets": [...]} or null; legOf maps a ride index to the caller's leg index
template <typename LegOf>
inline string toJson(const index& idx, const priced& p, LegOf legOf) {
    if (!p.ok) return "null";
    std::ostringstream out;
    out << "{ \"price\": " << priceJson(p.total) << ", \"currency\": \"" << gtfs::jsonEscape(p.currency) << "\", \"tickets\": [";
    for (size_t i = 0; i < p.tickets.size(); i++) {
        const ticket& t = p.tickets[i];
        const fare_option& f = idx.fares[t.fare];
        out << (i ? ", " : "") << "{ \"fare_id\": \"" << gtfs::jsonEscape(f.fare_id) <<
                "\", \"price\": " << priceJson(f.price) <<
                ", \"payment_method\": " << f.payment_method <<
                ", \"first_leg\": " << legOf(t.first) <<
                ", \"last_leg\": " << legOf(t.last) << " }";
    }
    out << "] }";
    return out.str();
}

}

#endif
