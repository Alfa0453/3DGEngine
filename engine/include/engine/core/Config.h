#pragma once

#include <map>
#include <string>

namespace engine {

// A tiny key=value settings file.
//
//   # comments and blank lines are ignored
//   window.width  = 1280
//   window.vsync  = true
//
// Every getter takes a default, so a missing file or a missing key is never an
// error — you simply get the default. Settings can be changed at runtime and
// written back with Save(), which is how the game persists choices like
// fullscreen and volume between runs.
class Config {
public:
    Config() = default;
    explicit Config(const std::string& path) { Load(path); }

    // Merge settings from a file. Returns false if the file was absent (not an
    // error: you keep whatever defaults the getters supply). Remembers the path.
    bool Load(const std::string& path);

    // Write all current settings back. Save() uses the path from Load().
    bool Save() const;
    bool Save(const std::string& path) const;

    int GetInt(const std::string& key, int def) const;
    float GetFloat(const std::string& key, float def) const;
    bool GetBool(const std::string& key, bool def) const;
    std::string GetString(const std::string& key, const std::string& def) const;

    void Set(const std::string& key, int value);
    void Set(const std::string& key, float value);
    void Set(const std::string& key, bool value);
    void Set(const std::string& key, const std::string& value);
    void Set(const std::string& key, const char* value) { Set(key, std::string(value)); }

    bool Has(const std::string&key) const { return m_values.count(key) != 0; }
    const std::string& Path() const { return m_path; }

private:
    std::map<std::string, std::string> m_values;    // sorted -> stable Save output
    std::string m_path;
};

} // namespace engine