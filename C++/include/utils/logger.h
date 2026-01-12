#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace rm_auto_attack {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& getInstance();
    
    void setLogLevel(LogLevel level);
    void setLogFile(const std::string& filename);
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    
private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void log(LogLevel level, const std::string& message);
    std::string levelToString(LogLevel level);
    std::string getCurrentTime();
    
    LogLevel m_logLevel;
    std::mutex m_mutex;
    std::ofstream m_logFile;
    bool m_logToFile;
};

// 便捷宏
#define LOG_DEBUG(msg) rm_auto_attack::Logger::getInstance().debug(msg)
#define LOG_INFO(msg) rm_auto_attack::Logger::getInstance().info(msg)
#define LOG_WARNING(msg) rm_auto_attack::Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) rm_auto_attack::Logger::getInstance().error(msg)

} // namespace rm_auto_attack
