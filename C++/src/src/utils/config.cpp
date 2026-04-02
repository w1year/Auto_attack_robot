#include "utils/config.h"
#include "utils/logger.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace rm_auto_attack {

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("无法打开配置文件: " + filename);
        return false;
    }
    
    std::string line;
    int lineNum = 0;
    
    while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // 解析键值对
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            m_config[key] = value;
        } else {
            LOG_WARNING("配置文件第 " + std::to_string(lineNum) + " 行格式错误: " + line);
        }
    }
    
    file.close();
    LOG_INFO("成功加载配置文件: " + filename + " (共 " + std::to_string(m_config.size()) + " 项)");
    return true;
}

void Config::setValue(const std::string& key, const std::string& value) {
    m_config[key] = value;
}

std::string Config::getValue(const std::string& key, const std::string& defaultValue) const {
    auto it = m_config.find(key);
    if (it != m_config.end()) {
        return it->second;
    }
    return defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    auto it = m_config.find(key);
    if (it != m_config.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception& e) {
            LOG_WARNING("配置项 " + key + " 无法转换为整数: " + it->second);
            return defaultValue;
        }
    }
    return defaultValue;
}

float Config::getFloat(const std::string& key, float defaultValue) const {
    auto it = m_config.find(key);
    if (it != m_config.end()) {
        try {
            return std::stof(it->second);
        } catch (const std::exception& e) {
            LOG_WARNING("配置项 " + key + " 无法转换为浮点数: " + it->second);
            return defaultValue;
        }
    }
    return defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = m_config.find(key);
    if (it != m_config.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return (value == "true" || value == "1" || value == "yes" || value == "on");
    }
    return defaultValue;
}

std::string Config::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

} // namespace rm_auto_attack
