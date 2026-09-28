// gtfs_cli — interactive, keyboard-only terminal explorer for a GTFS Schedule feed.
// Depends only on gtfs.hpp, config.hpp + the C++/POSIX standard library. Usage: gtfs_cli [feed_dir]
// Build: g++ -std=c++17 -O2 -o gtfs_cli gtfs_cli.cpp
#include "../config/config.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace fs = std::filesystem;
using std::optional;
using std::pair;
using std::vector;

// ============================================================================
// terminal
// ============================================================================

namespace term {

enum class Key { Up, Down, Left, Right, PgUp, PgDn, Home, End, Enter, Escape, Backspace, CtrlC, Char, Eof, Other };
struct KeyEvent { Key key = Key::Other; char ch = 0; };

static termios g_orig{};
static bool g_active = false;
static volatile sig_atomic_t g_busy = 0;   // inside a blocking library call: Ctrl+C quits instead of acting as "back"
static volatile sig_atomic_t g_sigint = 0;

static void restoreNow() {
    if (!g_active) return;
    const char seq[] = "\033[?25h\033[?1049l";
    if (write(STDOUT_FILENO, seq, sizeof(seq) - 1) < 0) { /* nothing useful to do */ }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    g_active = false;
}

static void onFatalSignal(int sig) {
    restoreNow();
    _exit(128 + sig);
}

static void onSigint(int) {
    if (g_busy) { restoreNow(); _exit(130); }
    g_sigint = 1;
}

static void onWinch(int) {} // only exists so a blocked read() returns EINTR and we redraw

// RAII: raw mode + alternate screen + hidden cursor; always undone on scope exit.
struct Screen {
    bool ok = false;
    Screen() {
        if (tcgetattr(STDIN_FILENO, &g_orig) == -1) return;
        termios raw = g_orig;
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN); // ISIG stays on so Ctrl+C can interrupt a long file scan
        raw.c_cc[VQUIT] = raw.c_cc[VSUSP] = _POSIX_VDISABLE;
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return;
        ok = g_active = true;
        struct sigaction sa{};
        sa.sa_handler = onFatalSignal;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
        struct sigaction si{};
        si.sa_handler = onSigint;
        sigaction(SIGINT, &si, nullptr);
        struct sigaction wc{};
        wc.sa_handler = onWinch;
        sigaction(SIGWINCH, &wc, nullptr);
        std::cout << "\033[?1049h\033[?25l" << std::flush;
    }
    ~Screen() { std::cout << std::flush; restoreNow(); }
};

static bool inputWaiting(int ms) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    return poll(&p, 1, ms) > 0;
}

static KeyEvent readKey() {
    g_busy = 0;
    if (g_sigint) { g_sigint = 0; return {Key::CtrlC}; }
    std::cout << std::flush;
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n < 0 && errno == EINTR) {
        if (g_sigint) { g_sigint = 0; return {Key::CtrlC}; }
        return {Key::Other};
    }
    if (n <= 0) return {Key::Eof};
    if (c == 3) return {Key::CtrlC};
    if (c == 27) {
        if (!inputWaiting(40)) return {Key::Escape};
        unsigned char a;
        if (read(STDIN_FILENO, &a, 1) != 1) return {Key::Escape};
        if (a != '[' && a != 'O') return {Key::Escape};
        string params;
        unsigned char f = 0;
        while (read(STDIN_FILENO, &f, 1) == 1) {
            if (f >= 0x40 && f <= 0x7e) break;
            params += static_cast<char>(f);
        }
        switch (f) {
            case 'A': return {Key::Up};
            case 'B': return {Key::Down};
            case 'C': return {Key::Right};
            case 'D': return {Key::Left};
            case 'H': return {Key::Home};
            case 'F': return {Key::End};
            case '~':
                if (params == "5") return {Key::PgUp};
                if (params == "6") return {Key::PgDn};
                if (params == "1" || params == "7") return {Key::Home};
                if (params == "4" || params == "8") return {Key::End};
                break;
        }
        return {Key::Other};
    }
    if (c == '\r' || c == '\n') return {Key::Enter};
    if (c == 127 || c == 8) return {Key::Backspace};
    if (c >= 32) return {Key::Char, static_cast<char>(c)};
    return {Key::Other};
}

static bool keyPending() { return inputWaiting(0); }

static pair<int, int> size() { // rows, cols
    winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0 && w.ws_col > 0) return {w.ws_row, w.ws_col};
    return {24, 80};
}

} // namespace term

using term::Key;
using term::KeyEvent;

// ============================================================================
// drawing primitives
// ============================================================================

namespace col {
static const char* RESET = "\033[0m";
static const char* BOLD = "\033[1m";
static const char* DIM = "\033[2m";
static const char* ORANGE = "\033[38;5;208m";
static const char* CYAN = "\033[38;5;80m";
static const char* GREEN = "\033[38;5;114m";
static const char* YELLOW = "\033[38;5;221m";
static const char* RED = "\033[38;5;203m";
static const char* GRAY = "\033[38;5;244m";
static const char* INVERT = "\033[7m";
} // namespace col

static size_t utf8Len(unsigned char c) { return c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; }

static string stripAnsi(const string& s) {
    string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\033') {
            size_t j = i + 1;
            if (j < s.size() && s[j] == '[') {
                ++j;
                while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7e)) ++j;
            }
            i = j < s.size() ? j + 1 : j;
        } else out += s[i++];
    }
    return out;
}

static size_t visLen(const string& s) {
    size_t n = 0;
    const string p = stripAnsi(s);
    for (size_t i = 0; i < p.size(); i += utf8Len(static_cast<unsigned char>(p[i]))) ++n;
    return n;
}

// Truncates to `width` visible columns (ANSI-aware), adding an ellipsis when cut.
static string clip(const string& s, size_t width) {
    if (visLen(s) <= width) return s;
    if (width == 0) return "";
    string out;
    size_t vis = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\033') {
            size_t j = i + 1;
            if (j < s.size() && s[j] == '[') {
                ++j;
                while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7e)) ++j;
                if (j < s.size()) ++j;
            }
            out.append(s, i, j - i);
            i = j;
            continue;
        }
        if (vis >= width - 1) break;
        size_t len = utf8Len(static_cast<unsigned char>(s[i]));
        out.append(s, i, len);
        i += len;
        ++vis;
    }
    return out + "…" + col::RESET;
}

static string padRight(const string& s, size_t width) {
    size_t v = visLen(s);
    return v >= width ? s : s + string(width - v, ' ');
}

static int g_headerLines = 3;

static int header(const string& title, const string& subtitle = "") {
    auto [rows, cols] = term::size();
    (void)rows;
    std::cout << "\033[2J\033[H";
    std::cout << col::ORANGE << col::BOLD << "  ▸ gtfs" << col::RESET << col::DIM << "  —  " << col::RESET << col::BOLD
              << clip(title, cols > 16 ? cols - 14 : 2) << col::RESET << "\n";
    int lines = 2;
    if (!subtitle.empty()) {
        std::cout << col::GRAY << "    " << clip(subtitle, cols > 8 ? cols - 6 : 2) << col::RESET << "\n";
        ++lines;
    }
    std::cout << col::GRAY << "  " << string(std::min<size_t>(60, cols > 4 ? cols - 4 : 1), '-') << col::RESET << "\n\n";
    return g_headerLines = lines + 1;
}

