// SaveGameManager.cpp
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
#define NOMINMAX // Prevent Windows.h from defining min/max macros

#include "SaveGameManager.h"
#include <fstream>       // For file input/output streams
#include <shlobj.h>      // For SHGetKnownFolderPath (includes windows.h)
#include <chrono>        // For timestamping (std::chrono)
#include <iomanip>       // For std::put_time
#include <sstream>       // For std::stringstream
#include <algorithm>     // For std::all_of, and std::min/max
#include "sqlite3.h"     // Required for sqlite3* parameter in MaxAllIngredients
#include "Logger.h"      // For LogMessage
#include "json.hpp"      // Corrected include for nlohmann/json
#include <vector>        // Required for std::vector
#include <map>           // Required for std::map
#include <string>        // Required for std::string
#include <stdexcept>     // Required for std::runtime_error
#include <filesystem>    // Required for std::filesystem::path, create_directories, copy, last_write_time
#include <iostream>

// --- Global Constants for SaveGameManager ---
// Use long long for currency to avoid overflow
const long long SAVE_MAX_CURRENCY = 999999999LL;

// Constructor: Initializes the SaveGameManager instance.
SaveGameManager::SaveGameManager() : m_isSaveFileLoaded(false) {
    LogMessage(LOG_INFO_LEVEL, "SaveGameManager initialized.");
}

// Destructor: Cleans up resources used by the SaveGameManager.
SaveGameManager::~SaveGameManager() {
    LogMessage(LOG_INFO_LEVEL, "SaveGameManager shutting down.");
}

// Applies XOR encryption/decryption to a string using a specified key.
// The same function is used for both encryption and decryption.
std::string SaveGameManager::XORDecryptEncrypt(const std::string& data, const std::string& key) {
    std::string result = data;
    size_t key_len = key.length();
    for (size_t i = 0; i < data.length(); ++i) {
        result[i] = data[i] ^ key[i % key_len];
    }
    return result;
}

// --- Zlib Decompression Implementation ---
// This function is for decompressing the embedded SQLite database, not the save file itself.
std::string SaveGameManager::decompressZlib(const std::vector<unsigned char>& compressed_bytes) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(compressed_bytes.size());
    strm.next_in = const_cast<Bytef*>(compressed_bytes.data());

    if (inflateInit(&strm) != Z_OK) {
        throw std::runtime_error("zlib inflateInit failed.");
    }

    std::string decompressed_str;
    const size_t CHUNK = 16384;
    std::vector<unsigned char> buffer(CHUNK);

    int ret;
    do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer.data();
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret < 0 && ret != Z_BUF_ERROR) {
             inflateEnd(&strm);
             throw std::runtime_error("zlib inflate error: " + std::string(strm.msg ? strm.msg : "Unknown error"));
        }
        decompressed_str.append(reinterpret_cast<char*>(buffer.data()), CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    inflateEnd(&strm);
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib inflate did not reach end of stream correctly.");
    }
    return decompressed_str;
}

// --- Zlib Compression Implementation ---
// This function is for compressing the embedded SQLite database, not the save file itself.
std::vector<unsigned char> SaveGameManager::compressZlib(const std::string& uncompressed_data) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(uncompressed_data.size());
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(uncompressed_data.data()));

    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("zlib deflateInit failed.");
    }

    std::vector<unsigned char> compressed_bytes;
    const size_t CHUNK = 16384;
    std::vector<unsigned char> buffer(CHUNK);

    int ret;
    do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer.data();
        ret = deflate(&strm, Z_FINISH);
        compressed_bytes.insert(compressed_bytes.end(), buffer.begin(), buffer.begin() + (CHUNK - strm.avail_out));
    } while (ret == Z_OK);

    deflateEnd(&strm);
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib deflate did not finish stream correctly: " + std::string(strm.msg ? strm.msg : "Unknown error"));
    }
    return compressed_bytes;
}


