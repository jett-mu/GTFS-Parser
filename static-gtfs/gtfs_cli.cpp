// gtfs_cli — interactive, keyboard-driven terminal explorer for GTFS Schedule + Realtime data.
// No args needed: launch it, arrow through menus, type to search. Ctrl+C / Esc always backs out.
#include "gtfs.hpp"
#include "config.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <optional>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <termios.h>
#include <unistd.h>
#include <libgen.h>
#include <climits>

using std::cout, std::cerr, std::string, std::endl;
using std::vector, std::optional;

// ============================================================================
// terminal / raw input
// ============================================================================

namespace term {

struct RawMode {
    termios orig{};
    bool ok = false;
    RawMode() {
        if (tcgetattr(STDIN_FILENO, &orig) == -1) return;
        termios raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        ok = (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0);
    }
    ~RawMode() { if (ok) tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig); }
};

enum class Key { Up, Down, Left, Right, Enter, Escape, Backspace, Tab, CtrlC, Char, Other };
struct KeyEvent { Key key; char ch = 0; };

KeyEvent readKey() {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n != 1) return {Key::Other};
    if (c == 3) return {Key::CtrlC};
    if (c == 27) {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return {Key::Escape};
        if (seq[0] != '[' && seq[0] != 'O') return {Key::Escape};
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return {Key::Escape};
        switch (seq[1]) {
            case 'A': return {Key::Up};
            case 'B': return {Key::Down};
            case 'C': return {Key::Right};
            case 'D': return {Key::Left};
        }
        return {Key::Escape};
    }
    if (c == '\r' || c == '\n') return {Key::Enter};
    if (c == 127 || c == 8) return {Key::Backspace};
    if (c == '\t') return {Key::Tab};
    if ((unsigned char)c >= 32) return {Key::Char, c};
    return {Key::Other};
}

void clear()      { cout << "\033[2J\033[H"; }
void hideCursor()  { cout << "\033[?25l"; }
void showCursor()  { cout << "\033[?25h"; }

} // namespace term

// ============================================================================
// colors + drawing helpers
// ============================================================================

namespace col {
const char* RESET  = "\033[0m";
const char* BOLD    = "\033[1m";
const char* DIM     = "\033[2m";
const char* ORANGE  = "\033[38;5;208m";
const char* CYAN    = "\033[38;5;80m";
const char* GREEN   = "\033[38;5;114m";
const char* YELLOW  = "\033[38;5;221m";
const char* MAGENTA = "\033[38;5;176m";
const char* RED     = "\033[38;5;203m";
const char* GRAY    = "\033[38;5;244m";
const char* INVERT  = "\033[7m";
}

static void header(const string& title, const string& subtitle = "") {
    term::clear();
    cout << col::ORANGE << col::BOLD << "  ▸ gtfs" << col::RESET
         << col::DIM << "  —  " << col::RESET
         << col::BOLD << title << col::RESET << "\n";
    if (!subtitle.empty()) cout << col::GRAY << "    " << subtitle << col::RESET << "\n";
    cout << col::GRAY << "  " << string(60, '-') << col::RESET << "\n\n";
}

static void footer(const string& hint) {
    cout << "\n" << col::GRAY << "  " << string(60, '-') << "\n  " << hint << col::RESET << "\n";
}

static void pauseKey(const string& msg = "press any key to continue…") {
    cout << "\n" << col::DIM << "  " << msg << col::RESET;
    cout.flush();
    term::readKey();
}

// ---- paged list ----
// shows pre-rendered lines (ANSI already applied) a page at a time.
// left/right (or h/l, pgup/pgdn) flip pages, esc/q exits.
static const int kPageSize = 12;

static void showPagedLines(const string& title, const string& subtitle,
                            const vector<string>& lines,
                            std::function<void()> preamble = nullptr,
                            int pageSize = kPageSize) {
    int totalPages = std::max(1, (int)((lines.size() + pageSize - 1) / pageSize));
    int page = 0;
    while (true) {
        header(title, subtitle);
        if (preamble) preamble();
        if (lines.empty()) {
            cout << col::GRAY << "  (nothing here)" << col::RESET << "\n";
        } else {
            int start = page * pageSize;
            int end = std::min((int)lines.size(), start + pageSize);
            for (int i = start; i < end; i++) cout << lines[i] << "\n";
        }
        cout << "\n" << col::GRAY << "  " << (lines.empty() ? 0 : page + 1) << "/" << totalPages
             << "  (" << lines.size() << " total)" << col::RESET << "\n";
        footer(totalPages > 1 ? "←/→ page   esc back" : "esc back");
        auto ev = term::readKey();
        if (ev.key == term::Key::Left)  page = (page - 1 + totalPages) % totalPages;
        else if (ev.key == term::Key::Right) page = (page + 1) % totalPages;
        else if (ev.key == term::Key::Char && ev.ch == 'h') page = (page - 1 + totalPages) % totalPages;
        else if (ev.key == term::Key::Char && ev.ch == 'l') page = (page + 1) % totalPages;
        else if (ev.key == term::Key::Escape || ev.key == term::Key::CtrlC) return;
        else if (ev.key == term::Key::Enter) return;
        else if (ev.key == term::Key::Char && (ev.ch == 'q' || ev.ch == 'Q')) return;
    }
}

