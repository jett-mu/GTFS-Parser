// Multi-route journey planning on top of the sorted tables in fast-gtfs.hpp.
//
//      build() turns the loaded stops / stop_times / trips into compact integer arrays once
//      per load (per-stop departures, per-trip calls, straight-line footpaths). plan() then
//      runs a time-dependent A* over (stop, rides taken, arrived on foot) from a departure
//      time, and repeats it later and later to list a day's journeys.

#ifndef PLANNER_HPP
#define PLANNER_HPP

#include <algorithm>
#include <climits>
#include <cmath>
#include <ctime>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include "fast-gtfs.hpp"

namespace planner {

constexpr double WALK_MAX_M = 500.0;        // longest straight-line walk between two stops
constexpr double WALK_SPEED_MS = 1.3;
constexpr double HEURISTIC_SPEED_MS = 30.0; // faster than any bus as the crow flies, so A* never overestimates
constexpr int MAX_RIDES = 4;                // 3 transfers
constexpr int MIN_TRANSFER_SECS = 60;       // changing vehicles at the same stop
constexpr int MAX_WAIT_SECS = 3600;         // longest wait considered at one stop
constexpr int TRANSFER_GAIN_SECS = 300;     // an extra transfer has to arrive at least this much earlier to be listed
constexpr int SLACK_SECS = 1800;            // keep looking this long past the fastest arrival, for fewer-transfer options
constexpr int HORIZON_SECS = 4 * 3600;      // nothing arriving later than this after the query time
constexpr int DAY = 86400;

struct stop_node { string id; double lat = 0, lon = 0; };
struct footpath { int to, secs, metres; };
struct call { int stop, arr, dep, seq; bool pickup, dropoff; };
struct trip_node { string trip_id, route_id; int pattern = 0, service = 0, first = 0, count = 0; };
struct departure { int dep, trip, pos; };

struct index {
    vector<stop_node> stops;
    std::unordered_map<string, int> stopIndex;
    vector<trip_node> trips;
    vector<call> calls;                    // every trip's calls back to back, in stop_sequence order
    vector<vector<departure>> departures;  // per stop, by dep
    vector<int> patterns;                  // per stop: distinct route_id + direction_id + last stop that board there
    vector<vector<footpath>> footpaths;    // per stop: every other stop within WALK_MAX_M
    vector<string> services;
};

struct leg {
    bool walk = false;
    int fromStop = 0, toStop = 0, depart = 0, arrive = 0;
    int trip = -1, boardPos = 0, alightPos = 0; // ride: positions in that trip's calls
    bool prevDay = false;                       // ride: trip runs on the previous service day
    int metres = 0;                             // walk
};

struct journey { vector<leg> legs; int depart = 0, arrive = 0, transfers = 0; };

inline int walkSecs(const double metres) { return static_cast<int>(std::ceil(metres / WALK_SPEED_MS)); }

inline double distanceM(const stop_node& a, const stop_node& b) {
    return gtfs::getDistanceKM(a.lat, a.lon, b.lat, b.lon) * 1000.0;
}

// seconds after midnight, or -1 when unset (parses as -1:-1:-1, or stays 00:00:00 when the column is missing)
inline int toSecs(const gtfs::time& t) {
    if (t.h < 0 || t == gtfs::time()) return -1;
    return t.h * 3600 + t.m * 60 + static_cast<int>(std::lround(t.s));
}

inline void previousDay(int& year, int& month, int& day) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day - 1;
    t.tm_hour = 12; // clear of any DST shift
    std::mktime(&t);
    year = t.tm_year + 1900;
    month = t.tm_mon + 1;
    day = t.tm_mday;
}

