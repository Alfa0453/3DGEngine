#include "engine/core/Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace engine {
namespace {

std::string Trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    const size_t b = s.find_first_not_of(ws);
    return s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // anonymous namespace

bool Config::Load(const std::string& path) {
    m_path = path;
    std::ifstream file(path);
    if (!file) return false;

    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');         // strip trailing comment
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;      // not a key=value line
        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));
        if (!key.empty()) m_values[key] = val;
    }
    return true;
}

bool Config::Save(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return false;
    file << "# Pong configuration (auto-generated; edit freely)\n";
    for (const auto& kv : m_values)
        file << kv.first << " = " << kv.second << "\n";
    return true;
}

bool Config::Save() const { return Save(m_path); }

int Config::GetInt(const std::string& key, int def) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

float Config::GetFloat(const std::string& key, float def) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}

bool Config::GetBool(const std::string& key, bool def) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return def;
    const std::string v = Lower(it->second);
    if (v == "true"  || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no"  || v == "off") return false;
    return def;
}

std::string Config::GetString(const std::string& key, const std::string& def) const {
    auto it = m_values.find(key);
    return (it == m_values.end()) ? def : it->second;
}

void Config::Set(const std::string& key, int value)         { m_values[key] = std::to_string(value); }
void Config::Set(const std::string& key, float value)       { m_values[key] = std::to_string(value); }
void Config::Set(const std::string& key, bool value)        { m_values[key] = value ? "true" : "false"; }
void Config::Set(const std::string& key, const std::string& value) { m_values[key] = value; }

} // namespace engine