// ---- menu ----
// returns selected index, or -1 if the user backed out (Esc / Ctrl+C / q)
static int runMenu(const string& title, const vector<string>& items,
                    const string& subtitle = "", const string& hint = "↑/↓ move   enter select   esc back") {
    int sel = 0;
    while (true) {
        header(title, subtitle);
        for (int i = 0; i < (int)items.size(); i++) {
            if (i == sel) {
                cout << col::ORANGE << col::BOLD << "  ❯ " << items[i] << col::RESET << "\n";
            } else {
                cout << "    " << col::GRAY << items[i] << col::RESET << "\n";
            }
        }
        footer(hint);
        auto ev = term::readKey();
        if (ev.key == term::Key::Up)    sel = (sel - 1 + (int)items.size()) % items.size();
        else if (ev.key == term::Key::Down)  sel = (sel + 1) % items.size();
        else if (ev.key == term::Key::Enter) return sel;
        else if (ev.key == term::Key::Escape || ev.key == term::Key::CtrlC) return -1;
        else if (ev.key == term::Key::Char && (ev.ch == 'q' || ev.ch == 'Q')) return -1;
        else if (ev.key == term::Key::Char && ev.ch == 'j') sel = (sel + 1) % items.size();
        else if (ev.key == term::Key::Char && ev.ch == 'k') sel = (sel - 1 + (int)items.size()) % items.size();
    }
}

// ---- free-text line input ----
static optional<string> inputLine(const string& title, const string& prompt, const string& subtitle = "") {
    string buf;
    while (true) {
        header(title, subtitle);
        cout << "  " << prompt << "\n\n  " << col::CYAN << "> " << col::RESET << buf << col::INVERT << " " << col::RESET;
        footer("type to enter   enter confirm   esc cancel");
        auto ev = term::readKey();
        if (ev.key == term::Key::Enter) return buf;
        if (ev.key == term::Key::Escape || ev.key == term::Key::CtrlC) return std::nullopt;
        if (ev.key == term::Key::Backspace) { if (!buf.empty()) buf.pop_back(); }
        else if (ev.key == term::Key::Char) buf += ev.ch;
    }
}

static optional<double> inputDouble(const string& title, const string& prompt) {
    while (true) {
        auto s = inputLine(title, prompt);
        if (!s) return std::nullopt;
        try { return std::stod(*s); } catch (...) {
            cout << "\n" << col::RED << "  not a number, try again" << col::RESET;
            pauseKey();
        }
    }
}

static optional<int> inputInt(const string& title, const string& prompt, optional<int> defVal = std::nullopt) {
    while (true) {
        auto s = inputLine(title, prompt + (defVal ? (" [" + std::to_string(*defVal) + "]") : ""));
        if (!s) return std::nullopt;
        if (s->empty() && defVal) return *defVal;
        try { return std::stoi(*s); } catch (...) {
            cout << "\n" << col::RED << "  not a number, try again" << col::RESET;
            pauseKey();
        }
    }
}

// ============================================================================
// mini ascii scatter map — plots lat/lon points in a terminal-sized grid
// ============================================================================

static void drawAsciiMap(const vector<std::pair<double,double>>& pts,
                          const vector<std::pair<double,double>>& highlights = {},
                          int width = 56, int height = 18) {
    if (pts.empty()) return;
    double minLat = pts[0].first, maxLat = pts[0].first;
    double minLon = pts[0].second, maxLon = pts[0].second;
    for (auto& p : pts) {
        minLat = std::min(minLat, p.first);  maxLat = std::max(maxLat, p.first);
        minLon = std::min(minLon, p.second); maxLon = std::max(maxLon, p.second);
    }
    for (auto& p : highlights) {
        minLat = std::min(minLat, p.first);  maxLat = std::max(maxLat, p.first);
        minLon = std::min(minLon, p.second); maxLon = std::max(maxLon, p.second);
    }
    double latSpan = std::max(maxLat - minLat, 1e-6);
    double lonSpan = std::max(maxLon - minLon, 1e-6);
    // pad a little so points don't sit on the border
    minLat -= latSpan * 0.05; maxLat += latSpan * 0.05;
    minLon -= lonSpan * 0.05; maxLon += lonSpan * 0.05;
    latSpan = maxLat - minLat; lonSpan = maxLon - minLon;

    vector<string> grid(height, string(width, ' '));
    auto plot = [&](double lat, double lon, char c) {
        int x = (int)std::round((lon - minLon) / lonSpan * (width - 1));
        int y = (int)std::round((maxLat - lat) / latSpan * (height - 1));
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        grid[y][x] = c;
    };
    for (auto& p : pts) plot(p.first, p.second, '.');
    for (auto& p : highlights) plot(p.first, p.second, '@');

    cout << col::GRAY << "  +" << string(width, '-') << "+\n" << col::RESET;
    for (auto& row : grid) {
        cout << col::GRAY << "  |" << col::RESET;
        for (char c : row) {
            if (c == '@') cout << col::ORANGE << col::BOLD << c << col::RESET;
            else if (c == '.') cout << col::CYAN << c << col::RESET;
            else cout << c;
        }
        cout << col::GRAY << "|" << col::RESET << "\n";
    }
    cout << col::GRAY << "  +" << string(width, '-') << "+" << col::RESET << "\n";
}

// ============================================================================
// live incremental search — the "fun" centerpiece: type, results update live
// ============================================================================