inline index build(const vector<pair<string, vector<string>>>& stoplines, const std::unordered_map<string, int>& stoprefs,
                   const vector<pair<string, vector<string>>>& stoptimesstoplines, const std::unordered_map<string, int>& stoptimesstoprefs,
                   const vector<pair<string, vector<string>>>& triplines, const std::unordered_map<string, int>& triprefs) {
    index idx;

    { auto latCol = stoprefs.find("stop_lat"), lonCol = stoprefs.find("stop_lon");
    for (const auto& [id, row] : stoplines) {
        stop_node s;
        s.id = id;
        if (latCol != stoprefs.end()) s.lat = gtfs::to_double(row[latCol->second]);
        if (lonCol != stoprefs.end()) s.lon = gtfs::to_double(row[lonCol->second]);
        idx.stopIndex.emplace(id, static_cast<int>(idx.stops.size()));
        idx.stops.push_back(std::move(s));
    } }
    const size_t nStops = idx.stops.size();

    // trips: route, direction and service straight from trips.txt (no stoi on a blank direction_id)
    std::unordered_map<string, int> serviceIndex, patternIndex;
    auto intern = [](std::unordered_map<string, int>& m, const string& key) {
        return m.emplace(key, static_cast<int>(m.size())).first->second;
    };
    const auto dirCol = triprefs.find("direction_id");
    const int stopCol = stoptimesstoprefs.at("stop_id"), seqCol = stoptimesstoprefs.at("stop_sequence");
    const auto arrCol = stoptimesstoprefs.find("arrival_time"), depCol = stoptimesstoprefs.find("departure_time");
    const auto pickCol = stoptimesstoprefs.find("pickup_type"), dropCol = stoptimesstoprefs.find("drop_off_type");
    const int notAvailable = static_cast<int>(gtfs::stop_time::not_available);

    idx.calls.reserve(stoptimesstoplines.size());
    for (size_t i = 0; i < stoptimesstoplines.size();) {
        const string& trip_id = stoptimesstoplines[i].first;
        size_t j = i;
        while (j < stoptimesstoplines.size() && stoptimesstoplines[j].first == trip_id) ++j;

        auto t = std::lower_bound(triplines.begin(), triplines.end(), trip_id,
            [](const pair<string, vector<string>>& element, const string& key) { return element.first < key; });
        if (t != triplines.end() && t->first == trip_id) {
            trip_node tn;
            tn.trip_id = trip_id;
            tn.route_id = t->second[triprefs.at("route_id")];
            const string dir = dirCol != triprefs.end() ? t->second[dirCol->second] : "";
            tn.service = intern(serviceIndex, t->second[triprefs.at("service_id")]);
            tn.first = static_cast<int>(idx.calls.size());

            for (size_t k = i; k < j; ++k) {
                const vector<string>& row = stoptimesstoplines[k].second;
                auto s = idx.stopIndex.find(row[stopCol]);
                if (s == idx.stopIndex.end()) continue;
                int arr = arrCol != stoptimesstoprefs.end() ? toSecs(gtfs::parseFormattedTime(row[arrCol->second])) : -1;
                int dep = depCol != stoptimesstoprefs.end() ? toSecs(gtfs::parseFormattedTime(row[depCol->second])) : -1;
                if (arr < 0) arr = dep;
                if (dep < 0) dep = arr;
                if (arr < 0) continue; // untimed stop: can't board or alight there, riding past it is unaffected
                call c;
                c.stop = s->second;
                c.arr = arr;
                c.dep = dep;
                c.seq = gtfs::to_integer(row[seqCol]);
                c.pickup = pickCol == stoptimesstoprefs.end() || gtfs::to_integer(row[pickCol->second]) != notAvailable;
                c.dropoff = dropCol == stoptimesstoprefs.end() || gtfs::to_integer(row[dropCol->second]) != notAvailable;
                idx.calls.push_back(c);
            }
            // the sorted file only guarantees grouping by trip_id, same as getAllStops
            std::sort(idx.calls.begin() + tn.first, idx.calls.end(), [](const call& a, const call& b) { return a.seq < b.seq; });
            tn.count = static_cast<int>(idx.calls.size()) - tn.first;
            if (tn.count >= 2) {
                // with the last stop in the key a short-turn isn't mistaken for the full trip behind it
                tn.pattern = intern(patternIndex, tn.route_id + "|" + dir + "|" + to_string(idx.calls.back().stop));
                idx.trips.push_back(std::move(tn));
            } else {
                idx.calls.resize(tn.first);
            }
        }
        i = j;
    }

    idx.services.resize(serviceIndex.size());
    for (const auto& [id, n] : serviceIndex) idx.services[n] = id;

    idx.departures.assign(nStops, {});
    vector<vector<int>> stopPatterns(nStops);
    for (int t = 0; t < static_cast<int>(idx.trips.size()); t++) {
        const trip_node& tn = idx.trips[t];
        for (int p = 0; p + 1 < tn.count; p++) { // nothing to ride from the last call
            const call& c = idx.calls[tn.first + p];
            if (!c.pickup) continue;
            idx.departures[c.stop].push_back({c.dep, t, p});
            stopPatterns[c.stop].push_back(tn.pattern);
        }
    }
    idx.patterns.assign(nStops, 0);
    for (size_t s = 0; s < nStops; s++) {
        std::sort(idx.departures[s].begin(), idx.departures[s].end(), [](const departure& a, const departure& b) { return a.dep < b.dep; });
        vector<int>& p = stopPatterns[s];
        std::sort(p.begin(), p.end());
        idx.patterns[s] = static_cast<int>(std::unique(p.begin(), p.end()) - p.begin());
    }

    // footpaths: bucket stops into cells at least WALK_MAX_M wide, compare against the 3x3 around each
    idx.footpaths.assign(nStops, {});
    double latSum = 0;
    size_t located = 0;
    for (const stop_node& s : idx.stops) if (s.lat != 0 || s.lon != 0) { latSum += s.lat; located++; }
    if (located) {
        const double cellLat = WALK_MAX_M / 111000.0 * 1.05;
        const double cellLon = cellLat / std::max(0.2, std::cos(latSum / located * 3.14159265358979323846 / 180.0));
        std::map<pair<long, long>, vector<int>> grid;
        auto cellOf = [&](const stop_node& s) {
            return pair<long, long>(static_cast<long>(std::floor(s.lat / cellLat)), static_cast<long>(std::floor(s.lon / cellLon)));
        };
        for (int s = 0; s < static_cast<int>(nStops); s++) {
            if (idx.stops[s].lat != 0 || idx.stops[s].lon != 0) grid[cellOf(idx.stops[s])].push_back(s);
        }
        for (int s = 0; s < static_cast<int>(nStops); s++) {
            if (idx.stops[s].lat == 0 && idx.stops[s].lon == 0) continue;
            const auto [cy, cx] = cellOf(idx.stops[s]);
            for (long dy = -1; dy <= 1; dy++) for (long dx = -1; dx <= 1; dx++) {
                auto cell = grid.find({cy + dy, cx + dx});
                if (cell == grid.end()) continue;
                for (int o : cell->second) {
                    if (o == s) continue;
                    const double m = distanceM(idx.stops[s], idx.stops[o]);
                    if (m <= WALK_MAX_M) idx.footpaths[s].push_back({o, walkSecs(m), static_cast<int>(std::lround(m))});
                }
            }
        }
    }

    return idx;
}

