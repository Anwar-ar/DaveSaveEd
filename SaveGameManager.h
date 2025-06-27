// SaveGameManager.h
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
#include <vector>
#include <filesystem>
#include "json.hpp"         // Include nlohmann/json for the JSON data
#include "zlib.h"           // For zlib compression/decompression
#include "sqlite3.h"        // For SQLite database operations

class SaveGameManager {
public:
    SaveGameManager();
    ~SaveGameManager();

    // Core Save File Operations
    bool LoadSaveFile(const std::string& filepath);
    bool WriteSaveFile(std::string& out_backup_filepath);

    // Player Stats Getters (already exists, but ensures it can access m_saveData)
    long long GetGold() const;
    long long GetBei() const;
    long long GetArtisansFlame() const;
    long long GetFollowerCount() const;
    bool IsSaveFileLoaded() const { return m_isSaveFileLoaded; }

    // Player Stats Setters
    void SetGold(long long value);
    void SetBei(long long value);
    void SetArtisansFlame(long long value);
    void SetFollowerCount(long long value);

    // Ingredient Modification Functions (these are new or will be expanded)
    void MaxOwnIngredients(sqlite3* db); // Needs access to the database
    void MaxAllIngredients(sqlite3* db); // Needs access to the database
    void MaxOwnMaterials(sqlite3* db); // Needs access to the database
    void MaxOwnStaffLevel(); // Needs access to the database

    // Static helper to find save directory (already exists)
    static std::filesystem::path GetDefaultSaveGameDirectoryAndLatestFile(std::string& latestSaveFileName);

private:
    // --- Member Variables ---
    nlohmann::json m_saveData;           // Holds the parsed JSON data of the save file.
    std::string m_currentSaveFilePath;   // Path of the currently loaded save file.
    bool m_isSaveFileLoaded;             // Flag to indicate if a save file is successfully loaded.

    // --- Private Helper Methods ---
    // XOR encryption/decryption (already exists, but ensure it's private if it's not a static utility)
    std::string XORDecryptEncrypt(const std::string& data, const std::string& key);

    // Zlib decompression (will be moved from DaveSaveEd.cpp and integrated with XOR)
    std::string decompressZlib(const std::vector<unsigned char>& compressed_bytes);
    // Zlib compression (will be moved from DaveSaveEd.cpp and integrated with XOR)
    std::vector<unsigned char> compressZlib(const std::string& uncompressed_json);

    // SQLite Callback for batch querying ingredients (for MaxAllIngredients)
    // This will need to be a static member function or a friend function
    // due to how sqlite3_exec callbacks work, or a lambda in C++11+
    // For now, let's keep it as a free function or make it static in SaveGameManager.cpp
    // and just declare it here.
    // For simplicity, let's keep it in SaveGameManager.cpp's private section for now.
    // static int callbackGetAllIngredients(void* data, int argc, char** argv, char** azColName);

    // Constant for the XOR key (replace with your actual key)
    const std::string XOR_KEY = "GameData"; // <-- **IMPORTANT: Replace with your actual key!**
};