// generic live search over a searchFn(query) -> vector<{label, sublabel, payload id}>
struct SearchHit { string label; string sub; string id; int score; };

static optional<SearchHit> liveSearch(const string& title, const string& hintLine,
                                       std::function<vector<SearchHit>(const string&)> searchFn) {
    string query;
    int sel = 0;
    vector<SearchHit> results;
    bool first = true;
    while (true) {
        header(title, hintLine);
        cout << "  " << col::CYAN << "🔎 " << col::RESET << query << col::INVERT << " " << col::RESET << "\n\n";
        if (!query.empty() && (first || true)) results = searchFn(query);
        else results.clear();
        first = false;

        int show = std::min((int)results.size(), 10);
        if (query.empty()) {
            cout << col::GRAY << "  start typing to search…" << col::RESET << "\n";
        } else if (show == 0) {
            cout << col::GRAY << "  no matches" << col::RESET << "\n";
        }
        for (int i = 0; i < show; i++) {
            auto& r = results[i];
            if (i == sel) {
                cout << col::ORANGE << col::BOLD << "  ❯ " << r.label << col::RESET;
                if (!r.sub.empty()) cout << col::ORANGE << "  (" << r.sub << ")" << col::RESET;
                cout << col::GRAY << "  " << r.score << "%" << col::RESET << "\n";
            } else {
                cout << "    " << r.label;
                if (!r.sub.empty()) cout << col::GRAY << "  (" << r.sub << ")" << col::RESET;
                cout << col::GRAY << "  " << r.score << "%" << col::RESET << "\n";
            }
        }
        footer("type to search   ↑/↓ move   enter select   esc cancel");
        auto ev = term::readKey();
        if (ev.key == term::Key::Char) { query += ev.ch; sel = 0; }
        else if (ev.key == term::Key::Backspace) { if (!query.empty()) query.pop_back(); sel = 0; }
        else if (ev.key == term::Key::Up)   sel = show > 0 ? (sel - 1 + show) % show : 0;
        else if (ev.key == term::Key::Down) sel = show > 0 ? (sel + 1) % show : 0;
        else if (ev.key == term::Key::Enter) { if (show > 0) return results[sel]; }
        else if (ev.key == term::Key::Escape || ev.key == term::Key::CtrlC) return std::nullopt;
    }
}

static optional<SearchHit> searchStopInteractive(const string& title = "search stops") {
    return liveSearch(title, "find a stop by name", [](const string& q) {
        auto raw = gtfs::searchStop(q);
        vector<SearchHit> out;
        for (auto& r : raw) out.push_back({r.text.str, "", r.stop_id, r.score});
        return out;
    });
}

static optional<SearchHit> searchRouteInteractive(const string& title = "search routes") {
    return liveSearch(title, "find a route by number or name", [](const string& q) {
        auto raw = gtfs::searchRoute(q);
        vector<SearchHit> out;
        for (auto& r : raw) out.push_back({r.route_short_name.empty() ? r.route_id : r.route_short_name,
                                            r.route_long_name, r.route_id, r.score});
        return out;
    });
}

// ============================================================================
// formatting helpers (ported from the old CLI)
// ============================================================================

static string allowableStr(gtfs::trip::allowable a) {
    switch (a) {
        case gtfs::trip::allowable::no_info:     return "no info";
        case gtfs::trip::allowable::allowed:     return "allowed";
        case gtfs::trip::allowable::not_allowed: return "not allowed";
        default: return "unknown";
    }
}

static string routeTypeStr(gtfs::route::type t) {
    switch (t) {
        case gtfs::route::type::light_rail:  return "light rail";
        case gtfs::route::type::underground: return "underground";
        case gtfs::route::type::rail:        return "rail";
        case gtfs::route::type::bus:         return "bus";
        case gtfs::route::type::ferry:       return "ferry";
        case gtfs::route::type::cable_tram:  return "cable tram";
        case gtfs::route::type::aerial_lift: return "aerial lift";
        case gtfs::route::type::funicular:   return "funicular";
        case gtfs::route::type::trolleybus:  return "trolleybus";
        case gtfs::route::type::monorail:    return "monorail";
        default: return "unknown";
    }
}

static string feedStatusStr(gtfs::feedStatus fs) {
    switch (fs) {
        case gtfs::feedStatus::in_use:   return "in use";
        case gtfs::feedStatus::expired:  return "expired";
        case gtfs::feedStatus::upcoming: return "upcoming";
        default: return "no result";
    }
}

static string fmtDate(int y, int m, int d) {
    return std::to_string(y) + "-" + (m < 10 ? "0" : "") + std::to_string(m) +
           "-" + (d < 10 ? "0" : "") + std::to_string(d);
}
static string fmtDate(const gtfs::calendar_day& cd) { return fmtDate(cd.year, cd.month, cd.day); }

static string kvLine(const string& k, const string& v) {
    std::ostringstream os;
    os << "  " << col::GRAY << std::left << std::setw(16) << k << col::RESET << v;
    return os.str();
}

static void kv(const string& k, const string& v) {
    cout << kvLine(k, v) << "\n";
}

// ============================================================================
// config
// ============================================================================