// which of idx.services run on a date
inline vector<char> activeServices(const index& idx, int year, int month, int day,
                                   const vector<pair<string, vector<string>>>& calendarlines, const std::unordered_map<string, int>& calendarrefs,
                                   const vector<pair<string, vector<string>>>& calendardatelines, const std::unordered_map<string, int>& calendardaterefs) {
    vector<char> out(idx.services.size(), 0);
    for (size_t i = 0; i < idx.services.size(); i++) {
        out[i] = fast_gtfs::bin_search::isServiceActive(idx.services[i], year, month, day,
            calendarlines, calendarrefs, calendardatelines, calendardaterefs);
    }
    return out;
}

// One query's fixed inputs plus scratch space reused across plan()'s repeated searches.
struct search {
    const index& idx;
    const vector<char>* active[2];  // [0] the date, [1] the day before (its trips shifted back DAY seconds)
    vector<char> isTarget;
    vector<int> hSecs;              // A* heuristic per stop: straight line to the nearest To stop at HEURISTIC_SPEED_MS
    vector<int> fromStops;

    struct parent { int prev = -1; int trip = -1, boardPos = 0, alightPos = 0, metres = 0; bool walk = false, prevDay = false; };
    vector<int> label;
    vector<parent> parents;
    vector<int> boarded;            // [rides * trips * 2 + trip * 2 + prevDay] -> earliest position boarded

    search(const index& idx, const vector<char>& today, const vector<char>& yesterday,
           const vector<int>& from, const vector<int>& to)
        : idx(idx), active{&today, &yesterday}, fromStops(from) {
        const size_t n = idx.stops.size();
        isTarget.assign(n, 0);
        for (int t : to) isTarget[t] = 1;
        hSecs.assign(n, 0);
        for (size_t s = 0; s < n; s++) {
            double best = 1e18;
            for (int t : to) best = std::min(best, distanceM(idx.stops[s], idx.stops[t]));
            hSecs[s] = static_cast<int>(best / HEURISTIC_SPEED_MS);
        }
    }

    static int state(int stop, int rides, bool onFoot) { return (stop * (MAX_RIDES + 1) + rides) * 2 + (onFoot ? 1 : 0); }