static void footer(const string& hint) {
    auto [rows, cols] = term::size();
    (void)rows;
    std::cout << "\n" << col::GRAY << "  " << string(std::min<size_t>(60, cols > 4 ? cols - 4 : 1), '-') << "\n  "
              << clip(hint, cols > 4 ? cols - 4 : 1) << col::RESET << "\n";
}

static void busy(const string& msg) {
    term::g_busy = 1;
    std::cout << "  " << col::DIM << msg << "  (ctrl+c quits)" << col::RESET << std::flush;
}

static void pauseKey(const string& msg = "press any key to continue") {
    std::cout << "\n" << col::DIM << "  " << msg << col::RESET;
    term::readKey();
}

static void message(const string& title, const string& text, const char* color = col::GRAY) {
    header(title);
    std::cout << color << "  " << text << col::RESET << "\n";
    pauseKey();
}

static string kvLine(const string& k, const string& v) {
    return "  " + string(col::GRAY) + padRight(k, 16) + col::RESET + (v.empty() ? string(col::DIM) + "—" + col::RESET : v);
}

static void kvScreen(const string& title, const string& subtitle, const vector<pair<string, string>>& rows) {
    header(title, subtitle);
    for (const auto& [k, v] : rows) std::cout << kvLine(k, v) << "\n";
    pauseKey();
}

// ============================================================================
// list widget — used for menus and every drill-down list
// ============================================================================

static constexpr int kExit = -2; // pickList result: onKey asked the caller to re-run its loop

struct ListOpts {
    string title, subtitle;
    string hint = "↑/↓ move   enter select   esc back";
    std::function<int()> preamble;                 // prints extra lines under the header, returns how many
    std::function<int(const KeyEvent&)> onKey;     // 0 = not handled, 1 = handled (redraw), 2 = leave widget (kExit)
    vector<bool> disabled;
    bool numberKeys = false;
    int initial = 0;
};

// Returns the chosen row, -1 when the user backed out, kExit when onKey requested it.
static int pickList(const vector<string>& rows, const ListOpts& o) {
    const int n = static_cast<int>(rows.size());
    auto isOff = [&](int i) { return i >= 0 && i < static_cast<int>(o.disabled.size()) && o.disabled[i]; };
    auto step = [&](int from, int dir) {
        for (int k = 1; k <= n; ++k) {
            int i = ((from + dir * k) % n + n) % n;
            if (!isOff(i)) return i;
        }
        return from;
    };
    int sel = n ? std::clamp(o.initial, 0, n - 1) : 0;
    if (n && isOff(sel)) sel = step(sel, 1);
    int top = 0;

    while (true) {
        int hdr = header(o.title, o.subtitle);
        int used = o.preamble ? o.preamble() : 0;
        auto [R, C] = term::size();
        int page = std::max(3, R - hdr - used - 5);
        if (sel < top) top = sel;
        if (sel >= top + page) top = sel - page + 1;
        if (n <= page) top = 0;

        if (n == 0) std::cout << col::GRAY << "  (nothing here)" << col::RESET << "\n";
        for (int i = top; i < std::min(n, top + page); ++i) {
            size_t w = C > 8 ? C - 6 : 2;
            if (i == sel) std::cout << col::ORANGE << col::BOLD << "  ❯ " << clip(stripAnsi(rows[i]), w) << col::RESET << "\n";
            else if (isOff(i)) std::cout << "    " << col::DIM << clip(stripAnsi(rows[i]), w) << col::RESET << "\n";
            else std::cout << "    " << clip(rows[i], w) << "\n";
        }
        if (n > 0) std::cout << "\n" << col::GRAY << "  " << sel + 1 << "/" << n << col::RESET << "\n";
        footer(o.hint);

        KeyEvent ev = term::readKey();
        if (o.onKey) {
            int r = o.onKey(ev);
            if (r == 2) return kExit;
            if (r == 1) continue;
        }
        switch (ev.key) {
            case Key::Up: if (n) sel = step(sel, -1); break;
            case Key::Down: if (n) sel = step(sel, 1); break;
            case Key::PgUp: case Key::Left: sel = std::max(0, sel - page); if (isOff(sel)) sel = step(sel, 1); break;
            case Key::PgDn: case Key::Right: sel = std::min(std::max(n - 1, 0), sel + page); if (isOff(sel)) sel = step(sel, -1); break;
            case Key::Home: sel = 0; if (n && isOff(sel)) sel = step(sel, 1); break;
            case Key::End: sel = std::max(n - 1, 0); if (n && isOff(sel)) sel = step(sel, -1); break;
            case Key::Enter: if (n && !isOff(sel)) return sel; break;
            case Key::Escape: case Key::CtrlC: case Key::Eof: return -1;
            case Key::Char:
                if (ev.ch == 'q' || ev.ch == 'Q') return -1;
                if (ev.ch == 'j' && n) sel = step(sel, 1);
                else if (ev.ch == 'k' && n) sel = step(sel, -1);
                else if (ev.ch == 'g') { sel = 0; if (n && isOff(sel)) sel = step(sel, 1); }
                else if (ev.ch == 'G') { sel = std::max(n - 1, 0); if (n && isOff(sel)) sel = step(sel, -1); }
                else if (o.numberKeys && ev.ch >= '1' && ev.ch <= '9') {
                    int idx = ev.ch - '1';
                    if (idx < n && !isOff(idx)) return idx;
                }
                break;
            default: break;
        }
    }
}

static int runMenu(const string& title, const vector<string>& items, const string& subtitle = "",
                   const vector<bool>& disabled = {}, const string& hint = "↑/↓ move   enter select   1-9 jump   esc back") {
    ListOpts o;
    o.title = title; o.subtitle = subtitle; o.disabled = disabled; o.hint = hint; o.numberKeys = true;
    vector<string> rows;
    for (size_t i = 0; i < items.size(); ++i) rows.push_back((i < 9 ? std::to_string(i + 1) + "  " : "   ") + items[i]);
    return pickList(rows, o);
}

// ============================================================================
// text input
// ============================================================================

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == string::npos ? "" : s.substr(a, b - a + 1);
}

static void popUtf8(string& s) {
    if (s.empty()) return;
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

static optional<string> inputLine(const string& title, const string& prompt, const string& subtitle = "", const string& def = "") {
    string buf;
    while (true) {
        header(title, subtitle);
        std::cout << "  " << prompt;
        if (!def.empty()) std::cout << " " << col::DIM << "[" << def << "]" << col::RESET;
        std::cout << "\n\n  " << col::CYAN << "> " << col::RESET << buf << col::INVERT << " " << col::RESET << "\n";
        footer("type   enter confirm   esc cancel");
        KeyEvent ev = term::readKey();
        if (ev.key == Key::Enter) { string t = trim(buf); return t.empty() ? def : t; }
        if (ev.key == Key::Escape || ev.key == Key::CtrlC || ev.key == Key::Eof) return std::nullopt;
        if (ev.key == Key::Backspace) popUtf8(buf);
        else if (ev.key == Key::Char) buf += ev.ch;
    }
}

static optional<double> inputDouble(const string& title, const string& prompt) {
    while (true) {
        auto s = inputLine(title, prompt);
        if (!s) return std::nullopt;
        try {
            size_t used = 0;
            double v = std::stod(*s, &used);
            if (used == s->size()) return v;
        } catch (...) {}
        message(title, "not a number, try again", col::RED);
    }
}

static string fmtDate(const gtfs::calendar_day& d) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
    return buf;
}