static string configFilePath(const char* argv0) {
    char resolved[PATH_MAX];
    char input[PATH_MAX];
    strncpy(input, argv0, PATH_MAX - 1);
    input[PATH_MAX - 1] = '\0';
    if (realpath(argv0, resolved)) {
        char copy[PATH_MAX];
        strncpy(copy, resolved, PATH_MAX - 1);
        copy[PATH_MAX - 1] = '\0';
        return string(dirname(copy)) + "/config.json";
    }
    return string(dirname(input)) + "/config.json";
}

static string readConfigDataPath(const string& cfgFile) {
    std::ifstream f(cfgFile);
    if (!f.is_open()) return "";
    string content, line;
    while (getline(f, line)) content += line;
    auto pos = content.find("\"data_path\"");
    if (pos == string::npos) return "";
    pos = content.find('"', pos + 11);
    if (pos == string::npos) return "";
    ++pos;
    auto end = content.find('"', pos);
    if (end == string::npos) return "";
    return content.substr(pos, end - pos);
}

static void writeConfigDataPath(const string& cfgFile, const string& dataPath) {
    std::ofstream f(cfgFile);
    if (!f.is_open()) throw std::runtime_error("cannot write config: " + cfgFile);
    f << "{\n    \"data_path\": \"" << dataPath << "\"\n}\n";
}

static void applyRoot(const string& newRoot) {
    string r = newRoot;
    if (!r.empty() && r.back() != '/') r += '/';
    config::root           = r;
    gtfs::stopPath         = r + "stops.txt";
    gtfs::routePath        = r + "routes.txt";
    gtfs::tripsPath        = r + "trips.txt";
    gtfs::stopTimesPath    = r + "stop_times.txt";
    gtfs::tripPath         = r + "trips.txt";
    gtfs::calendarPath     = r + "calendar.txt";
    gtfs::calendarDatesPath = r + "calendar_dates.txt";
    gtfs::agencyPath       = r + "agency.txt";
    gtfs::shapePath        = r + "shapes.txt";
    gtfs::feedInfoFile     = r + "feed_info.txt";
}

// ============================================================================
// realtime — shells out to the compiled gtfs-rt decoders (built by `make rt`)
// ============================================================================

static string rtToolDir(const char* argv0) {
    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) return "";
    char copy[PATH_MAX];
    strncpy(copy, resolved, PATH_MAX - 1);
    copy[PATH_MAX - 1] = '\0';
    string staticGtfsDir = dirname(copy); // .../static-gtfs
    string repoRoot = staticGtfsDir + "/..";
    return repoRoot + "/gtfs-rt/proto-conversion/webserver-implementation";
}

static string shellQuote(const string& s) {
    string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    out += "'";
    return out;
}

// runs `<toolDir>/<tool> <arg>`, returns {success, stdout+stderr}
static std::pair<bool, string> runTool(const string& toolPath, const string& arg) {
    if (access(toolPath.c_str(), X_OK) != 0) {
        return {false, "not built — run `make rt` from the repo root (needs protobuf + pkg-config)"};
    }
    string cmd = shellQuote(toolPath) + " " + shellQuote(arg) + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {false, "failed to launch " + toolPath};
    string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
    int rc = pclose(pipe);
    return {rc == 0, out};
}

// very small JSON syntax highlighter — line-oriented, matches the
// add_whitespace=true / preserve_proto_field_names output of decode*.cpp
static vector<string> colorizeJsonLines(const string& json) {
    vector<string> out;
    std::istringstream iss(json);
    string line;
    while (std::getline(iss, line)) {
        std::ostringstream os;
        size_t indentEnd = line.find_first_not_of(' ');
        if (indentEnd == string::npos) { out.push_back(""); continue; }
        string indent = line.substr(0, indentEnd);
        string rest = line.substr(indentEnd);
        os << indent;
        bool handled = false;
        if (rest[0] == '"') {
            size_t keyEnd = rest.find('"', 1);
            if (keyEnd != string::npos && keyEnd + 1 < rest.size() && rest[keyEnd + 1] == ':') {
                string key = rest.substr(0, keyEnd + 1);
                string valuePart = rest.substr(keyEnd + 2); // after ':'
                os << col::CYAN << key << col::RESET << ":";
                size_t vstart = valuePart.find_first_not_of(' ');
                if (vstart == string::npos) { os << valuePart; }
                else {
                    os << " ";
                    string value = valuePart.substr(vstart);
                    bool trailingComma = !value.empty() && value.back() == ',';
                    string core = trailingComma ? value.substr(0, value.size() - 1) : value;
                    if (!core.empty() && core.front() == '"') os << col::GREEN << core << col::RESET;
                    else if (core == "true" || core == "false") os << col::MAGENTA << core << col::RESET;
                    else if (!core.empty() && (isdigit((unsigned char)core[0]) || core[0] == '-')) os << col::YELLOW << core << col::RESET;
                    else os << col::GRAY << core << col::RESET;
                    if (trailingComma) os << col::GRAY << "," << col::RESET;
                }
                handled = true;
            }
        }
        if (!handled) os << col::GRAY << rest << col::RESET;
        out.push_back(os.str());
    }
    return out;
}

// ============================================================================
// screens
// ============================================================================

static string g_prog;   // argv0, for locating rt tools
static string g_cfgFile;