    // Pareto-best journeys (arrival vs transfers, each extra transfer worth TRANSFER_GAIN_SECS) leaving at or after tau
    vector<journey> run(const int tau) {
        const size_t nStates = idx.stops.size() * (MAX_RIDES + 1) * 2;
        const size_t nTrips = idx.trips.size();
        label.assign(nStates, INT_MAX);
        parents.assign(nStates, {});
        boarded.assign((MAX_RIDES + 1) * nTrips * 2, INT_MAX);

        struct entry { int f, g, st; };
        auto cmp = [](const entry& a, const entry& b) { return a.f > b.f; };
        std::priority_queue<entry, vector<entry>, decltype(cmp)> open(cmp);

        int bestArr[MAX_RIDES + 1], bestState[MAX_RIDES + 1];
        std::fill(bestArr, bestArr + MAX_RIDES + 1, INT_MAX);
        std::fill(bestState, bestState + MAX_RIDES + 1, -1);
        int fastest = INT_MAX;
        const int horizon = tau + HORIZON_SECS;

        auto relax = [&](int st, int stop, int g, const parent& p) {
            if (g >= label[st] || g > horizon) return;
            if (fastest != INT_MAX && g > fastest + SLACK_SECS) return;
            label[st] = g;
            parents[st] = p;
            open.push({g + hSecs[stop], g, st});
        };
        for (int s : fromStops) relax(state(s, 0, false), s, tau, {});

        while (!open.empty()) {
            const entry e = open.top();
            open.pop();
            if (e.g > label[e.st]) continue;
            if (fastest != INT_MAX && e.f > fastest + SLACK_SECS) break;

            const bool onFoot = e.st & 1;
            const int rides = (e.st / 2) % (MAX_RIDES + 1);
            const int stop = e.st / 2 / (MAX_RIDES + 1);

            if (rides > 0 && isTarget[stop]) {
                if (e.g < bestArr[rides]) { bestArr[rides] = e.g; bestState[rides] = e.st; }
                fastest = std::min(fastest, e.g);
                continue; // nothing to gain by riding on past a To stop
            }

            if (!onFoot) {
                for (const footpath& f : idx.footpaths[stop]) {
                    parent p;
                    p.prev = e.st;
                    p.walk = true;
                    p.metres = f.metres;
                    relax(state(f.to, rides, true), f.to, e.g + f.secs, p);
                }
            }

            if (rides == MAX_RIDES) continue;
            const int ready = e.g + (rides > 0 && !onFoot ? MIN_TRANSFER_SECS : 0);
            const vector<departure>& deps = idx.departures[stop];
            for (int day = 0; day < 2; day++) {
                const int offset = day ? -DAY : 0;
                const vector<char>& on = *active[day];
                vector<int> seen;
                for (auto d = std::lower_bound(deps.begin(), deps.end(), ready - offset,
                         [](const departure& x, int key) { return x.dep < key; });
                     d != deps.end() && d->dep + offset <= ready + MAX_WAIT_SECS; ++d) {
                    const trip_node& tn = idx.trips[d->trip];
                    if (!on[tn.service]) continue;
                    // the first departure of each route + direction + last stop is the one worth taking
                    if (std::find(seen.begin(), seen.end(), tn.pattern) != seen.end()) continue;
                    seen.push_back(tn.pattern);

                    const size_t key = static_cast<size_t>(d->trip) * 2 + day;
                    bool already = false;
                    for (int r = 0; r <= rides && !already; r++) already = boarded[r * nTrips * 2 + key] <= d->pos;
                    if (!already) {
                        boarded[rides * nTrips * 2 + key] = d->pos;
                        for (int q = d->pos + 1; q < tn.count; q++) {
                            const call& c = idx.calls[tn.first + q];
                            if (!c.dropoff) continue;
                            parent p;
                            p.prev = e.st;
                            p.trip = d->trip;
                            p.boardPos = d->pos;
                            p.alightPos = q;
                            p.prevDay = day == 1;
                            relax(state(c.stop, rides + 1, false), c.stop, c.arr + offset, p);
                        }
                    }
                    if (static_cast<int>(seen.size()) == idx.patterns[stop]) break;
                }
            }
        }

        vector<journey> out;
        int bestFewer = INT_MAX;
        for (int r = 1; r <= MAX_RIDES; r++) {
            if (bestState[r] < 0 || (bestFewer != INT_MAX && bestArr[r] + TRANSFER_GAIN_SECS > bestFewer)) continue;
            bestFewer = bestArr[r];
            out.push_back(rebuild(bestState[r]));
        }
        return out;
    }