// --- LoadSaveFile Implementation ---
bool SaveGameManager::LoadSaveFile(const std::string& filepath) {
    LogMessage(LOG_INFO_LEVEL, ("Attempting to load save file: " + filepath).c_str());
    m_isSaveFileLoaded = false;
    m_currentSaveFilePath = "";
    m_saveData = nlohmann::json(); // Clear any previously loaded data

    try {
        // 1. Read the raw XOR-encrypted bytes from the file
        std::ifstream input_file(filepath, std::ios::binary);
        if (!input_file) {
            LogMessage(LOG_ERROR_LEVEL, ("Could not open save file for reading: " + filepath).c_str());
            return false;
        }
        std::vector<unsigned char> xor_encrypted_bytes((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
        input_file.close();
        LogMessage(LOG_INFO_LEVEL, ("Read " + std::to_string(xor_encrypted_bytes.size()) + " bytes from file.").c_str());

        // 2. XOR decrypt the bytes to get the raw JSON string
        std::string json_str = XORDecryptEncrypt(std::string(xor_encrypted_bytes.begin(), xor_encrypted_bytes.end()), XOR_KEY);
        LogMessage(LOG_INFO_LEVEL, "XOR decrypted save file. Data is now raw JSON.");

        // 3. Parse the JSON string
        m_saveData = nlohmann::json::parse(json_str);
        m_currentSaveFilePath = filepath;
        m_isSaveFileLoaded = true;
        LogMessage(LOG_INFO_LEVEL, "Save file JSON parsed successfully.");
        return true;

    } catch (const nlohmann::json::parse_error& e) {
        LogMessage(LOG_ERROR_LEVEL, ("JSON parse error during load: " + std::string(e.what())).c_str());
    } catch (const std::runtime_error& e) {
        LogMessage(LOG_ERROR_LEVEL, ("Runtime error during load: " + std::string(e.what())).c_str());
    } catch (const std::exception& e) {
        LogMessage(LOG_ERROR_LEVEL, ("An unknown error occurred during load: " + std::string(e.what())).c_str());
    }
    return false;
}

// --- WriteSaveFile Implementation ---
// Modified to return the backup file path on success via an output parameter.
bool SaveGameManager::WriteSaveFile(std::string& out_backup_filepath) {
    // Clear the output path initially, in case of failure.
    out_backup_filepath.clear();

    if (!m_isSaveFileLoaded || m_currentSaveFilePath.empty()) {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to write save file, but no file is loaded or path is empty.");
        return false;
    }

    LogMessage(LOG_INFO_LEVEL, ("Attempting to write save file: " + m_currentSaveFilePath).c_str());
    try {
        std::filesystem::path original_path(m_currentSaveFilePath);
        std::filesystem::path backup_dir; // Declare backup_dir here

        // Get system temporary path using Windows API
        WCHAR tempPathBuffer[MAX_PATH];
        DWORD length = GetTempPathW(MAX_PATH, tempPathBuffer);
        if (length == 0 || length > MAX_PATH) {
            LogMessage(LOG_ERROR_LEVEL, "Failed to get system temporary path. Falling back to save directory backup.");
            backup_dir = original_path.parent_path() / "backups"; // Fallback to old behavior
        } else {
            std::filesystem::path base_temp_path = tempPathBuffer;
            backup_dir = base_temp_path / "DaveSaveEd_Backups"; // Create a specific subfolder for backups
        }
        std::filesystem::create_directories(backup_dir); // Ensure the chosen backup directory exists

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &now_c); // Use localtime_s for thread safety on Windows

        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        std::string timestamp = ss.str();

        std::string backup_filename = original_path.stem().string() + "_" + timestamp + original_path.extension().string();
        std::filesystem::path backup_path = backup_dir / backup_filename;

        // Copy original file to backup location
        std::filesystem::copy(original_path, backup_path, std::filesystem::copy_options::overwrite_existing);
        LogMessage(LOG_INFO_LEVEL, ("Original save file backed up to: " + backup_path.string()).c_str());

        // 2. Serialize the modified JSON data to a string
        std::string json_to_write_str = m_saveData.dump(); // No pretty printing for smaller size
        LogMessage(LOG_INFO_LEVEL, "Serialized JSON data.");

        // 3. XOR encrypt the JSON string
        std::string xor_encrypted_str = XORDecryptEncrypt(json_to_write_str, XOR_KEY);
        std::vector<unsigned char> final_bytes(xor_encrypted_str.begin(), xor_encrypted_str.end());
        LogMessage(LOG_INFO_LEVEL, "XOR encrypted JSON data.");

        // 4. Write the final bytes to the original save file path
        std::ofstream output_file(m_currentSaveFilePath, std::ios::binary | std::ios::trunc); // trunc to overwrite
        if (!output_file) {
            LogMessage(LOG_ERROR_LEVEL, ("Could not open save file for writing: " + m_currentSaveFilePath).c_str());
            return false;
        }
        output_file.write(reinterpret_cast<const char*>(final_bytes.data()), final_bytes.size());
        output_file.close();
        
        // On success, populate the output parameter with the backup file path
        out_backup_filepath = backup_path.string();
        LogMessage(LOG_INFO_LEVEL, ("Modified save file written successfully to: " + m_currentSaveFilePath).c_str());
        return true;

    } catch (const std::exception& e) {
        LogMessage(LOG_ERROR_LEVEL, ("Error writing save file: " + std::string(e.what())).c_str());
        // out_backup_filepath remains empty, as set at the beginning.
        return false;
    }
}

// --- Player Stats Getters ---
long long SaveGameManager::GetGold() const {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object() && m_saveData["PlayerInfo"].contains("m_Gold")) {
        return m_saveData["PlayerInfo"]["m_Gold"].get<long long>();
    }
    return 0;
}