static const char* weekdayName(const gtfs::calendar_day& d) {
    static const char* names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return names[static_cast<int>(gtfs::convertDateToWeek(d.year, d.month, d.day))];
}

static gtfs::calendar_day addDays(const gtfs::calendar_day& d, int delta) {
    std::tm tm{};
    tm.tm_year = d.year - 1900;
    tm.tm_mon = d.month - 1;
    tm.tm_mday = d.day + delta;
    tm.tm_hour = 12;
    std::mktime(&tm);
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

static optional<gtfs::calendar_day> parseDate(const string& in) {
    string digits;
    for (char c : in) if (c != '-' && c != '/' && c != ' ') digits += c;
    if (digits.size() != 8 || digits.find_first_not_of("0123456789") != string::npos) return std::nullopt;
    gtfs::calendar_day d(std::stoi(digits.substr(0, 4)), std::stoi(digits.substr(4, 2)), std::stoi(digits.substr(6, 2)));
    if (d.month < 1 || d.month > 12 || d.day < 1) return std::nullopt;
    if (!(addDays({d.year, d.month, 1}, d.day - 1) == d)) return std::nullopt;
    return d;
}

static optional<gtfs::calendar_day> inputDate(const string& title, const string& subtitle = "") {
    const gtfs::calendar_day today = gtfs::getToday();
    while (true) {
        auto s = inputLine(title, "date (YYYY-MM-DD)", subtitle, fmtDate(today));
        if (!s) return std::nullopt;
        if (auto d = parseDate(*s)) return d;
        message(title, "not a valid date, try again", col::RED);
    }
}

// ============================================================================
// feed state (the CLI-side memo caches are safe: the library itself stays cache-free)
// ============================================================================

struct Caps { bool calendar = false, calendarDates = false, shapes = false, agency = false, feedInfo = false, frequencies = false, fares = false; };

static struct {
    std::unique_ptr<gtfs::data_feed> feed;
    Caps caps;
    std::map<string, gtfs::route> routes;
    std::map<string, gtfs::stop> stops;
} g;

static const gtfs::data_feed& feed() { return *g.feed; }

static bool fileExists(const string& p) { return std::ifstream(p).good(); }

static string feedName() {
    string n = fs::path(feed().path).parent_path().filename().string();
    return n.empty() ? feed().path : n;
}

static string expandPath(string p) {
    p = trim(p);
    if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') || (p.front() == '\'' && p.back() == '\''))) p = p.substr(1, p.size() - 2);
    if (!p.empty() && p[0] == '~') if (const char* h = std::getenv("HOME")) p = string(h) + p.substr(1);
    return p;
}

// Returns an error message, or "" on success.
static string loadFeed(const string& rawPath) {
    string path = expandPath(rawPath);
    if (path.empty()) path = ".";
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return "not a directory: " + path;
    auto f = std::make_unique<gtfs::data_feed>(path);
    string missing;
    for (const auto& [name, p] : vector<pair<string, string>>{{"stops.txt", f->stopPath}, {"routes.txt", f->routePath},
                                                             {"trips.txt", f->tripsPath}, {"stop_times.txt", f->stopTimesPath}})
        if (!fileExists(p)) missing += (missing.empty() ? "" : ", ") + name;
    if (!missing.empty()) return "missing required file(s) in " + path + ": " + missing;

    g.caps = {fileExists(f->calendarPath), fileExists(f->calendarDatesPath), fileExists(f->shapePath), fileExists(f->agencyPath),
              fileExists(f->feedInfoFile), fileExists(f->frequencyPath), fileExists(f->fareAttributesPath)};
    g.routes.clear();
    g.stops.clear();
    g.feed = std::move(f);
    return "";
}

static const gtfs::route& routeOf(const string& id) {
    auto it = g.routes.find(id);
    if (it == g.routes.end()) it = g.routes.emplace(id, gtfs::getRouteInfo(feed(), id)).first;
    return it->second;
}

static const gtfs::stop& stopOf(const string& id) {
    auto it = g.stops.find(id);
    if (it == g.stops.end()) it = g.stops.emplace(id, gtfs::getStopInfo(feed(), id)).first;
    return it->second;
}

static string routeLabel(const string& route_id) {
    const auto& r = routeOf(route_id);
    if (!r.route_short_name.empty()) return r.route_short_name;
    if (!r.route_long_name.empty()) return r.route_long_name;
    return route_id;
}

// ============================================================================
// formatting
// ============================================================================

static string hhmm(const gtfs::time& t) {
    if (t.h < 0) return "--:--";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t.h, t.m);
    return buf;
}

static string routeTypeStr(gtfs::route::type t) {
    switch (t) {
        case gtfs::route::light_rail: return "light rail / tram";
        case gtfs::route::underground: return "subway / metro";
        case gtfs::route::rail: return "rail";
        case gtfs::route::bus: return "bus";
        case gtfs::route::ferry: return "ferry";
        case gtfs::route::cable_tram: return "cable tram";
        case gtfs::route::aerial_lift: return "aerial lift";
        case gtfs::route::funicular: return "funicular";
        case gtfs::route::trolleybus: return "trolleybus";
        case gtfs::route::monorail: return "monorail";
        default: return "";
    }
}

static string allowableStr(gtfs::trip::allowable a) {
    switch (a) {
        case gtfs::trip::no_info: return "no information";
        case gtfs::trip::allowed: return "yes";
        case gtfs::trip::not_allowed: return "no";
        default: return "";
    }
}

static string locationTypeStr(gtfs::stop::location l) {
    switch (l) {
        case gtfs::stop::stop_platform: return "stop / platform";
        case gtfs::stop::station: return "station";
        case gtfs::stop::entrance_exit: return "entrance / exit";
        case gtfs::stop::generic_node: return "generic node";
        case gtfs::stop::boarding_area: return "boarding area";
        default: return "";
    }
}

static string wheelchairStr(gtfs::stop::wheelchair w) {
    switch (w) {
        case gtfs::stop::no_info: return "no information / inherit";
        case gtfs::stop::wheelchair_boarding_supported: return "accessible";
        case gtfs::stop::no_wheelchair_boarding: return "not accessible";
        default: return "";
    }
}

static string coordStr(double lat, double lon) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6f, %.6f", lat, lon);
    return buf;
}

static string stopRow(const string& name, const string& id, const string& code) {
    string extra = "#" + id;
    if (!code.empty() && code != id) extra += "  code " + code;
    return name + "  " + col::DIM + extra + col::RESET;
}

static string routeRow(const string& id, const string& shortName, const string& longName) {
    return string(col::BOLD) + padRight(shortName.empty() ? id : shortName, 6) + col::RESET + " " + longName + "  " + col::DIM + "#" + id + col::RESET;
}

static string feedStatusStr(gtfs::feedStatus s) {
    switch (s) {
        case gtfs::in_use: return "in use";
        case gtfs::expired: return "expired";
        case gtfs::upcoming: return "upcoming (not started yet)";
        case gtfs::no_result_a: return "unknown (feed_info.txt has no feed_end_date)";
        case gtfs::no_result_b: return "unknown (feed_info.txt has no feed_start_date)";
        default: return "unknown (no data in feed_info.txt)";
    }
}