    journey rebuild(int st) const {
        journey j;
        for (; parents[st].prev >= 0; st = parents[st].prev) {
            const parent& p = parents[st];
            leg l;
            l.fromStop = p.prev / 2 / (MAX_RIDES + 1);
            l.toStop = st / 2 / (MAX_RIDES + 1);
            if (p.walk) {
                l.walk = true;
                l.metres = p.metres;
                l.depart = label[p.prev];
                l.arrive = label[st];
            } else {
                const trip_node& tn = idx.trips[p.trip];
                const int offset = p.prevDay ? -DAY : 0;
                l.trip = p.trip;
                l.boardPos = p.boardPos;
                l.alightPos = p.alightPos;
                l.prevDay = p.prevDay;
                l.depart = idx.calls[tn.first + p.boardPos].dep + offset;
                l.arrive = idx.calls[tn.first + p.alightPos].arr + offset;
            }
            j.legs.push_back(l);
        }
        std::reverse(j.legs.begin(), j.legs.end());
        // a walk to the first stop leaves just in time for the bus, not at the query time
        if (j.legs.size() > 1 && j.legs[0].walk) {
            const int secs = j.legs[0].arrive - j.legs[0].depart;
            j.legs[0].arrive = j.legs[1].depart;
            j.legs[0].depart = j.legs[1].depart - secs;
        }
        int rides = 0;
        for (const leg& l : j.legs) rides += l.walk ? 0 : 1;
        j.transfers = std::max(0, rides - 1);
        j.depart = j.legs.front().depart;
        j.arrive = j.legs.back().arrive;
        return j;
    }
};

// the closest From/To pair, when it's within walking distance outright (from = -1 otherwise)
struct walk_pair { int from = -1, to = -1; double metres = 0; };
inline walk_pair directWalk(const index& idx, const vector<int>& from, const vector<int>& to) {
    walk_pair best;
    best.metres = WALK_MAX_M + 1;
    for (int f : from) for (int t : to) {
        const double m = distanceM(idx.stops[f], idx.stops[t]);
        if (m < best.metres) { best.metres = m; best.from = f; best.to = t; }
    }
    if (best.metres > WALK_MAX_M) best.from = best.to = -1;
    return best;
}

// Journeys leaving at or after `after` (and no later than `until`), earliest first: the search
// is repeated from just after each found departure, and anything another journey beats on
// departure, arrival and transfers at once is dropped.
inline vector<journey> plan(const index& idx, const vector<int>& from, const vector<int>& to,
                            const vector<char>& today, const vector<char>& yesterday,
                            const int after, const int until, const int limit) {
    vector<journey> found;
    if (from.empty() || to.empty() || limit <= 0) return found;

    search s(idx, today, yesterday, from, to);
    std::set<vector<int>> seen;
    auto signature = [](const journey& j) {
        vector<int> sig;
        for (const leg& l : j.legs) {
            if (l.walk) sig.insert(sig.end(), {-1, l.fromStop, l.toStop});
            else sig.insert(sig.end(), {l.trip * 2 + (l.prevDay ? 1 : 0), l.boardPos, l.alightPos});
        }
        return sig;
    };
    auto dominated = [](const journey& a, const journey& b) { // b beats a
        return b.depart >= a.depart && b.arrive <= a.arrive && b.transfers <= a.transfers
            && (b.depart > a.depart || b.arrive < a.arrive || b.transfers < a.transfers);
    };
    auto keep = [&]() {
        vector<journey> out;
        for (const journey& a : found) {
            bool beaten = false;
            for (const journey& b : found) if (dominated(a, b)) { beaten = true; break; }
            if (!beaten) out.push_back(a);
        }
        std::sort(out.begin(), out.end(), [](const journey& a, const journey& b) {
            return a.depart != b.depart ? a.depart < b.depart : a.arrive < b.arrive;
        });
        return out;
    };

    int tau = after;
    for (int iteration = 0; iteration < limit * 3 && tau <= until; iteration++) {
        vector<journey> batch = s.run(tau);
        if (batch.empty()) { tau += HORIZON_SECS / 2; continue; } // e.g. the overnight gap: look further on
        int next = INT_MAX;
        for (journey& j : batch) {
            next = std::min(next, j.depart);
            if (j.depart > until || !seen.insert(signature(j)).second) continue;
            found.push_back(std::move(j));
        }
        tau = std::max(next, tau) + 1;
        if (static_cast<int>(keep().size()) > limit) break;
    }

    vector<journey> out = keep();
    // nothing slower than just walking there
    const walk_pair w = directWalk(idx, from, to);
    if (w.from >= 0) {
        const int secs = walkSecs(w.metres);
        out.erase(std::remove_if(out.begin(), out.end(), [&](const journey& j) { return j.arrive - j.depart >= secs; }), out.end());
    }
    if (static_cast<int>(out.size()) > limit) out.resize(limit);
    return out;
}

}

#endif