long long SaveGameManager::GetBei() const {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object() && m_saveData["PlayerInfo"].contains("m_Bei")) {
        return m_saveData["PlayerInfo"]["m_Bei"].get<long long>();
    }
    return 0;
}

long long SaveGameManager::GetArtisansFlame() const {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object() && m_saveData["PlayerInfo"].contains("m_ChefFlame")) {
        return m_saveData["PlayerInfo"]["m_ChefFlame"].get<long long>();
    }
    return 0;
}

long long SaveGameManager::GetFollowerCount() const {
    if (m_isSaveFileLoaded && m_saveData.contains("SNSInfo") && m_saveData["SNSInfo"].is_object() && m_saveData["SNSInfo"].contains("m_Follow_Count")) {
        return m_saveData["SNSInfo"]["m_Follow_Count"].get<long long>();
    }
    return 0;
}


// --- Player Stats Setters ---
void SaveGameManager::SetGold(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object()) {
        m_saveData["PlayerInfo"]["m_Gold"] = std::min(value, SAVE_MAX_CURRENCY);
        LogMessage(LOG_INFO_LEVEL, ("Gold set to: " + std::to_string(m_saveData["PlayerInfo"]["m_Gold"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set gold, but PlayerInfo section not found or invalid.");
    }
}

void SaveGameManager::SetBei(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object()) {
        m_saveData["PlayerInfo"]["m_Bei"] = std::min(value, SAVE_MAX_CURRENCY);
        LogMessage(LOG_INFO_LEVEL, ("Bei set to: " + std::to_string(m_saveData["PlayerInfo"]["m_Bei"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set bei, but PlayerInfo section not found or invalid.");
    }
}

void SaveGameManager::SetArtisansFlame(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object()) {
        m_saveData["PlayerInfo"]["m_ChefFlame"] = std::min(value, SAVE_MAX_CURRENCY);
        LogMessage(LOG_INFO_LEVEL, ("Artisan's Flame set to: " + std::to_string(m_saveData["PlayerInfo"]["m_ChefFlame"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set artisan's flame, but PlayerInfo section not found or invalid.");
    }
}

void SaveGameManager::SetFollowerCount(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("SNSInfo") && m_saveData["SNSInfo"].is_object()) {
        m_saveData["SNSInfo"]["m_Follow_Count"] = value;
        LogMessage(LOG_INFO_LEVEL, ("Follower count set to: " + std::to_string(m_saveData["SNSInfo"]["m_Follow_Count"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set follower count, but SNSInfo section not found or invalid.");
    }
}


// Helper function to determine the target count based on the item's MaxCount from DB
static int GetDesiredMaxCountForTier(int item_db_max_count) {
    if (item_db_max_count == 1) {
        //  items with MaxCount of 1 to avoid potential quest progression issues.
        return 0; // Indicates this item should be skipped
    } else if (item_db_max_count == 99) {
        return 66;
    } else if (item_db_max_count == 999) {
        return 666;
    } else if (item_db_max_count >= 9999) {
        return 6666;
    } else { 
        // Default case: If MaxCount is not one of the explicit tiers (1, 99, 999, >=9999),
        // it's safest to log a warning and skip the item to prevent unexpected behavior.
        LogMessage(LOG_WARNING_LEVEL, ("Unhandled MaxCount tier encountered: " + std::to_string(item_db_max_count) + ". Skipping item.").c_str());
        return 0; // Skip this item as its MaxCount tier is not explicitly handled
    }
}

// Save JSON to file named save_dump.txt
void DumpSaveDataToFile(const nlohmann::json& m_saveData) {
    // Get the directory where the executable is running
    std::filesystem::path exePath = std::filesystem::current_path();
    std::filesystem::path outputPath = exePath / "save_dump.txt";

    try {
        // Open file in truncate mode (overwrite if it exists)
        std::ofstream outFile(outputPath, std::ios::out | std::ios::trunc);
        if (!outFile) {
            LogMessage(LOG_ERROR_LEVEL, "Failed to open save_dump.txt for writing.");
            return;
        }

        // Write formatted JSON
        outFile << m_saveData.dump(4);  // 4 = indent size
        outFile.close();

        LogMessage(LOG_INFO_LEVEL, "Successfully wrote save data to save_dump.txt");
    } catch (const std::exception& ex) {
        LogMessage(LOG_ERROR_LEVEL, ex.what());
    }
}

// Dump db
void DumpSQLiteToText(sqlite3* db, const std::string& outputFilePath) {
    std::ofstream out(outputFilePath);
    if (!out.is_open()) {
        std::cerr << "Could not open file: " << outputFilePath << std::endl;
        return;
    }

    sqlite3_stmt* stmt;
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table';";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string tableName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            out << "== Table: " << tableName << " ==\n";

            std::string dataQuery = "SELECT * FROM " + tableName + ";";
            sqlite3_stmt* dataStmt;

            if (sqlite3_prepare_v2(db, dataQuery.c_str(), -1, &dataStmt, nullptr) == SQLITE_OK) {
                int cols = sqlite3_column_count(dataStmt);

                // 🆕 Write column headers before any rows
                for (int i = 0; i < cols; ++i) {
                    const char* colName = sqlite3_column_name(dataStmt, i);
                    out << (colName ? colName : "NULL") << (i < cols - 1 ? " | " : "");
                }
                out << "\n";

                // Write each row of data
                while (sqlite3_step(dataStmt) == SQLITE_ROW) {
                    for (int i = 0; i < cols; ++i) {
                        const char* colText = reinterpret_cast<const char*>(sqlite3_column_text(dataStmt, i));
                        out << (colText ? colText : "NULL") << (i < cols - 1 ? " | " : "");
                    }
                    out << "\n";
                }

                sqlite3_finalize(dataStmt);
            } else {
                out << "Failed to query table: " << tableName << "\n";
            }

            out << "\n";
        }
        sqlite3_finalize(stmt);
    } else {
        out << "Failed to list tables\n";
    }

    out.close();
}

// --- MaxOwnIngredients Implementation ---
void SaveGameManager::MaxOwnIngredients(sqlite3* db) {
    if (!m_isSaveFileLoaded || !m_saveData.contains("Ingredients") || !m_saveData["Ingredients"].is_object()) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded or 'Ingredients' section not found/invalid for MaxOwnIngredients.");
        return;
    }
    if (!db) {
        LogMessage(LOG_ERROR_LEVEL, "Database handle (g_refDb) is null for MaxOwnIngredients.");
        return;
    }

    nlohmann::json& ingredients_json_map = m_saveData["Ingredients"];
    
    int updated_count = 0;
    int skipped_count = 0; // Counter for items skipped due to rules or issues

    // SQL to get MaxCount for an ingredient ID
    sqlite3_stmt *stmt = nullptr; // Prepare statement once outside the loop
    std::string sql = "SELECT MaxCount FROM Items WHERE ItemDataID = ?;";
    int rc_prepare = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    if (rc_prepare != SQLITE_OK) {
        LogMessage(LOG_ERROR_LEVEL, ("SQL prepare failed for MaxOwnIngredients: " + std::string(sqlite3_errmsg(db))).c_str());
        return; // Exit if prepare fails
    }

    for (auto it = ingredients_json_map.begin(); it != ingredients_json_map.end(); ++it) {
        // Ensure "ingredientsID" exists and is an integer
        if (it.value().contains("ingredientsID") && it.value()["ingredientsID"].is_number_integer()) {
            int ingredients_id = it.value()["ingredientsID"].get<int>();

            sqlite3_reset(stmt); // Reset statement for reuse in each iteration
            sqlite3_bind_int(stmt, 1, ingredients_id);

            int max_count_from_db = 0; // Initialize to 0; will be retrieved from DB
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                max_count_from_db = sqlite3_column_int(stmt, 0);
            } else {
                LogMessage(LOG_WARNING_LEVEL, ("MaxCount not found for existing ingredient ID: " + std::to_string(ingredients_id) + " in Items table. Skipping update.").c_str());
                skipped_count++; // Count as skipped due to DB lookup failure
                continue; // Skip to next if item data not found
            }

            // Determine the target count based on the item's MaxCount from DB
            int target_count = GetDesiredMaxCountForTier(max_count_from_db);

            if (target_count > 0) { // target_count == 0 indicates skipping
                // Update the count to the determined target
                it.value()["count"] = target_count;
                updated_count++;
            } else {
                // Item should be skipped (e.g., MaxCount == 1 or unhandled tier)
                LogMessage(LOG_INFO_LEVEL, ("Skipping owned ingredient ID " + std::to_string(ingredients_id) + " with MaxCount " + std::to_string(max_count_from_db) + " as per tier rules.").c_str());
                skipped_count++;
            }
        } else {
            LogMessage(LOG_WARNING_LEVEL, ("Skipping ingredient entry without valid 'ingredientsID': " + it.key() + ". Malformed entry.").c_str());
            skipped_count++; // Count as skipped due to invalid JSON structure
        }
    }

    sqlite3_finalize(stmt); // Clean up the prepared statement once after the loop
    LogMessage(LOG_INFO_LEVEL, ("MaxOwnIngredients: Updated " + std::to_string(updated_count) + " owned ingredients. Skipped " + std::to_string(skipped_count) + " ingredients.").c_str());
    
    // DumpSaveDataToFile(m_saveData);
    // DumpSQLiteToText(db, "db_dump.txt");
}

// --- MaxOwnMaterials Implementation ---
void SaveGameManager::MaxOwnMaterials(sqlite3* db) {
    if (!m_isSaveFileLoaded || !m_saveData.contains("InventoryItemSlot") || !m_saveData["InventoryItemSlot"].is_object()) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded or 'InventoryItemSlot' section not found/invalid for MaxOwnMaterials.");
        return;
    }
    if (!db) {
        LogMessage(LOG_ERROR_LEVEL, "Database handle (g_refDb) is null for MaxOwnMaterials.");
        return;
    }

    nlohmann::json& material_json_map = m_saveData["InventoryItemSlot"];
    
    int updated_count = 0;
    int skipped_count = 0; // Counter for items skipped due to rules or issues

    // SQL to get MaxCount for an Item ID
    sqlite3_stmt *stmt_material = nullptr; // Prepare statement once outside the loop
    std::string sql_material = "SELECT MaxCount FROM Items WHERE TID = ?;";
    int rc_prepare_material = sqlite3_prepare_v2(db, sql_material.c_str(), -1, &stmt_material, NULL);
    if (rc_prepare_material != SQLITE_OK) {
        LogMessage(LOG_ERROR_LEVEL, ("SQL prepare failed for MaxOwnMaterial: " + std::string(sqlite3_errmsg(db))).c_str());
        return; // Exit if prepare fails
    }

    for (auto it = material_json_map.begin(); it != material_json_map.end(); ++it) {
        // Ensure "ItemID" exists and is an integer
        if (it.value().contains("itemID") && it.value()["itemID"].is_number_integer()) {
            int material_id = it.value()["itemID"].get<int>();

            sqlite3_reset(stmt_material); // Reset statement for reuse in each iteration
            sqlite3_bind_int(stmt_material, 1, material_id);

            int max_count_from_db = 0; // Initialize to 0; will be retrieved from DB
            if (sqlite3_step(stmt_material) == SQLITE_ROW) {
                max_count_from_db = sqlite3_column_int(stmt_material, 0);
            } else {
                LogMessage(LOG_WARNING_LEVEL, ("MaxCount not found for existing TID: " + std::to_string(material_id) + " in Items table. Skipping update.").c_str());
                skipped_count++; // Count as skipped due to DB lookup failure
                continue; // Skip to next if item data not found
            }

            // Determine the target count based on the item's MaxCount from DB
            int target_count = GetDesiredMaxCountForTier(max_count_from_db);

            if (target_count > 0) { // target_count == 0 indicates skipping
                // Update the count to the determined target
                it.value()["totalCount"] = target_count;
                updated_count++;
            } else {
                // Item should be skipped (e.g., MaxCount == 1 or unhandled tier)
                LogMessage(LOG_INFO_LEVEL, ("Skipping owned TID " + std::to_string(material_id) + " with MaxCount " + std::to_string(max_count_from_db) + " as per tier rules.").c_str());
                skipped_count++;
            }
        } else {
            LogMessage(LOG_WARNING_LEVEL, ("Skipping material entry without valid 'TID': " + it.key() + ". Malformed entry.").c_str());
            skipped_count++; // Count as skipped due to invalid JSON structure
        }
    }

    sqlite3_finalize(stmt_material); // Clean up the prepared statement once after the loop
    LogMessage(LOG_INFO_LEVEL, ("MaxOwnMaterial: Updated " + std::to_string(updated_count) + " owned material. Skipped " + std::to_string(skipped_count) + " material.").c_str());

    // DumpSaveDataToFile(m_saveData);
    // DumpSQLiteToText(db, "db_dump.txt");
}

// --- MaxOwnStaffLevel Implementation ---
void SaveGameManager::MaxOwnStaffLevel() {
    if (!m_isSaveFileLoaded || !m_saveData.contains("Staff")) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded or 'Staff' section not found/invalid for MaxOwnStaffLevel.");
        return;
    }

    // Max All Staff Level
    nlohmann::json& hired_staff_json_map = m_saveData["Staff"];
    for (auto it = hired_staff_json_map.begin(); it != hired_staff_json_map.end(); ++it) {
        std::string staff_name = it.value()["name"].get<std::string>();

        if(staff_name == "Staff_Dave"){
            continue;
        }

        it.value()["level"] = 20;
    }
}

// --- SQLite Callback for batch querying ingredients (for MaxAllIngredients) ---
// This is a static function that can be accessed by sqlite3_exec
static int callbackGetAllIngredients(void *data, int argc, char **argv, char **azColName){
    std::vector<std::map<std::string, int>>* results = static_cast<std::vector<std::map<std::string, int>>*>(data);
    std::map<std::string, int> row;
    for(int i = 0; i < argc; i++){
        // Safely convert to int, assumes columns are numeric where relevant
        row[azColName[i]] = argv[i] ? std::stoi(argv[i]) : 0;
    }
    results->push_back(row);
    return 0;
}

// --- MaxAllIngredients Implementation ---
void SaveGameManager::MaxAllIngredients(sqlite3* db) {
    if (!m_isSaveFileLoaded) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded for MaxAllIngredients.");
        return;
    }
    if (!db) {
        LogMessage(LOG_ERROR_LEVEL, "Database handle (g_refDb) is null for MaxAllIngredients.");
        return;
    }

    if (!m_saveData.contains("Ingredients") || !m_saveData["Ingredients"].is_object()) {
        LogMessage(LOG_INFO_LEVEL, "Creating empty 'Ingredients' section in save data.");
        m_saveData["Ingredients"] = nlohmann::json::object();
    }

    nlohmann::json& ingredients_json_map = m_saveData["Ingredients"];

    std::string default_lastGainTime = "04/01/2025 12:34:56";
    std::string default_lastGainGameTime = "10/03/2022 08:30:52";

    // If the ingredient map isn't empty, try to get a timestamp from the first entry.
    if (!ingredients_json_map.empty()) {
        auto& first_item_value = ingredients_json_map.begin().value();
        if (first_item_value.contains("lastGainTime") && first_item_value["lastGainTime"].is_string()) {
            default_lastGainTime = first_item_value["lastGainTime"].get<std::string>();
        }
        if (first_item_value.contains("lastGainGameTime") && first_item_value["lastGainGameTime"].is_string()) {
            default_lastGainGameTime = first_item_value["lastGainGameTime"].get<std::string>();
        }
    }
    LogMessage(LOG_INFO_LEVEL, ("Using timestamps '" + default_lastGainTime + "' / '" + default_lastGainGameTime + "' for new ingredients.").c_str());

    std::vector<std::map<std::string, int>> all_db_ingredients;
    std::string sql_query = R"(
        SELECT
            I.TID AS ingredientsID_for_save_file_key,
            T.TID AS parentID,
            T.MaxCount
        FROM
            Ingredients AS I
        JOIN
            Items AS T
        ON
            I.TID = T.ItemDataID;
    )";

    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(db, sql_query.c_str(), callbackGetAllIngredients, &all_db_ingredients, &zErrMsg);
    if (rc != SQLITE_OK) {
        LogMessage(LOG_ERROR_LEVEL, ("SQL error getting all ingredients: " + std::string(zErrMsg)).c_str());
        sqlite3_free(zErrMsg);
        return;
    }
    LogMessage(LOG_INFO_LEVEL, ("Retrieved " + std::to_string(all_db_ingredients.size()) + " potential ingredients from database.").c_str());

    int updated_count = 0;
    int added_count = 0;
    int skipped_count = 0; // Counter for items skipped due to rules or issues

    for (const auto& db_ingredient : all_db_ingredients) {
        // Ensure all required keys exist before accessing, to prevent exceptions
        if (!db_ingredient.count("ingredientsID_for_save_file_key") ||
            !db_ingredient.count("parentID") ||
            !db_ingredient.count("MaxCount")) {
            LogMessage(LOG_WARNING_LEVEL, "Skipping database ingredient entry due to missing required fields (ingredientsID_for_save_file_key, parentID, or MaxCount).");
            skipped_count++;
            continue;
        }

        int ingredients_id_from_db = db_ingredient.at("ingredientsID_for_save_file_key");
        int parent_id_from_db = db_ingredient.at("parentID");
        int max_count_from_db = db_ingredient.at("MaxCount");

        // Determine the target count based on the item's MaxCount from DB
        int target_count = GetDesiredMaxCountForTier(max_count_from_db);

        if (target_count == 0) {
            // Item should be skipped (e.g., MaxCount == 1 or unhandled tier)
            LogMessage(LOG_INFO_LEVEL, ("Skipping ingredient ID " + std::to_string(ingredients_id_from_db) + " with MaxCount " + std::to_string(max_count_from_db) + " from database as per tier rules.").c_str());
            skipped_count++;
            continue; // Skip to next if this item should not be maxed/added
        }

        // The ingredient ID from the DB is used as the key in the JSON map
        std::string ingredient_key = std::to_string(ingredients_id_from_db);

        if (ingredients_json_map.contains(ingredient_key)) {
            // Ingredient already exists, just update its count
            ingredients_json_map[ingredient_key]["count"] = target_count;
            updated_count++;
        } else {
            // Ingredient does not exist, add it
            nlohmann::json new_ingredient_entry;
            new_ingredient_entry["ingredientsID"] = ingredients_id_from_db;
            new_ingredient_entry["level"] = 1; // Default level
            new_ingredient_entry["parentID"] = parent_id_from_db;
            new_ingredient_entry["count"] = target_count; // Set to the determined target count
            new_ingredient_entry["branchCount"] = 0; // Default
            new_ingredient_entry["lastGainTime"] = default_lastGainTime;
            new_ingredient_entry["lastGainGameTime"] = default_lastGainGameTime;
            new_ingredient_entry["isNew"] = true; // Mark as new
            new_ingredient_entry["placeTagMask"] = 1; // Default

            ingredients_json_map[ingredient_key] = new_ingredient_entry;
            added_count++;
        }
    }
    LogMessage(LOG_INFO_LEVEL, ("MaxAllIngredients: Updated " + std::to_string(updated_count) + " existing, added " + std::to_string(added_count) + " new, skipped " + std::to_string(skipped_count) + " ingredients.").c_str());
}

// --- Static Helper: GetDefaultSaveGameDirectoryAndLatestFile Implementation ---
// Discovers the default save game directory for Dave the Diver and identifies the most recent save file.
std::filesystem::path SaveGameManager::GetDefaultSaveGameDirectoryAndLatestFile(std::string& latestSaveFileName) {
    PWSTR pszPath = NULL;
    std::filesystem::path baseSavePath;

    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, NULL, &pszPath);

    if (SUCCEEDED(hr)) {
        baseSavePath = pszPath;
        CoTaskMemFree(pszPath);

        baseSavePath /= L"nexon";
        baseSavePath /= L"DAVE THE DIVER";
        baseSavePath /= L"SteamSData";
    } else {
        LogMessage(LOG_ERROR_LEVEL, "Failed to get AppData LocalLow path using SHGetKnownFolderPath.");
        const char* localAppDataEnv = getenv("LOCALAPPDATA");
        if (localAppDataEnv) {
            baseSavePath = localAppDataEnv;
            baseSavePath = baseSavePath.parent_path() / "LocalLow"; // Correcting if LOCALAPPDATA gives ...\Roaming
            baseSavePath /= L"nexon";
            baseSavePath /= L"DAVE THE DIVER";
            baseSavePath /= L"SteamSData";
        } else {
            LogMessage(LOG_ERROR_LEVEL, "LOCALAPPDATA environment variable not found either.");
            return {};
        }
    }

    std::filesystem::path steamIDPath;

    if (std::filesystem::exists(baseSavePath) && std::filesystem::is_directory(baseSavePath)) {
        for (const auto& entry : std::filesystem::directory_iterator(baseSavePath)) {
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();
                if (!folderName.empty() && std::all_of(folderName.begin(), folderName.end(), ::isdigit)) {
                    steamIDPath = entry.path();
                    LogMessage(LOG_INFO_LEVEL, (std::string("Found SteamID folder: ") + steamIDPath.string()).c_str());
                    break;
                }
            }
        }
    }

    if (steamIDPath.empty()) {
        LogMessage(LOG_ERROR_LEVEL, (std::string("Could not find a SteamID folder under: ") + baseSavePath.string()).c_str());
        // Fallback: If no SteamID folder found, assume the save files might be directly under baseSavePath
        steamIDPath = baseSavePath;
    }

    std::filesystem::file_time_type lastWriteTime;
    std::filesystem::path mostRecentSaveFile;

    if (std::filesystem::exists(steamIDPath) && std::filesystem::is_directory(steamIDPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(steamIDPath)) {
            if (entry.is_regular_file()) {
                std::string fileName = entry.path().filename().string();
                if (fileName.length() > 10 && fileName.substr(0, 8) == "GameSave" &&
                    fileName.substr(fileName.length() - 7) == "_GD.sav") {
                    try {
                        auto fileTime = std::filesystem::last_write_time(entry.path());
                        if (mostRecentSaveFile.empty() || fileTime > lastWriteTime) {
                            lastWriteTime = fileTime;
                            mostRecentSaveFile = entry.path();
                        }
                    } catch (const std::filesystem::filesystem_error& e) {
                        LogMessage(LOG_ERROR_LEVEL, (std::string("Error getting write time for ") + fileName + ": " + e.what()).c_str());
                    }
                }
            }
        }
    }

    if (!mostRecentSaveFile.empty()) {
        latestSaveFileName = mostRecentSaveFile.string();
        LogMessage(LOG_INFO_LEVEL, (std::string("Identified most recent save file: ") + latestSaveFileName).c_str());
    } else {
        LogMessage(LOG_INFO_LEVEL, (std::string("No GameSave_XX_GD.sav files found in " + steamIDPath.string())).c_str());
        latestSaveFileName.clear();
    }

    return steamIDPath;
}