static void screenStopDetail(const string& stop_id) {
    header("stop " + stop_id);
    try {
        auto st = gtfs::getStopInfo(stop_id);
        kv("stop id",    st.stop_id);
        kv("stop code",  st.stop_code);
        kv("name",       st.stop_name);
        kv("coords",     std::to_string(st.stop_lat) + ", " + std::to_string(st.stop_lon));
        kv("zone",       st.zone_id);
        kv("parent",     st.parent_station.empty() ? "(none)" : st.parent_station);
    } catch (const std::exception& e) {
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
    }
    pauseKey();
}

static string fmtTimeRouteTrip(const string& time, const string& routeShortName, const string& tripId) {
    std::ostringstream os;
    os << "  " << col::CYAN << time << col::RESET << "  " << col::BOLD << std::left << std::setw(6)
       << routeShortName << col::RESET << col::GRAY << tripId << col::RESET;
    return os.str();
}

static void screenStopDepartures(const string& stop_id, bool remaining) {
    try {
        auto st = gtfs::getStopInfo(stop_id);
        vector<string> lines;
        string sub;
        if (remaining) {
            auto now   = gtfs::getCurrentTime();
            auto today = gtfs::getToday();
            auto deps  = gtfs::getRemainingDayStops(stop_id, now, today);
            std::sort(deps.begin(), deps.end(), [](gtfs::trip_segment& a, gtfs::trip_segment& b) {
                return a.stop.arrival_time < b.stop.arrival_time;
            });
            sub = st.stop_name + "  —  from " + now.leadingRoundedTime() + "  —  " + std::to_string(deps.size()) + " departures";
            for (auto& seg : deps) {
                auto route = gtfs::getRouteInfo(seg.route_id);
                lines.push_back(fmtTimeRouteTrip(seg.stop.arrival_time.leadingRoundedTime(), route.route_short_name, seg.stop.trip_id));
            }
        } else {
            auto today = gtfs::getToday();
            auto deps  = gtfs::getDayTimesAtStop(stop_id, today.year, today.month, today.day);
            std::sort(deps.begin(), deps.end(), [](gtfs::trip_segment& a, gtfs::trip_segment& b) {
                return a.stop.arrival_time < b.stop.arrival_time;
            });
            sub = st.stop_name + "  —  " + fmtDate(today) + "  —  " + std::to_string(deps.size()) + " departures";
            for (auto& seg : deps) {
                auto route = gtfs::getRouteInfo(seg.route_id);
                lines.push_back(fmtTimeRouteTrip(seg.stop.arrival_time.leadingRoundedTime(), route.route_short_name, seg.stop.trip_id));
            }
        }
        showPagedLines(remaining ? "remaining departures — " + stop_id : "departures — " + stop_id, sub, lines);
    } catch (const std::exception& e) {
        header((remaining ? "remaining departures — " : "departures — ") + stop_id);
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
        pauseKey();
    }
}

static void screenNearbyStops() {
    header("nearby stops");
    auto lat = inputDouble("nearby stops", "latitude?");
    if (!lat) return;
    auto lon = inputDouble("nearby stops", "longitude?");
    if (!lon) return;
    auto top = inputInt("nearby stops", "how many?", 10);
    if (!top) return;

    auto stops = gtfs::getNearestStops(*lat, *lon, *top, -1);
    vector<std::pair<double,double>> pts, highlight = {{*lat, *lon}};
    for (auto& s : stops) pts.push_back({s.stop_lat, s.stop_lon});

    vector<string> lines;
    for (auto& s : stops) {
        double dist = gtfs::getDistanceKM(*lat, *lon, s.stop_lat, s.stop_lon);
        char buf[16]; snprintf(buf, sizeof(buf), "%.2f km", dist);
        std::ostringstream os;
        os << "  " << col::CYAN << std::left << std::setw(10) << s.stop_id << col::RESET
           << col::GRAY << std::right << std::setw(9) << buf << col::RESET
           << "  " << s.stop_name;
        lines.push_back(os.str());
    }
    showPagedLines("nearby stops", fmtDate(gtfs::getToday()), lines, [&]() {
        drawAsciiMap(pts, highlight);
        cout << "\n";
    });
}

static void screenStopsMenu() {
    while (true) {
        int c = runMenu("stops", {
            "search stops by name",
            "look up stop by id",
            "departures today",
            "remaining departures (from now)",
            "nearby stops"
        }, "everything about a stop");
        if (c == -1) return;
        if (c == 0) {
            auto hit = searchStopInteractive();
            if (hit) screenStopDetail(hit->id);
        } else if (c == 1) {
            auto id = inputLine("stop lookup", "stop id?");
            if (id) screenStopDetail(*id);
        } else if (c == 2) {
            auto hit = searchStopInteractive("departures today — pick a stop");
            if (hit) screenStopDepartures(hit->id, false);
        } else if (c == 3) {
            auto hit = searchStopInteractive("remaining departures — pick a stop");
            if (hit) screenStopDepartures(hit->id, true);
        } else if (c == 4) {
            screenNearbyStops();
        }
    }
}

static void screenTripDetail(const string& trip_id) {
    header("trip " + trip_id);
    try {
        auto info  = gtfs::getTripInfo(trip_id);
        auto route = gtfs::getRouteInfo(info.route_id);
        kv("trip id",    info.trip_id);
        kv("route",      info.route_id + "  (" + route.route_short_name + " — " + route.route_long_name + ")");
        kv("service id", info.service_id);
        kv("headsign",   info.trip_headsign);
        kv("direction",  info.direction_id ? "1 (inbound)" : "0 (outbound)");
        kv("block id",   info.block_id);
        kv("shape id",   info.shape_id);
        kv("wheelchair", allowableStr(info.wheelchair_accessible));
        kv("bikes",      allowableStr(info.bikes_allowed));
    } catch (const std::exception& e) {
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
    }
    pauseKey();
}

