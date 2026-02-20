#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace netgate {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    explicit Logger(const std::string& component, LogLevel min_level = LogLevel::INFO)
        : component_(component), min_level_(min_level) {}

    void debug(const std::string& msg) const { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg)  const { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg)  const { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) const { log(LogLevel::ERROR, msg); }

    void set_level(LogLevel level) { min_level_ = level; }

    template <typename... Args>
    void info_fmt(const char* fmt, Args... args) const {
        char buf[512];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        log(LogLevel::INFO, buf);
    }

    template <typename... Args>
    void warn_fmt(const char* fmt, Args... args) const {
        char buf[512];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        log(LogLevel::WARN, buf);
    }

    template <typename... Args>
    void error_fmt(const char* fmt, Args... args) const {
        char buf[512];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        log(LogLevel::ERROR, buf);
    }

private:
    std::string component_;
    LogLevel min_level_;

    void log(LogLevel level, const std::string& msg) const {
        if (level < min_level_) return;

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::ostringstream out;
        out << std::put_time(std::localtime(&time_t), "%H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << level_str(level) << "] "
            << "[" << component_ << "] "
            << msg << "\n";

        if (level >= LogLevel::ERROR) {
            std::cerr << out.str();
        } else {
            std::cout << out.str();
        }
    }

    static const char* level_str(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DBG";
            case LogLevel::INFO:  return "INF";
            case LogLevel::WARN:  return "WRN";
            case LogLevel::ERROR: return "ERR";
        }
        return "???";
    }
};

} // namespace netgate
