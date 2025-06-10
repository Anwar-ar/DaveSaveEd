// Logger.cpp
//
// Copyright (c) 2025 FNGarvin (184324400+FNGarvin@users.noreply.github.com)
// All rights reserved.
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// Disclaimer: This project and its creators are not affiliated with Mintrocket, Nexon,
// or any other entities associated with the game "Dave the Diver." This is an independent
// fan-made tool.
//
// This project uses third-party libraries under their respective licenses:
// - zlib (Zlib License)
// - nlohmann/json (MIT License)
// - SQLite (Public Domain)
// Full license texts can be found in the /dist/zlib, /dist/nlohmann_json, and /dist/sqlite3 directories.
//
#include "Logger.h"
#include <iostream>     // For std::cout, std::cerr
#include <filesystem>   // For std::filesystem::path, exists, create_directories
#include <chrono>       // For std::chrono::system_clock
#include <iomanip>      // For std::put_time
#include <sstream>      // For std::stringstream

// Static member definitions for the Logger class.
std::ofstream Logger::s_logFile;
bool Logger::s_isFileLoggingEnabled = false;
std::string Logger::s_logFilePath;
std::string Logger::s_binDirectory;

// Initializes the Logger by setting up the log file path and opening the file if logging is enabled.
void Logger::Initialize(const std::string& appName, bool enableFileLogging, const std::string& binDir) {
    s_binDirectory = binDir;
    s_isFileLoggingEnabled = enableFileLogging;
    if (s_isFileLoggingEnabled) {
        std::filesystem::path logDirPath = s_binDirectory;
        // Ensure the log directory exists.
        if (!std::filesystem::exists(logDirPath)) {
            std::filesystem::create_directories(logDirPath);
        }

        // Generate a timestamp for the log file name.
        auto now = std::chrono::system_clock::now();
        std::time_t time_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_now), "%Y%m%d_%H%M%S");
        std::string timestamp = ss.str();

        // Construct the full log file path.
        s_logFilePath = logDirPath.string() + "/" + appName + "_log_" + timestamp + ".txt";
        s_logFile.open(s_logFilePath, std::ios::app);
        if (!s_logFile.is_open()) {
            std::cerr << "[ERROR] Failed to open log file: " << s_logFilePath << std::endl;
            s_isFileLoggingEnabled = false; // Disable file logging if opening fails.
        } else {
            std::cout << "[INFO] File logging enabled. Log will be written to: " << s_logFilePath << std::endl;
        }
    }
}

// Logs a message to the console and the log file (if enabled).
// Messages are prefixed with their log level and optionally include an SQLite error code.
void Logger::Log(LogLevel level, const char* message, int sqlite_err_code) {
    std::string prefix;
    std::ostream* os_console; // Pointer to either std::cout or std::cerr.

    if (level == LOG_INFO_LEVEL) {
        prefix = "[INFO] ";
        os_console = &std::cout;
    } else { // LOG_ERROR_LEVEL
        prefix = "[ERROR] ";
        os_console = &std::cerr;
    }

    std::string full_message = prefix + message;
    // Appends SQLite error code if provided.
    if (sqlite_err_code != -1) {
        full_message += " (Error Code: " + std::to_string(sqlite_err_code) + ")";
    }

    // Output to console.
    *os_console << full_message << std::endl;
    // Output to file if enabled and open.
    if (s_isFileLoggingEnabled && s_logFile.is_open()) {
        s_logFile << full_message << std::endl;
    }
}

// Closes the log file, gracefully shutting down the logging system.
void Logger::Shutdown() {
    if (s_logFile.is_open()) {
        s_logFile.close();
        std::cout << "[INFO] Log file closed." << std::endl;
    }
}