static void screenTripStops(const string& trip_id) {
    try {
        auto segs = gtfs::getAllStops(trip_id);
        vector<std::pair<double,double>> pts;
        vector<string> lines;
        for (auto& seg : segs) {
            auto st = gtfs::getStopInfo(seg.stop.stop_id);
            pts.push_back({st.stop_lat, st.stop_lon});
            std::ostringstream os;
            os << "  " << col::GRAY << std::right << std::setw(3) << seg.stop.stop_sequence << col::RESET
               << "  " << col::CYAN << seg.stop.arrival_time.leadingRoundedTime() << col::RESET
               << "  " << st.stop_name;
            lines.push_back(os.str());
        }
        showPagedLines("stops along trip " + trip_id, std::to_string(segs.size()) + " stops", lines, [&]() {
            drawAsciiMap(pts);
            cout << "\n";
        });
    } catch (const std::exception& e) {
        header("stops along trip " + trip_id);
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
        pauseKey();
    }
}

static void screenTripsForRoute(const string& route_id) {
    auto y = inputInt("trips for route " + route_id, "year?", gtfs::getToday().year);
    if (!y) return;
    auto m = inputInt("trips for route " + route_id, "month?", gtfs::getToday().month);
    if (!m) return;
    auto d = inputInt("trips for route " + route_id, "day?", gtfs::getToday().day);
    if (!d) return;

    auto trips = gtfs::getAllTrips(route_id);
    vector<gtfs::trip> valid;
    for (auto& t : trips) if (gtfs::isTripValid(t.trip_id, *y, *m, *d)) valid.push_back(gtfs::getTripInfo(t.trip_id));
    vector<string> lines;
    for (auto& t : valid) {
        std::ostringstream os;
        os << "  " << col::CYAN << t.trip_id << col::RESET
           << col::GRAY << "  dir=" << t.direction_id << col::RESET
           << "  " << t.trip_headsign;
        lines.push_back(os.str());
    }
    showPagedLines("trips for " + route_id, fmtDate(*y, *m, *d) + "  —  " + std::to_string(valid.size()) + " trips running", lines);
}

static void screenBlockTrips(const string& block_id) {
    auto trips = gtfs::getAllBlockId(block_id);
    vector<string> lines;
    for (auto& t : trips) lines.push_back("  " + string(col::CYAN) + t.trip_id + col::RESET);
    showPagedLines("block " + block_id, std::to_string(trips.size()) + " trips share this block", lines);
}

static void screenTripsMenu() {
    while (true) {
        int c = runMenu("trips", {
            "look up trip by id",
            "stops along a trip",
            "trips on a route + date",
            "trips sharing a block id"
        }, "everything about a trip");
        if (c == -1) return;
        if (c == 0) { auto id = inputLine("trip lookup", "trip id?"); if (id) screenTripDetail(*id); }
        else if (c == 1) { auto id = inputLine("trip stops", "trip id?"); if (id) screenTripStops(*id); }
        else if (c == 2) { auto hit = searchRouteInteractive("trips on a route — pick a route"); if (hit) screenTripsForRoute(hit->id); }
        else if (c == 3) { auto id = inputLine("block trips", "block id?"); if (id) screenBlockTrips(*id); }
    }
}

static void screenRouteDetail(const string& route_id) {
    header("route " + route_id);
    try {
        auto r = gtfs::getRouteInfo(route_id);
        kv("route id",    r.route_id);
        kv("agency",      r.agency_id);
        kv("short name",  r.route_short_name);
        kv("long name",   r.route_long_name);
        kv("type",        routeTypeStr(r.route_type));
        kv("color",       "#" + r.route_color + "  text #" + r.route_text_color);
    } catch (const std::exception& e) {
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
    }
    pauseKey();
}

static void screenRoutesMenu() {
    while (true) {
        int c = runMenu("routes", {
            "search routes",
            "look up route by id"
        }, "route details & colors");
        if (c == -1) return;
        if (c == 0) { auto hit = searchRouteInteractive(); if (hit) screenRouteDetail(hit->id); }
        else if (c == 1) { auto id = inputLine("route lookup", "route id?"); if (id) screenRouteDetail(*id); }
    }
}

static void screenAgency() {
    auto agencies = gtfs::getAgencyInfo();
    vector<string> lines;
    for (auto& a : agencies) {
        lines.push_back("  " + string(col::BOLD) + a.agency_name + col::RESET);
        lines.push_back(kvLine("  id",       a.agency_id));
        lines.push_back(kvLine("  url",      a.agency_url));
        lines.push_back(kvLine("  timezone", a.agency_timezone));
        lines.push_back(kvLine("  phone",    a.agency_phone));
        lines.push_back("");
    }
    showPagedLines("agencies", std::to_string(agencies.size()) + " agencies", lines);
}

