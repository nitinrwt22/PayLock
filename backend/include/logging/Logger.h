#ifndef PAYLOCK_LOGGER_H
#define PAYLOCK_LOGGER_H

#include <string>
#include <mutex>
#include <iostream>

namespace paylock {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

/**
 * @brief Thread-safe logging subsystem for PayLock.
 * 
 * Provides synchronized console logging across all worker threads,
 * transaction engines, lock managers, and recovery modules.
 */
class Logger {
public:
    static void setLogLevel(LogLevel level);
    static LogLevel getLogLevel();

    static void debug(const std::string& message, const std::string& component = "");
    static void info(const std::string& message, const std::string& component = "");
    static void warn(const std::string& message, const std::string& component = "");
    static void error(const std::string& message, const std::string& component = "");

    static void log(LogLevel level, const std::string& message, const std::string& component = "");

private:
    static std::string levelToString(LogLevel level);
    static std::string getTimestamp();

    static std::mutex logMutex_;
    static LogLevel currentLevel_;
};

} // namespace paylock

#endif // PAYLOCK_LOGGER_H