// ============================================================================
// ASCII scatter map (longitude is scaled by cos(latitude); terminal cells are ~2x taller than wide)
// ============================================================================

struct MapData {
    vector<pair<double, double>> pts;   // lat, lon
    vector<pair<double, double>> marks; // highlighted
};

static int drawMap(const MapData& m, int width, int height) {
    if (m.pts.empty() && m.marks.empty()) return 0;
    double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
    auto grow = [&](const pair<double, double>& p) {
        minLat = std::min(minLat, p.first); maxLat = std::max(maxLat, p.first);
        minLon = std::min(minLon, p.second); maxLon = std::max(maxLon, p.second);
    };
    for (auto& p : m.pts) grow(p);
    for (auto& p : m.marks) grow(p);
    const double lonScale = std::cos((minLat + maxLat) / 2 * 3.14159265 / 180.0);
    const double spanX = std::max((maxLon - minLon) * lonScale, 1e-6), spanY = std::max(maxLat - minLat, 1e-6);
    const double s = std::min((width - 1) / spanX, 2.0 * (height - 1) / spanY);
    const double offX = ((width - 1) - spanX * s) / 2, offY = ((height - 1) - spanY * s / 2) / 2;

    vector<string> grid(height, string(width, ' '));
    vector<vector<char>> kind(height, vector<char>(width, 0));
    auto plot = [&](const pair<double, double>& p, char c, char k) {
        int x = std::clamp(static_cast<int>(std::lround(offX + (p.second - minLon) * lonScale * s)), 0, width - 1);
        int y = std::clamp(static_cast<int>(std::lround(offY + (maxLat - p.first) * s / 2)), 0, height - 1);
        if (kind[y][x] > k) return;
        grid[y][x] = c;
        kind[y][x] = k;
    };
    for (auto& p : m.pts) plot(p, 'o', 1);
    for (auto& p : m.marks) plot(p, '@', 2);
    for (int y = 0; y < height; ++y) {
        std::cout << "  ";
        for (int x = 0; x < width; ++x) {
            if (kind[y][x] == 2) std::cout << col::ORANGE << col::BOLD << grid[y][x] << col::RESET;
            else if (kind[y][x] == 1) std::cout << col::CYAN << grid[y][x] << col::RESET;
            else std::cout << ' ';
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    return height + 1;
}

// A map that can be toggled with 'm' inside a pickList; drawn only when on.
struct MapPane {
    MapData data;
    bool on = false;
    int draw() const {
        if (!on) return 0;
        auto [R, C] = term::size();
        return drawMap(data, std::clamp(C - 6, 20, 72), std::clamp(R / 3, 6, 16));
    }
    int handle(const KeyEvent& ev) {
        if (ev.key == Key::Char && ev.ch == 'm') { on = !on; return 1; }
        return 0;
    }
};

// ============================================================================
// search picker — type to search, ↑/↓ to choose, enter to open
// ============================================================================

using SearchFn = std::function<vector<pair<string, string>>(const string&)>; // -> (id, row text)

static optional<string> searchPicker(const string& title, const string& prompt, const SearchFn& search, string& query) {
    vector<pair<string, string>> hits;
    int sel = 0, top = 0;
    bool dirty = !query.empty();
    while (true) {
        if (dirty && !term::keyPending()) { // typing fast? wait until the burst ends before scanning files
            hits.clear();
            if (!trim(query).empty()) {
                busy("searching…");
                hits = search(trim(query));
                if (hits.size() > 200) hits.resize(200);
            }
            sel = top = 0;
            dirty = false;
        }
        int hdr = header(title);
        auto [R, C] = term::size();
        std::cout << "  " << col::GRAY << prompt << col::RESET << "\n  " << col::CYAN << "> " << col::RESET << query << col::INVERT << " " << col::RESET << "\n\n";
        int page = std::max(3, R - hdr - 8);
        const int n = static_cast<int>(hits.size());
        if (sel < top) top = sel;
        if (sel >= top + page) top = sel - page + 1;
        if (n == 0) std::cout << col::GRAY << "  " << (trim(query).empty() ? "start typing…" : "no matches") << col::RESET << "\n";
        for (int i = top; i < std::min(n, top + page); ++i) {
            size_t w = C > 8 ? C - 6 : 2;
            if (i == sel) std::cout << col::ORANGE << col::BOLD << "  ❯ " << clip(stripAnsi(hits[i].second), w) << col::RESET << "\n";
            else std::cout << "    " << clip(hits[i].second, w) << "\n";
        }
        if (n > 0) std::cout << "\n" << col::GRAY << "  " << sel + 1 << "/" << n << (n == 200 ? "+" : "") << col::RESET << "\n";
        footer("type to search   ↑/↓ move   enter open   esc back");

        KeyEvent ev = term::readKey();
        switch (ev.key) {
            case Key::Up: if (sel > 0) --sel; break;
            case Key::Down: if (sel + 1 < n) ++sel; break;
            case Key::PgUp: sel = std::max(0, sel - page); break;
            case Key::PgDn: sel = std::min(std::max(n - 1, 0), sel + page); break;
            case Key::Enter: if (n) return hits[sel].first; break;
            case Key::Escape: case Key::CtrlC: case Key::Eof: return std::nullopt;
            case Key::Backspace: popUtf8(query); dirty = true; break;
            case Key::Char: query += ev.ch; dirty = true; break;
            default: break;
        }
    }
}

static void searchLoop(const string& title, const string& prompt, const SearchFn& search, const std::function<void(const string&)>& open) {
    string query;
    while (auto id = searchPicker(title, prompt, search, query)) open(*id);
}

// ============================================================================
// screens
// ============================================================================

template <class F> static void guarded(const string& title, F&& f) {
    try { f(); }
    catch (const std::exception& e) { message(title, string("error: ") + e.what(), col::RED); }
}

static void notFound(const string& kind, const string& id) { message(kind + " lookup", kind + " '" + id + "' not found in this feed", col::YELLOW); }

// ---- stops ----

static void stopDetails(const string& stop_id) {
    const auto& s = stopOf(stop_id);
    kvScreen("stop " + stop_id, s.stop_name, {
        {"stop id", s.stop_id}, {"stop code", s.stop_code}, {"name", s.stop_name}, {"coordinates", coordStr(s.stop_lat, s.stop_lon)},
        {"type", locationTypeStr(s.location_type)}, {"parent station", s.parent_station}, {"platform", s.platform_code},
        {"zone", s.zone_id}, {"wheelchair", wheelchairStr(s.wheelchair_boarding)}, {"timezone", s.stop_timezone},
        {"description", s.stop_desc}, {"url", s.stop_url}});
}

static void tripMenu(const string& trip_id);
static void stopMenu(const string& stop_id);
static void routeMenu(const string& route_id);

// A parent station has no stop_times of its own; its platforms (children) do.
static vector<string> platformsOf(const string& stop_id) {
    const auto& s = stopOf(stop_id);
    if (s.location_type != gtfs::stop::station) return {stop_id};
    vector<string> ids;
    for (const auto& c : gtfs::getNearestStops(feed(), s.stop_lat, s.stop_lon, -1, 1.0)) {
        const bool isPlatform = c.location_type == gtfs::stop::stop_platform || c.location_type == gtfs::stop::loc_undef;
        if (c.parent_station == stop_id && isPlatform) ids.push_back(c.stop_id); // entrances/nodes have no stop_times
    }
    std::sort(ids.begin(), ids.end());
    return ids.empty() ? vector<string>{stop_id} : ids;
}

static void screenDepartures(const string& stop_id, bool fromNow) {
    const auto& st = stopOf(stop_id);
    gtfs::calendar_day date = gtfs::getToday();
    while (true) {
        header(fromNow ? "remaining departures" : "departures", st.stop_name);
        busy("finding platforms…");
        const vector<string> ids = platformsOf(stop_id);
        header(fromNow ? "remaining departures" : "departures", st.stop_name);
        busy("reading stop_times.txt once per platform (" + std::to_string(ids.size()) + "); large feeds can take a while…");
        const gtfs::time now = gtfs::getCurrentTime();
        vector<gtfs::trip_segment> deps;
        for (const auto& id : ids) {
            auto part = gtfs::getDayTimesAtStop(feed(), id, date);
            deps.insert(deps.end(), part.begin(), part.end());
        }
        if (fromNow) deps.erase(std::remove_if(deps.begin(), deps.end(), [&](const gtfs::trip_segment& x) { return x.stop.departure_time < now; }), deps.end());
        std::stable_sort(deps.begin(), deps.end(), [](const gtfs::trip_segment& a, const gtfs::trip_segment& b) {
            return a.stop.departure_time < b.stop.departure_time;
        });

        vector<string> rows;
        int initial = 0;
        const bool isToday = date == gtfs::getToday();
        for (size_t i = 0; i < deps.size(); ++i) {
            const auto& d = deps[i];
            if (!fromNow && isToday && d.stop.departure_time < now) initial = static_cast<int>(i) + 1;
            const auto& r = routeOf(d.route_id);
            string platform = ids.size() > 1 ? padRight(d.stop.stop_id, 6) + " " : "";
            rows.push_back(string(col::CYAN) + hhmm(d.stop.departure_time) + col::RESET + "  " + col::DIM + platform + col::RESET + col::BOLD +
                           padRight(routeLabel(d.route_id), 6) + col::RESET + " " + r.route_long_name + "  " + col::DIM + d.stop.trip_id + col::RESET);
        }

        ListOpts o;
        o.title = fromNow ? "remaining departures" : "departures";
        o.subtitle = st.stop_name + (ids.size() > 1 ? " (station, " + std::to_string(ids.size()) + " platforms)" : "") + "  —  " +
                     (fromNow ? string("from ") + hhmm(now) + " today" : string(weekdayName(date)) + " " + fmtDate(date)) + "  —  " +
                     std::to_string(deps.size()) + " departures";
        if (deps.empty() && g.caps.feedInfo) o.subtitle += "  —  feed is " + feedStatusStr(gtfs::verifyGTFS(feed(), date));
        o.hint = fromNow ? "↑/↓ move   enter open trip   esc back" : "↑/↓ move   ←/→ prev/next day   d pick date   t today   enter open trip   esc back";
        o.initial = std::min(initial, std::max(0, static_cast<int>(rows.size()) - 1));
        if (!fromNow) o.onKey = [&](const KeyEvent& ev) {
            if (ev.key == Key::Left) { date = addDays(date, -1); return 2; }
            if (ev.key == Key::Right) { date = addDays(date, 1); return 2; }
            if (ev.key == Key::Char && ev.ch == 't') { date = gtfs::getToday(); return 2; }
            if (ev.key == Key::Char && ev.ch == 'd') {
                if (auto nd = inputDate("departures date", st.stop_name)) { date = *nd; return 2; }
                return 1;
            }
            return 0;
        };
        while (true) {
            int r = pickList(rows, o);
            if (r == kExit) break;
            if (r < 0) return;
            o.initial = r;
            guarded("trip", [&] { tripMenu(deps[r].stop.trip_id); });
        }
    }
}

static void screenNearby(double lat, double lon, const string& label) {
    header("nearby stops", label);
    busy("reading stops.txt…");
    auto stops = gtfs::getNearestStops(feed(), lat, lon, 40, -1);
    MapPane map;
    map.data.marks.push_back({lat, lon});
    vector<string> rows;
    for (const auto& s : stops) {
        map.data.pts.push_back({s.stop_lat, s.stop_lon});
        char km[24];
        std::snprintf(km, sizeof(km), "%6.2f km", gtfs::getDistanceKM(lat, lon, s.stop_lat, s.stop_lon));
        rows.push_back(string(col::GRAY) + km + col::RESET + "  " + stopRow(s.stop_name, s.stop_id, s.stop_code));
    }
    ListOpts o;
    o.title = "nearby stops";
    o.subtitle = label + "  —  closest " + std::to_string(stops.size());
    o.hint = "↑/↓ move   enter open stop   m map   esc back";
    o.preamble = [&] { return map.draw(); };
    o.onKey = [&](const KeyEvent& ev) { return map.handle(ev); };
    while (true) {
        int r = pickList(rows, o);
        if (r < 0) return;
        o.initial = r;
        const string id = stops[r].stop_id;
        guarded("stop", [&] { stopMenu(id); });
    }
}

static void stopMenu(const string& stop_id) {
    header("stop " + stop_id);
    busy("loading…");
    const auto& s = stopOf(stop_id);
    if (s.stop_id.empty()) { notFound("stop", stop_id); return; }
    while (true) {
        int c = runMenu("stop " + stop_id, {"details", "departures (pick a day)", "remaining departures today", "stops near here"},
                        s.stop_name + "  —  " + coordStr(s.stop_lat, s.stop_lon));
        if (c < 0) return;
        guarded("stop", [&] {
            if (c == 0) stopDetails(stop_id);
            else if (c == 1) screenDepartures(stop_id, false);
            else if (c == 2) screenDepartures(stop_id, true);
            else screenNearby(s.stop_lat, s.stop_lon, "near " + s.stop_name);
        });
    }
}

static void menuStops() {
    while (true) {
        int c = runMenu("stops", {"search stops (name, id or code)", "look up stop by id", "find stops near a coordinate"});
        if (c < 0) return;
        guarded("stops", [&] {
            if (c == 0) {
                searchLoop("search stops", "stop name, id or code", [](const string& q) {
                    vector<pair<string, string>> out;
                    for (const auto& m : gtfs::searchStop(feed(), q)) {
                        out.push_back({m.stop_id, stopRow(m.text.str, m.stop_id, m.stop_code)});
                        if (out.size() >= 200) break;
                    }
                    return out;
                }, [](const string& id) { guarded("stop", [&] { stopMenu(id); }); });
            } else if (c == 1) {
                if (auto id = inputLine("stop lookup", "stop id")) stopMenu(*id);
            } else {
                auto lat = inputDouble("nearby stops", "latitude");
                if (!lat) return;
                auto lon = inputDouble("nearby stops", "longitude");
                if (!lon) return;
                screenNearby(*lat, *lon, "near " + coordStr(*lat, *lon));
            }
        });
    }
}

// ---- trips ----

static void screenTripStops(const gtfs::trip& t) {
    header("stops along trip " + t.trip_id, t.trip_headsign);
    busy("reading stop_times.txt (large feeds take a few seconds)…");
    auto segs = gtfs::getAllStops(feed(), t.trip_id);
    std::stable_sort(segs.begin(), segs.end(), [](const gtfs::trip_segment& a, const gtfs::trip_segment& b) {
        return a.stop.stop_sequence < b.stop.stop_sequence;
    });
    MapPane map;
    vector<string> rows;
    for (const auto& seg : segs) {
        const auto& s = stopOf(seg.stop.stop_id);
        map.data.pts.push_back({s.stop_lat, s.stop_lon});
        const gtfs::time& tm = seg.stop.arrival_time.h >= 0 ? seg.stop.arrival_time : seg.stop.departure_time;
        rows.push_back(string(col::GRAY) + padRight(std::to_string(seg.stop.stop_sequence), 3) + col::RESET + " " + col::CYAN + hhmm(tm) + col::RESET + "  " +
                       stopRow(s.stop_name.empty() ? seg.stop.stop_id : s.stop_name, seg.stop.stop_id, s.stop_code));
    }
    if (!map.data.pts.empty()) { map.data.marks.push_back(map.data.pts.front()); map.data.marks.push_back(map.data.pts.back()); }
    ListOpts o;
    o.title = "stops along trip " + t.trip_id;
    o.subtitle = (t.trip_headsign.empty() ? "" : t.trip_headsign + "  —  ") + std::to_string(segs.size()) + " stops";
    o.hint = "↑/↓ move   enter open stop   m map   esc back";
    o.preamble = [&] { return map.draw(); };
    o.onKey = [&](const KeyEvent& ev) { return map.handle(ev); };
    while (true) {
        int r = pickList(rows, o);
        if (r < 0) return;
        o.initial = r;
        const string id = segs[r].stop.stop_id;
        guarded("stop", [&] { stopMenu(id); });
    }
}

static void screenShape(const string& shape_id) {
    header("shape " + shape_id);
    busy("reading shapes.txt…");
    auto pts = gtfs::getShapeInfo(feed(), shape_id);
    header("shape " + shape_id, std::to_string(pts.size()) + " points");
    if (pts.empty()) { std::cout << col::YELLOW << "  no points found for that shape id\n" << col::RESET; pauseKey(); return; }
    MapData m;
    for (const auto& p : pts) m.pts.push_back({p.shape_pt_lat, p.shape_pt_lon});
    m.marks = {m.pts.front(), m.pts.back()};
    auto [R, C] = term::size();
    drawMap(m, std::clamp(C - 6, 20, 100), std::clamp(R - 8, 6, 30));
    std::cout << col::GRAY << "  @ = first / last point" << col::RESET << "\n";
    pauseKey();
}

static void screenTripsSharingBlock(const string& block_id) {
    header("block " + block_id);
    busy("reading trips.txt…");
    auto trips = gtfs::getAllBlockId(feed(), block_id);
    vector<string> rows;
    for (const auto& t : trips) rows.push_back(t.trip_id);
    std::sort(rows.begin(), rows.end());
    ListOpts o;
    o.title = "block " + block_id;
    o.subtitle = std::to_string(rows.size()) + " trips share this block";
    o.hint = "↑/↓ move   enter open trip   esc back";
    while (true) {
        int r = pickList(rows, o);
        if (r < 0) return;
        o.initial = r;
        guarded("trip", [&] { tripMenu(rows[r]); });
    }
}

static void screenServiceInfo(const string& service_id) {
    header("service " + service_id);
    busy("reading calendar files…");
    auto svc = gtfs::getServiceInfo(feed(), service_id);
    const auto& cal = svc.schedule;
    if (cal.start_date.year < 0 && svc.exceptions.empty()) { notFound("service", service_id); return; }
    static const char* names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    const bool days[7] = {cal.monday, cal.tuesday, cal.wednesday, cal.thursday, cal.friday, cal.saturday, cal.sunday};
    vector<string> rows;
    auto exs = svc.exceptions;
    std::sort(exs.begin(), exs.end(), [](const gtfs::calendar_date& a, const gtfs::calendar_date& b) { return a.date < b.date; });
    for (const auto& e : exs) {
        const bool added = e.exception_type == gtfs::calendar_date::added;
        rows.push_back(fmtDate(e.date) + " " + weekdayName(e.date) + "  " + (added ? col::GREEN : col::RED) + (added ? "service added" : "service removed") + col::RESET);
    }
    ListOpts o;
    o.title = "service " + service_id;
    o.subtitle = cal.start_date.year < 0 ? "no calendar.txt entry (calendar_dates only)" : "active " + fmtDate(cal.start_date) + " to " + fmtDate(cal.end_date);
    o.hint = "↑/↓ scroll   esc back";
    o.preamble = [&] {
        std::cout << "  " << col::GRAY << padRight("weekly", 10) << col::RESET;
        for (int i = 0; i < 7; ++i) {
            if (days[i]) std::cout << col::GREEN << col::BOLD << names[i] << ' ' << col::RESET;
            else std::cout << col::GRAY << "--- " << col::RESET;
        }
        std::cout << "\n\n  " << col::GRAY << "exceptions (" << rows.size() << ")" << col::RESET << "\n";
        return 3;
    };
    pickList(rows, o);
}

static void screenTripFrequencies(const string& trip_id) {
    header("frequencies " + trip_id);
    auto fr = gtfs::getFrequencies(feed(), trip_id);
    vector<string> rows;
    for (const auto& f : fr)
        rows.push_back(hhmm(f.start_time) + " – " + hhmm(f.end_time) + "  every " + std::to_string(f.headway_secs / 60) + " min " + std::to_string(f.headway_secs % 60) + " s  " +
                       col::DIM + (f.exact_times == gtfs::frequency::schedule_based ? "schedule-based" : "frequency-based") + col::RESET);
    ListOpts o;
    o.title = "frequencies " + trip_id;
    o.subtitle = std::to_string(rows.size()) + " frequency windows";
    o.hint = "↑/↓ scroll   esc back";
    pickList(rows, o);
}

static void tripMenu(const string& trip_id) {
    header("trip " + trip_id);
    busy("loading…");
    const gtfs::trip t = gtfs::getTripInfo(feed(), trip_id);
    if (t.trip_id.empty()) { notFound("trip", trip_id); return; }
    const auto& r = routeOf(t.route_id);
    const string routeName = routeLabel(t.route_id);
    while (true) {
        vector<string> items = {"details", "stops along this trip", "route " + routeName, "service " + t.service_id, "frequencies",
                                "shape " + t.shape_id, "trips sharing block " + t.block_id};
        vector<bool> off = {false, false, r.route_id.empty(), !(g.caps.calendar || g.caps.calendarDates), !g.caps.frequencies,
                            t.shape_id.empty() || !g.caps.shapes, t.block_id.empty()};
        int c = runMenu("trip " + trip_id, items, (t.trip_headsign.empty() ? "" : t.trip_headsign + "  —  ") + routeName, off);
        if (c < 0) return;
        guarded("trip", [&] {
            switch (c) {
                case 0: kvScreen("trip " + trip_id, t.trip_headsign, {
                            {"trip id", t.trip_id}, {"headsign", t.trip_headsign}, {"short name", t.trip_short_name},
                            {"route", t.route_id + (r.route_long_name.empty() ? "" : "  (" + routeName + " — " + r.route_long_name + ")")},
                            {"service id", t.service_id}, {"direction", t.direction_id ? "1" : "0"}, {"block id", t.block_id}, {"shape id", t.shape_id},
                            {"wheelchair", allowableStr(t.wheelchair_accessible)}, {"bikes", allowableStr(t.bikes_allowed)}, {"cars", allowableStr(t.cars_allowed)}});
                        break;
                case 1: screenTripStops(t); break;
                case 2: routeMenu(t.route_id); break;
                case 3: screenServiceInfo(t.service_id); break;
                case 4: screenTripFrequencies(trip_id); break;
                case 5: screenShape(t.shape_id); break;
                case 6: screenTripsSharingBlock(t.block_id); break;
            }
        });
    }
}

static void menuTrips() {
    while (true) {
        int c = runMenu("trips", {"look up trip by id", "trips sharing a block id"});
        if (c < 0) return;
        guarded("trips", [&] {
            if (c == 0) { if (auto id = inputLine("trip lookup", "trip id")) tripMenu(*id); }
            else if (auto id = inputLine("block lookup", "block id")) screenTripsSharingBlock(*id);
        });
    }
}

// ---- routes ----

static void screenRouteTrips(const gtfs::route& r) {
    header("trips on route " + routeLabel(r.route_id));
    busy("reading trips.txt…");
    auto trips = gtfs::getAllTrips(feed(), r.route_id);
    vector<string> rows;
    for (const auto& t : trips) rows.push_back(t.trip_id);
    std::sort(rows.begin(), rows.end());
    ListOpts o;
    o.title = "trips on route " + routeLabel(r.route_id);
    o.subtitle = r.route_long_name + "  —  " + std::to_string(rows.size()) + " trips (all service days)";
    o.hint = "↑/↓ move   enter open trip   esc back";
    while (true) {
        int i = pickList(rows, o);
        if (i < 0) return;
        o.initial = i;
        guarded("trip", [&] { tripMenu(rows[i]); });
    }
}

static void routeMenu(const string& route_id) {
    header("route " + route_id);
    busy("loading…");
    const gtfs::route r = routeOf(route_id);
    if (r.route_id.empty()) { notFound("route", route_id); return; }
    while (true) {
        int c = runMenu("route " + routeLabel(route_id), {"details", "trips on this route"}, r.route_long_name);
        if (c < 0) return;
        guarded("route", [&] {
            if (c == 0)
                kvScreen("route " + routeLabel(route_id), r.route_long_name, {
                    {"route id", r.route_id}, {"short name", r.route_short_name}, {"long name", r.route_long_name}, {"type", routeTypeStr(r.route_type)},
                    {"agency", r.agency_id}, {"description", r.route_desc}, {"color", r.route_color.empty() ? "" : "#" + r.route_color},
                    {"text color", r.route_text_color.empty() ? "" : "#" + r.route_text_color}, {"url", r.route_url}});
            else screenRouteTrips(r);
        });
    }
}

static void menuRoutes() {
    while (true) {
        int c = runMenu("routes", {"search routes (name or id)", "look up route by id"});
        if (c < 0) return;
        guarded("routes", [&] {
            if (c == 0) {
                searchLoop("search routes", "route short name, long name or id", [](const string& q) {
                    vector<pair<string, string>> out;
                    for (const auto& m : gtfs::searchRoute(feed(), q)) {
                        if (m.score < 25) continue;
                        out.push_back({m.route_id, routeRow(m.route_id, m.route_short_name, m.route_long_name)});
                        if (out.size() >= 200) break;
                    }
                    return out;
                }, [](const string& id) { guarded("route", [&] { routeMenu(id); }); });
            } else if (auto id = inputLine("route lookup", "route id")) routeMenu(*id);
        });
    }
}

// ---- service ----

static void menuService() {
    while (true) {
        int c = runMenu("service & calendars", {"look up service id", "does a trip run on a date?", "feed validity on a date"},
                        "", {false, false, !g.caps.feedInfo});
        if (c < 0) return;
        guarded("service", [&] {
            if (c == 0) { if (auto id = inputLine("service lookup", "service id")) screenServiceInfo(*id); }
            else if (c == 1) {
                auto id = inputLine("trip validity", "trip id");
                if (!id) return;
                header("trip validity");
                busy("looking up trip…");
                const gtfs::trip t = gtfs::getTripInfo(feed(), *id);
                if (t.trip_id.empty()) { notFound("trip", *id); return; }
                auto d = inputDate("trip validity", "trip " + *id + "  (service " + t.service_id + ")");
                if (!d) return;
                header("trip validity");
                busy("checking calendar files…");
                const bool ok = gtfs::isTripValid(feed(), *id, *d);
                header("trip validity");
                std::cout << "  trip " << *id << " (service " << t.service_id << ") on " << weekdayName(*d) << " " << fmtDate(*d) << ":  "
                          << (ok ? col::GREEN : col::RED) << col::BOLD << (ok ? "RUNS" : "DOES NOT RUN") << col::RESET << "\n";
                pauseKey();
            } else {
                auto d = inputDate("feed validity");
                if (!d) return;
                message("feed validity", fmtDate(*d) + ":  " + feedStatusStr(gtfs::verifyGTFS(feed(), *d)), col::RESET);
            }
        });
    }
}

// ---- agency, fares, distance, feed ----

static void screenAgencies() {
    auto ags = gtfs::getAgencyInfo(feed());
    header("agencies", std::to_string(ags.size()) + (ags.size() == 1 ? " agency" : " agencies"));
    for (const auto& a : ags) {
        std::cout << "  " << col::BOLD << a.agency_name << col::RESET << "\n";
        for (const auto& [k, v] : vector<pair<string, string>>{{"  id", a.agency_id}, {"  url", a.agency_url}, {"  timezone", a.agency_timezone},
                                                              {"  language", a.agency_lang}, {"  phone", a.agency_phone}, {"  email", a.agency_email},
                                                              {"  fare url", a.agency_fare_url}})
            if (!v.empty()) std::cout << kvLine(k, v) << "\n";
        std::cout << "\n";
    }
    pauseKey();
}

static void menuAgency() {
    while (true) {
        int c = runMenu("agency & fares", {"agencies", "look up fare by fare_id"}, "", {!g.caps.agency, !g.caps.fares});
        if (c < 0) return;
        guarded("agency", [&] {
            if (c == 0) screenAgencies();
            else if (auto id = inputLine("fare lookup", "fare id")) {
                const auto f = gtfs::getFareInfo(feed(), *id);
                if (f.fare_id.empty()) { notFound("fare", *id); return; }
                char price[32];
                std::snprintf(price, sizeof(price), "%.2f %s", f.price, f.currency_type.c_str());
                kvScreen("fare " + *id, "", {{"fare id", f.fare_id}, {"price", price},
                    {"paid", f.payment_method == gtfs::fare::paid_on_board ? "on board" : f.payment_method == gtfs::fare::paid_before_boarding ? "before boarding" : ""},
                    {"transfers", f.transfers == gtfs::fare::unlimited ? "unlimited" : f.transfers == gtfs::fare::transfer_undef ? "" : std::to_string(static_cast<int>(f.transfers))},
                    {"transfer time", f.transfer_duration ? std::to_string(f.transfer_duration / 60) + " min" : ""}, {"agency", f.agency_id}});
            }
        });
    }
}

static void screenDistance() {
    auto lat1 = inputDouble("distance", "point A latitude"); if (!lat1) return;
    auto lon1 = inputDouble("distance", "point A longitude"); if (!lon1) return;
    auto lat2 = inputDouble("distance", "point B latitude"); if (!lat2) return;
    auto lon2 = inputDouble("distance", "point B longitude"); if (!lon2) return;
    header("distance");
    MapData m;
    m.marks = {{*lat1, *lon1}, {*lat2, *lon2}};
    auto [R, C] = term::size();
    drawMap(m, std::clamp(C - 6, 20, 72), std::clamp(R - 10, 6, 16));
    std::cout << "  " << col::BOLD << std::fixed << std::setprecision(3) << gtfs::getDistanceKM(*lat1, *lon1, *lat2, *lon2) << " km" << col::RESET
              << col::GRAY << "  (great-circle)" << col::RESET << "\n";
    pauseKey();
}

static vector<pair<string, string>> discoverFeeds(const string& exeDir) {
    vector<pair<string, string>> found; // name, path
    std::set<string> seen;
    std::error_code ec;
    vector<fs::path> bases = {fs::current_path(ec)};
    if (!exeDir.empty()) { bases.push_back(exeDir); bases.push_back(fs::path(exeDir) / ".."); }
    for (const auto& base : bases)
        for (const char* sub : {"data"}) {
            fs::path dir = base / sub;
            if (!fs::is_directory(dir, ec)) continue;
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                if (!e.is_directory(ec) || !fs::exists(e.path() / "stops.txt", ec)) continue;
                string canon = fs::weakly_canonical(e.path(), ec).string();
                if (seen.insert(canon).second) found.push_back({e.path().filename().string(), canon});
            }
        }
    std::sort(found.begin(), found.end());
    return found;
}

static string g_exeDir;

static optional<string> feedPicker() {
    auto found = discoverFeeds(g_exeDir);
    vector<string> rows;
    for (const auto& [name, path] : found) rows.push_back(string(col::BOLD) + padRight(name, 16) + col::RESET + " " + col::DIM + path + col::RESET);
    rows.push_back("type a path…");
    ListOpts o;
    o.title = "choose a GTFS feed";
    o.subtitle = found.empty() ? "no feeds found in ./data" : std::to_string(found.size()) + " feeds found";
    while (true) {
        int r = pickList(rows, o);
        if (r < 0) return std::nullopt;
        if (r < static_cast<int>(found.size())) return found[r].second;
        if (auto p = inputLine("feed path", "path to a folder of GTFS .txt files")) return *p;
    }
}

static void screenFeedInfo() {
    header("feed", feedName());
    std::cout << kvLine("path", fs::weakly_canonical(feed().path).string()) << "\n";
    if (g.caps.feedInfo) std::cout << kvLine("status today", feedStatusStr(gtfs::verifyGTFS(feed(), gtfs::getToday()))) << "\n";
    std::cout << "\n";
    const gtfs::data_feed& d = feed();
    std::error_code ec;
    for (const auto& [name, p] : vector<pair<string, string>>{
             {"stops.txt", d.stopPath}, {"routes.txt", d.routePath}, {"trips.txt", d.tripsPath}, {"stop_times.txt", d.stopTimesPath},
             {"calendar.txt", d.calendarPath}, {"calendar_dates.txt", d.calendarDatesPath}, {"agency.txt", d.agencyPath}, {"shapes.txt", d.shapePath},
             {"feed_info.txt", d.feedInfoFile}, {"frequencies.txt", d.frequencyPath}, {"fare_attributes.txt", d.fareAttributesPath}}) {
        const bool ok = fileExists(p);
        char size[24] = "";
        if (ok) std::snprintf(size, sizeof(size), "%.1f MB", static_cast<double>(fs::file_size(p, ec)) / 1048576.0);
        std::cout << "  " << (ok ? col::GREEN : col::GRAY) << (ok ? "✓ " : "· ") << col::RESET << padRight(name, 22) << col::GRAY << size << col::RESET << "\n";
    }
    pauseKey();
}

static void menuFeed() {
    while (true) {
        int c = runMenu("feed", {"feed details", "switch feed"}, feedName());
        if (c < 0) return;
        guarded("feed", [&] {
            if (c == 0) screenFeedInfo();
            else if (auto p = feedPicker()) {
                string err = loadFeed(*p);
                message("feed", err.empty() ? "loaded " + feedName() : err, err.empty() ? col::GREEN : col::RED);
            }
        });
    }
}

static void mainMenu() {
    while (true) {
        int c = runMenu("gtfs explorer",
                        {"stops", "routes", "trips", "service & calendars", "agency & fares", "distance calculator", "feed: " + feedName(), "quit"},
                        fs::weakly_canonical(feed().path).string(),
                        {false, false, false, !(g.caps.calendar || g.caps.calendarDates || g.caps.feedInfo), !(g.caps.agency || g.caps.fares), false, false, false},
                        "↑/↓ move   enter select   1-9 jump   esc/q quit");
        switch (c) {
            case -1: case 7: return;
            case 0: menuStops(); break;
            case 1: menuRoutes(); break;
            case 2: menuTrips(); break;
            case 3: menuService(); break;
            case 4: menuAgency(); break;
            case 5: guarded("distance", screenDistance); break;
            case 6: menuFeed(); break;
        }
    }
}

// ============================================================================
// entry
// ============================================================================

static void printHelp(const char* prog) {
    std::cerr << "Usage: " << prog << " [feed_dir]\n\n"
              << "Interactive, keyboard-only explorer for a GTFS Schedule feed (a folder of .txt files).\n"
              << "With no argument it opens the feed from config/config.json, or lists feeds found in ./data if that is unusable.\n\n"
              << "Keys: ↑/↓ or j/k move · enter select · esc or q back · ←/→ page (or change day in departures)\n"
              << "      1-9 jump in menus · m toggle map in stop lists · d pick a date / t today in departures\n";
}

int main(int argc, char* argv[]) {
    string feedArg;
    for (int i = 1; i < argc; ++i) {
        const string a = argv[i];
        if (a == "-h" || a == "--help") { printHelp(argv[0]); return 0; }
        if (!a.empty() && a[0] == '-') { std::cerr << "unknown option: " << a << "\n"; printHelp(argv[0]); return 1; }
        if (!feedArg.empty()) { std::cerr << "only one feed path may be given\n"; return 1; }
        feedArg = a;
    }
    if (!feedArg.empty()) {
        string err = loadFeed(feedArg);
        if (!err.empty()) { std::cerr << err << "\n"; return 1; }
    } else {
        try { loadFeed(config::loadDataPath()); } catch (const std::exception&) {} // no usable config: fall back to the feed picker
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cerr << "gtfs_cli is interactive and needs a real terminal (stdin and stdout must be a tty).\n";
        return 1;
    }
    char resolved[PATH_MAX];
    if (realpath(argv[0], resolved)) g_exeDir = fs::path(resolved).parent_path().string();

    std::ios::sync_with_stdio(false);
    static char outBuf[1 << 16];
    std::cout.rdbuf()->pubsetbuf(outBuf, sizeof(outBuf));

    string fatal;
    {
        term::Screen screen;
        if (!screen.ok) { std::cerr << "failed to enter raw terminal mode\n"; return 1; }
        try {
            while (!g.feed) {
                auto p = feedPicker();
                if (!p) return 0;
                string err = loadFeed(*p);
                if (!err.empty()) message("feed", err, col::RED);
            }
            mainMenu();
        } catch (const std::exception& e) { fatal = e.what(); }
    }
    if (!fatal.empty()) { std::cerr << "fatal error: " << fatal << "\n"; return 1; }
    return 0;
}