static void screenShape() {
    auto id = inputLine("shape lookup", "shape id?");
    if (!id) return;
    header("shape " + *id);
    try {
        auto pts = gtfs::getShapeInfo(*id);
        vector<std::pair<double,double>> coords;
        for (auto& p : pts) coords.push_back({p.shape_pt_lat, p.shape_pt_lon});
        drawAsciiMap(coords);
        cout << "\n  " << pts.size() << " points\n";
    } catch (const std::exception& e) {
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
    }
    pauseKey();
}

static void screenServiceDetail(const string& service_id) {
    try {
        auto svc = gtfs::getServiceInfo(service_id);
        auto& cal = svc.schedule;
        const char* names[7] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
        bool days[7] = {cal.monday, cal.tuesday, cal.wednesday, cal.thursday, cal.friday, cal.saturday, cal.sunday};

        vector<string> lines;
        for (auto& ex : svc.exceptions) {
            bool added = ex.exception_type == gtfs::calendar_date::added;
            std::ostringstream os;
            os << "  " << fmtDate(ex.date) << "  "
               << (added ? col::GREEN : col::RED) << (added ? "added" : "removed") << col::RESET;
            lines.push_back(os.str());
        }

        showPagedLines("service " + service_id,
                        "active " + fmtDate(cal.start_date) + " to " + fmtDate(cal.end_date),
                        lines, [&]() {
            cout << "  " << col::GRAY << std::left << std::setw(16) << "schedule" << col::RESET;
            for (int i = 0; i < 7; i++) {
                if (days[i]) cout << col::GREEN << col::BOLD << names[i] << " " << col::RESET;
                else cout << col::GRAY << "--- " << col::RESET;
            }
            cout << "\n\n  exceptions (" << lines.size() << "):\n";
        });
    } catch (const std::exception& e) {
        header("service " + service_id);
        cout << col::RED << "  error: " << e.what() << col::RESET << "\n";
        pauseKey();
    }
}

static void screenTripValidOnDate() {
    auto id = inputLine("check trip validity", "trip id?");
    if (!id) return;
    auto today = gtfs::getToday();
    auto y = inputInt("check trip validity", "year?", today.year); if (!y) return;
    auto m = inputInt("check trip validity", "month?", today.month); if (!m) return;
    auto d = inputInt("check trip validity", "day?", today.day); if (!d) return;
    header("trip validity");
    bool valid = gtfs::isTripValid(*id, *y, *m, *d);
    cout << "  trip " << *id << " on " << fmtDate(*y, *m, *d) << ":  "
         << (valid ? col::GREEN : col::RED) << col::BOLD << (valid ? "VALID" : "NOT VALID") << col::RESET << "\n";
    pauseKey();
}

static void screenVerifyFeed() {
    auto today = gtfs::getToday();
    header("verify feed status", fmtDate(today));
    auto status = gtfs::verifyGTFS(today.year, today.month, today.day);
    cout << "  feed status today:  " << col::BOLD << feedStatusStr(status) << col::RESET << "\n";
    pauseKey();
}

static void screenServiceMenu() {
    while (true) {
        int c = runMenu("service & calendars", {
            "look up service id",
            "check if a trip runs on a date",
            "verify feed status (today)"
        });
        if (c == -1) return;
        if (c == 0) { auto id = inputLine("service lookup", "service id?"); if (id) screenServiceDetail(*id); }
        else if (c == 1) screenTripValidOnDate();
        else if (c == 2) screenVerifyFeed();
    }
}

static void screenDistance() {
    auto lat1 = inputDouble("distance calculator", "point A latitude?"); if (!lat1) return;
    auto lon1 = inputDouble("distance calculator", "point A longitude?"); if (!lon1) return;
    auto lat2 = inputDouble("distance calculator", "point B latitude?"); if (!lat2) return;
    auto lon2 = inputDouble("distance calculator", "point B longitude?"); if (!lon2) return;
    header("distance");
    double dist = gtfs::getDistanceKM(*lat1, *lon1, *lat2, *lon2);
    drawAsciiMap({}, {{*lat1, *lon1}, {*lat2, *lon2}});
    cout << "\n  " << col::BOLD << dist << " km" << col::RESET << "\n";
    pauseKey();
}

// ---- realtime ----

static void screenRtStopArrivals() {
    auto hit = searchStopInteractive("live arrivals — pick a stop");
    if (!hit) return;
    string title = "live arrivals — " + hit->label;
    string sub = "stop " + hit->id;
    string toolPath = rtToolDir(g_prog.c_str()) + "/decodeStop";
    auto [ok, out] = runTool(toolPath, hit->id);
    if (!ok && out.find("not built") != string::npos) {
        header(title, sub);
        cout << col::RED << "  " << out << col::RESET << "\n";
        pauseKey();
    } else if (out.empty()) {
        header(title, sub);
        cout << col::GRAY << "  no live data returned\n" << col::RESET;
        pauseKey();
    } else {
        showPagedLines(title, sub, colorizeJsonLines(out));
    }
}

