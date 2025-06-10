// Logger.h
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
#pragma once

#include <string>
#include <fstream>
#include "DaveSaveEd.h" // For LogLevel enum and BIN_DIRECTORY

// The Logger class provides static methods for application-wide logging.
// It supports both console output and file logging to a timestamped file within the bin directory.
class Logger {
public:
    // Initializes the logging system, setting up file logging if enabled.
    // Parameters:
    //   appName: The name of the application, used in the log file name.
    //   enableFileLogging: Boolean to enable/disable logging to a file.
    //   binDir: The directory where log files should be created.
    static void Initialize(const std::string& appName, bool enableFileLogging, const std::string& binDir);

    // Logs a message to the console and optionally to a file.
    // Parameters:
    //   level: The severity level of the log message (e.g., LOG_INFO_LEVEL, LOG_ERROR_LEVEL).
    //   message: The string message to be logged.
    //   sqlite_err_code: Optional SQLite error code to include in the log message.
    static void Log(LogLevel level, const char* message, int sqlite_err_code = -1);

    // Shuts down the logging system, ensuring the log file is properly closed.
    static void Shutdown();

private:
    // Static members to hold logging state.
    static std::ofstream s_logFile;
    static bool s_isFileLoggingEnabled;
    static std::string s_logFilePath;
    static std::string s_binDirectory;
};

// Global function alias for convenience to call the static Logger::Log method.
// This allows continued use of LogMessage(level, msg) throughout the application.
inline void LogMessage(LogLevel level, const char* message, int sqlite_err_code) {
    Logger::Log(level, message, sqlite_err_code);
}
