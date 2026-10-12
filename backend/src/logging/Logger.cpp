#include "logging/Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace paylock {

std::mutex Logger::logMutex_;
LogLevel Logger::currentLevel_ = LogLevel::INFO;

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(logMutex_);
    currentLevel_ = level;
}

LogLevel Logger::getLogLevel() {
    std::lock_guard<std::mutex> lock(logMutex_);
    return currentLevel_;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    return ss.str();
}

void Logger::debug(const std::string& message, const std::string& component) {
    log(LogLevel::DEBUG, message, component);
}

void Logger::info(const std::string& message, const std::string& component) {
    log(LogLevel::INFO, message, component);
}

void Logger::warn(const std::string& message, const std::string& component) {
    log(LogLevel::WARNING, message, component);
}

void Logger::error(const std::string& message, const std::string& component) {
    log(LogLevel::ERROR, message, component);
}

void Logger::log(LogLevel level, const std::string& message, const std::string& component) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (static_cast<int>(level) < static_cast<int>(currentLevel_)) {
        return;
    }

    std::ostream& out = (level == LogLevel::ERROR) ? std::cerr : std::cout;

    out << "[" << levelToString(level) << "]";
    if (!component.empty()) {
        out << " [" << component << "]";
    }
    out << " " << message << "\n";
}

} // namespace paylock
