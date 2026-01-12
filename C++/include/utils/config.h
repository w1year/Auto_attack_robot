#pragma once

#include <string>
#include <unordered_map>
#include <fstream>

namespace rm_auto_attack {

class Config {
public:
    static Config& getInstance();
    
    bool loadFromFile(const std::string& filename);
    void setValue(const std::string& key, const std::string& value);
    std::string getValue(const std::string& key, const std::string& defaultValue = "") const;
    
    int getInt(const std::string& key, int defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::unordered_map<std::string, std::string> m_config;
    
    std::string trim(const std::string& str) const;
};

} // namespace rm_auto_attack