static void screenRtTripStatus() {
    auto stopHit = searchStopInteractive("live trip status — pick a stop first");
    if (!stopHit) return;
    auto today = gtfs::getToday();
    auto deps = gtfs::getDayTimesAtStop(stopHit->id, today.year, today.month, today.day);
    if (deps.empty()) {
        header("live trip status");
        cout << col::GRAY << "  no scheduled trips at this stop today\n" << col::RESET;
        pauseKey();
        return;
    }
    vector<string> items;
    for (auto& seg : deps) {
        auto route = gtfs::getRouteInfo(seg.route_id);
        items.push_back(seg.stop.arrival_time.leadingRoundedTime() + "  " + route.route_short_name + "  " + seg.stop.trip_id);
    }
    int sel = runMenu("pick a scheduled trip", items, stopHit->label + " — today");
    if (sel == -1) return;
    string trip_id = deps[sel].stop.trip_id;

    string title = "live trip status";
    string sub = "trip " + trip_id;
    string toolPath = rtToolDir(g_prog.c_str()) + "/decodeTrip";
    auto [ok, out] = runTool(toolPath, trip_id);
    if (!ok && out.find("not built") != string::npos) {
        header(title, sub);
        cout << col::RED << "  " << out << col::RESET << "\n";
        pauseKey();
    } else if (out.empty()) {
        header(title, sub);
        cout << col::GRAY << "  no live data returned\n" << col::RESET;
        pauseKey();
    } else {
        showPagedLines(title, sub, colorizeJsonLines(out));
    }
}

static void screenRtAlerts() {
    auto hit = searchRouteInteractive("service alerts — pick a route");
    if (!hit) return;
    string title = "service alerts — " + hit->label;
    string sub = "route " + hit->id;
    string toolPath = rtToolDir(g_prog.c_str()) + "/decodeAlerts";
    auto [ok, out] = runTool(toolPath, hit->id);
    if (!ok && out.find("not built") != string::npos) {
        header(title, sub);
        cout << col::RED << "  " << out << col::RESET << "\n";
        pauseKey();
    } else if (out.find("No alerts found") != string::npos || out.empty()) {
        header(title, sub);
        cout << col::GRAY << "  no active alerts for this route\n" << col::RESET;
        pauseKey();
    } else {
        showPagedLines(title, sub, colorizeJsonLines(out));
    }
}

static void screenRealtimeMenu() {
    while (true) {
        int c = runMenu("realtime", {
            "live arrivals at a stop",
            "live status of a trip",
            "service alerts for a route"
        }, "pulls live GTFS-RT feeds via the gtfs-rt decoders");
        if (c == -1) return;
        if (c == 0) screenRtStopArrivals();
        else if (c == 1) screenRtTripStatus();
        else if (c == 2) screenRtAlerts();
    }
}

// ---- settings ----

static void screenSettings() {
    while (true) {
        string saved = readConfigDataPath(g_cfgFile);
        int c = runMenu("settings", {
            "set data path",
            "reset to default data path"
        }, "data path: " + (saved.empty() ? config::root + " (default)" : saved));
        if (c == -1) return;
        if (c == 0) {
            auto p = inputLine("settings", "new data path?");
            if (p) {
                string np = *p;
                if (!np.empty() && np.back() != '/') np += '/';
                writeConfigDataPath(g_cfgFile, np);
                applyRoot(np);
                header("settings");
                cout << col::GREEN << "  saved: " << np << col::RESET << "\n";
                pauseKey();
            }
        } else if (c == 1) {
            std::remove(g_cfgFile.c_str());
            applyRoot(config::root);
            header("settings");
            cout << col::GREEN << "  reset to default data path\n" << col::RESET;
            pauseKey();
        }
    }
}

// ============================================================================
// main menu / entry
// ============================================================================

static void mainMenu() {
    while (true) {
        int c = runMenu("gtfs explorer", {
            "stops",
            "trips",
            "routes",
            "agency",
            "shapes",
            "service & calendars",
            "realtime",
            "distance calculator",
            "settings",
            "quit"
        }, "", "↑/↓ move   enter select   esc/q quit");
        switch (c) {
            case -1: case 9: return;
            case 0: screenStopsMenu(); break;
            case 1: screenTripsMenu(); break;
            case 2: screenRoutesMenu(); break;
            case 3: screenAgency(); break;
            case 4: screenShape(); break;
            case 5: screenServiceMenu(); break;
            case 6: screenRealtimeMenu(); break;
            case 7: screenDistance(); break;
            case 8: screenSettings(); break;
        }
    }
}

static void printHelp(const string& prog) {
    cerr << "Usage: " << prog << " [-c <data_path>]\n\n"
         << "Launches an interactive, keyboard-driven GTFS explorer.\n"
         << "Arrow keys / j,k to move, Enter to select, Esc or q to back out.\n\n"
         << "Options:\n"
         << "  -c, --config <path>  Override the GTFS data path for this run (not saved)\n"
         << "  -h, --help           Show this message\n";
}

int main(int argc, char* argv[]) {
    g_prog = argv[0];
    g_cfgFile = configFilePath(argv[0]);

    string savedPath = readConfigDataPath(g_cfgFile);
    if (!savedPath.empty()) applyRoot(savedPath);

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            applyRoot(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else {
            cerr << "Unknown argument: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        cerr << "gtfs_cli is interactive and needs a real terminal (stdin/stdout must be a tty).\n";
        return 1;
    }

    term::RawMode raw;
    if (!raw.ok) {
        cerr << "failed to enter raw terminal mode\n";
        return 1;
    }
    term::hideCursor();
    try {
        mainMenu();
    } catch (const std::exception& e) {
        term::showCursor();
        term::clear();
        cerr << "fatal error: " << e.what() << "\n";
        return 1;
    }
    term::showCursor();
    term::clear();
    cout << col::ORANGE << "  see you next time!" << col::RESET << "\n";
    return 0;
}
