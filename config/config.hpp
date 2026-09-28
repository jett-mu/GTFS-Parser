#ifndef GTFS_PARSER_CONFIG_HPP
#define GTFS_PARSER_CONFIG_HPP

#include "../static-gtfs/gtfs.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace json_read {
    // functions to read from config.json
    // Minimal reader for a flat JSON object: looks up top-level keys only, nested values are skipped.

    inline string readFile(const string& path) {
        ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open config file: " + path);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    inline void skipWhitespace(const string& s, size_t& i) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    }

    // i must point at the opening quote; on success it is left just past the closing quote
    inline bool parseString(const string& s, size_t& i, string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        out.clear();
        for (i++; i < s.size(); i++) {
            const char c = s[i];
            if (c == '"') { i++; return true; }
            if (c != '\\') { out += c; continue; }
            if (++i >= s.size()) return false;
            switch (s[i]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 >= s.size()) return false;
                    const unsigned long cp = std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += s[i]; break; // \" \\ \/
            }
        }
        return false; // unterminated string
    }

    // Sets pos to the first character of the value belonging to the top-level `key`.
    inline bool findValue(const string& json, const string& key, size_t& pos) {
        int depth = 0;
        size_t i = 0;
        while (i < json.size()) {
            const char c = json[i];
            if (c == '{' || c == '[') { depth++; i++; }
            else if (c == '}' || c == ']') { depth--; i++; }
            else if (c == '"') {
                string text;
                if (!parseString(json, i, text)) return false;
                if (depth != 1) continue;
                size_t j = i;
                skipWhitespace(json, j);
                if (j < json.size() && json[j] == ':') { // a string followed by ':' is a key
                    if (text == key) {
                        j++;
                        skipWhitespace(json, j);
                        pos = j;
                        return true;
                    }
                    i = j + 1;
                }
            }
            else i++;
        }
        return false;
    }

    inline bool getString(const string& json, const string& key, string& out) {
        size_t pos = 0;
        return findValue(json, key, pos) && parseString(json, pos, out);
    }

    inline bool getInt(const string& json, const string& key, int& out) {
        size_t pos = 0;
        if (!findValue(json, key, pos)) return false;
        const char* begin = json.c_str() + pos;
        char* end = nullptr;
        const long v = std::strtol(begin, &end, 10);
        if (end == begin) return false;
        out = static_cast<int>(v);
        return true;
    }

    // throwing variants for required keys
    inline string requireString(const string& json, const string& key) {
        string out;
        if (!getString(json, key, out)) throw std::runtime_error("config: missing or invalid string \"" + key + "\"");
        return out;
    }

    inline int requireInt(const string& json, const string& key) {
        int out = 0;
        if (!getInt(json, key, out)) throw std::runtime_error("config: missing or invalid integer \"" + key + "\"");
        return out;
    }
}

namespace config {
    // functions to load config and return a gtfs::data_feed objct calling ::json_read

    constexpr int supported_version = 3;

    // Locates config.json: $GTFS_CONFIG if set, otherwise config/config.json (or config.json) in the
    // current directory or the nearest parent that has one, so tools work from any repo subfolder.
    inline string findConfigFile() {
        namespace fs = std::filesystem;
        if (const char* env = std::getenv("GTFS_CONFIG"); env && *env) return env;

        std::error_code ec;
        for (fs::path dir = fs::current_path(ec); !dir.empty(); dir = dir.parent_path()) {
            for (const fs::path& candidate : {dir / "config" / "config.json", dir / "config.json"})
                if (fs::is_regular_file(candidate, ec)) return candidate.string();
            if (dir == dir.parent_path()) break;
        }
        throw std::runtime_error("config: config.json not found in the current directory or its parents (set GTFS_CONFIG to point at it)");
    }

    // Path to the GTFS data folder named by the config; throws std::runtime_error if the config is unusable.
    // An empty configFile means "find it" (see findConfigFile).
    inline string loadDataPath(const string& configFile = "") {
        const string file = configFile.empty() ? findConfigFile() : configFile;
        const string json = json_read::readFile(file);

        const int version = json_read::requireInt(json, "config_version");
        if (version != supported_version)
            throw std::runtime_error("config: unsupported config_version " + to_string(version) +
                                     " (expected " + to_string(supported_version) + ") in " + file);

        const string dataPath = json_read::requireString(json, "absolute_path_to_data");
        if (dataPath.empty()) throw std::runtime_error("config: \"absolute_path_to_data\" is empty in " + file);
        if (!std::filesystem::is_directory(dataPath))
            throw std::runtime_error("config: data folder does not exist: " + dataPath + " (from " + file + ")");
        return dataPath;
    }

    // Loads the config and builds the data_feed every gtfs:: query takes.
    inline gtfs::data_feed load(const string& configFile = "") {
        return gtfs::data_feed(loadDataPath(configFile));
    }

    // For CLI tools: same as load(), but reports the problem on stderr and exits non-zero (which server.py treats as an error).
    inline gtfs::data_feed loadOrExit(const string& configFile = "") {
        try {
            return load(configFile);
        } catch (const std::exception& e) {
            cerr << e.what() << endl;
            std::exit(1);
        }
    }
}
#endif //GTFS_PARSER_CONFIG_HPP
