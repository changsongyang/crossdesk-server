#include "device_db_manager.h"

#include <openssl/sha.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "log.h"

namespace {

std::string ColumnText(sqlite3_stmt* stmt, int column) {
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

std::string SqliteExecError(sqlite3* db, char* err_msg) {
  return err_msg ? std::string(err_msg) : std::string(sqlite3_errmsg(db));
}

}  // namespace

DeviceDBManager::DeviceDBManager(const std::string& db_path) : db_(nullptr) {
  try {
    std::filesystem::path path(db_path);
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
  } catch (const std::exception& e) {
    throw std::runtime_error("Failed to create parent directory for DB: " +
                             std::string(e.what()));
  }

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::string error =
        db_ ? sqlite3_errmsg(db_) : std::string(sqlite3_errstr(rc));
    LOG_ERROR("Failed to open database, {}", error);
    if (db_) {
      sqlite3_close(db_);
    }
    db_ = nullptr;
    throw std::runtime_error("Failed to open database: " + error);
  }
  try {
    InitDB();
  } catch (...) {
    sqlite3_close(db_);
    db_ = nullptr;
    throw;
  }
}

DeviceDBManager::~DeviceDBManager() {
  if (db_) sqlite3_close(db_);
}

void DeviceDBManager::InitDB() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in InitDB.");
    throw std::runtime_error("Database is not initialized in InitDB.");
  }

  const char* sql_devices =
      "CREATE TABLE IF NOT EXISTS devices ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "device_id TEXT UNIQUE NOT NULL,"
      "password_salt TEXT NOT NULL,"
      "password_hash TEXT NOT NULL);";

  const char* sql_seq =
      "CREATE TABLE IF NOT EXISTS device_id_seq ("
      "next_id INTEGER NOT NULL);";

  const char* sql_seq_init =
      "INSERT INTO device_id_seq (next_id) "
      "SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM device_id_seq);";

  const char* sql_presence =
      "CREATE TABLE IF NOT EXISTS device_presence ("
      "device_id TEXT PRIMARY KEY,"
      "online INTEGER NOT NULL,"
      "updated_at INTEGER NOT NULL"
      ");";

  const char* sql_user_devices =
      "CREATE TABLE IF NOT EXISTS user_devices ("
      "user_id TEXT NOT NULL,"
      "device_id TEXT NOT NULL,"
      "PRIMARY KEY (user_id, device_id)"
      ");";

  char* err_msg = nullptr;

  if (sqlite3_exec(db_, sql_devices, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to create devices table: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to create devices table: " + error);
  }

  if (sqlite3_exec(db_, sql_seq, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to create device_id_seq table: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to create device_id_seq table: " + error);
  }

  if (sqlite3_exec(db_, sql_seq_init, nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to initialize device_id_seq: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to initialize device_id_seq: " + error);
  }

  if (sqlite3_exec(db_, sql_presence, nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to create device_presence table: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to create device_presence table: " +
                             error);
  }

  if (sqlite3_exec(db_, sql_user_devices, nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to create user_devices table: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to create user_devices table: " + error);
  }
}

std::string DeviceDBManager::Sha256(const std::string& str) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(str.c_str()), str.size(), hash);

  std::stringstream ss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

  return ss.str();
}

std::string DeviceDBManager::GenerateSalt() {
  static const char charset[] = "0123456789ABCDEF";
  static std::mt19937 rng(static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  std::uniform_int_distribution<int> dist(0, 15);

  std::string salt;
  for (int i = 0; i < 16; ++i) {
    salt += charset[dist(rng)];
  }
  return salt;
}

std::string DeviceDBManager::HashPasswordWithSalt(const std::string& salt,
                                                  const std::string& password) {
  return Sha256(salt + password);
}

bool DeviceDBManager::DeviceIdExists(const std::string& device_id) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr || device_id.empty()) {
    return false;
  }

  const char* sql = "SELECT 1 FROM devices WHERE device_id = ? LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;

  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
  bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);

  return exists;
}

std::string DeviceDBManager::GenerateDeviceId() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in GenerateDeviceId.");
    return {};
  }

  const int MIN_ID = 100000000;
  const int MAX_ID = 999999999;
  const int MAX_RETRIES = 100;

  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> dist(MIN_ID, MAX_ID);

  // try to generate unique ID
  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    int obfuscated_id = dist(rng);

    char buf[10] = {0};
    snprintf(buf, sizeof(buf), "%09d", obfuscated_id);
    std::string device_id(buf);

    // check if ID already exists
    if (!DeviceIdExists(device_id)) {
      return device_id;
    }
  }

  LOG_ERROR("Failed to generate unique device ID after {} attempts.",
            MAX_RETRIES);
  return {};
}

std::string DeviceDBManager::GeneratePassword() {
  static const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  static std::mt19937 rng(static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  std::uniform_int_distribution<int> dist(0,
                                          sizeof(charset) - 2);  // exclude '\0'

  std::string pwd;
  for (int i = 0; i < 6; ++i) {
    pwd += charset[dist(rng)];
  }
  return pwd;
}

DeviceCredential DeviceDBManager::AddDevice(const std::string& device_id,
                                            const std::string& password) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized.");
    return {};
  }

  if (!device_id.empty() && device_id != "web") {
    const char* select_sql =
        "SELECT password_salt, password_hash FROM devices WHERE device_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      LOG_ERROR("Failed to prepare select statement.");
      return {};
    }

    sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      // Device exists
      std::string salt = ColumnText(stmt, 0);
      std::string stored_hash = ColumnText(stmt, 1);
      std::string hash = HashPasswordWithSalt(salt, password);

      sqlite3_finalize(stmt);
      if (stored_hash != hash) {
        // Update password
        const char* update_sql =
            "UPDATE devices SET password_hash = ?, password_salt = ? WHERE "
            "device_id = ?;";
        if (sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr) !=
            SQLITE_OK) {
          LOG_ERROR("Failed to prepare update statement.");
          return {};
        }
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, device_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
          LOG_ERROR("Failed to update password.");
          return {};
        }
        return {device_id, "", true};  // password updated
      } else {
        return {device_id, "", false};  // same password
      }
    }
    sqlite3_finalize(stmt);
  }

  // Device not exists or device_id is empty — generate new
  const int MAX_RETRIES = 10;
  for (int i = 0; i < MAX_RETRIES; ++i) {
    std::string new_id;
    if (device_id == "web") {
      std::string generated_id = GenerateDeviceId();
      if (generated_id.empty()) {
        LOG_ERROR("Failed to generate device ID for web client.");
        return {};
      }
      new_id = device_id + "-" + generated_id;
    } else {
      new_id = GenerateDeviceId();
      if (new_id.empty()) {
        LOG_ERROR("Failed to generate device ID.");
        return {};
      }
    }

    // Check if the generated ID (including web- prefix) already exists
    if (DeviceIdExists(new_id)) {
      LOG_WARN("Generated ID {} already exists, retrying...", new_id);
      continue;
    }

    std::string new_pwd = GeneratePassword();
    if (new_pwd.empty()) {
      LOG_ERROR("Failed to generate password.");
      return {};
    }

    std::string salt = GenerateSalt();
    std::string hash = HashPasswordWithSalt(salt, new_pwd);

    // Use transaction to reduce race condition
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Double-check ID uniqueness within transaction
    if (DeviceIdExists(new_id)) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      LOG_WARN("Generated ID {} already exists, retrying...", new_id);
      continue;
    }

    const char* insert_sql =
        "INSERT INTO devices (device_id, password_hash, password_salt) VALUES "
        "(?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      LOG_ERROR("Failed to prepare insert statement.");
      return {};
    }

    sqlite3_bind_text(stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
      sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
      // For web clients, return empty password
      if (device_id == "web") {
        return {new_id, "", false};
      }
      return {new_id, new_pwd, false};
    } else if (rc == SQLITE_CONSTRAINT) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      LOG_WARN(
          "Insert failed due to constraint (ID may have been inserted "
          "concurrently): {}",
          new_id);
      continue;
    } else {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      LOG_ERROR("Insert device failed: {}", sqlite3_errmsg(db_));
      return {};
    }
  }

  LOG_ERROR("Failed to generate unique device_id after {} attempts.",
            MAX_RETRIES);
  return {};
}

int DeviceDBManager::VerifyDevice(const std::string& device_id,
                                  const std::string& password) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in VerifyDevice.");
    return -1;
  }

  const char* sql =
      "SELECT password_salt, password_hash FROM devices WHERE device_id = ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);

  // Check if device exists
  int result = -2;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    std::string salt = ColumnText(stmt, 0);
    std::string stored_hash = ColumnText(stmt, 1);

    std::string hash = HashPasswordWithSalt(salt, password);
    if (hash == stored_hash) {
      // Password is correct
      result = 0;
    } else {
      // Password is incorrect
      result = -1;
    }
  }

  sqlite3_finalize(stmt);
  return result;
}

bool DeviceDBManager::UpdatePassword(const std::string& device_id,
                                     const std::string& new_password) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in UpdatePassword.");
    return false;
  }

  std::string salt = GenerateSalt();
  std::string hash = HashPasswordWithSalt(salt, new_password);

  const char* sql =
      "UPDATE devices SET password_salt = ?, password_hash = ? WHERE device_id "
      "= ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, salt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, device_id.c_str(), -1, SQLITE_TRANSIENT);

  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool DeviceDBManager::RemoveDevice(const std::string& device_id) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in RemoveDevice.");
    return false;
  }

  const char* sql = "DELETE FROM devices WHERE device_id = ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool DeviceDBManager::SetDeviceOnline(const std::string& device_id,
                                      bool online) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in SetDeviceOnline.");
    return false;
  }

  const char* sql =
      "INSERT INTO device_presence (device_id, online, updated_at) "
      "VALUES (?, ?, strftime('%s','now')) "
      "ON CONFLICT(device_id) DO UPDATE SET online=excluded.online, "
      "updated_at=excluded.updated_at;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, online ? 1 : 0);
  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

int DeviceDBManager::GetOnlineDeviceCount() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in GetOnlineDeviceCount.");
    return 0;
  }

  const char* sql =
      "SELECT COUNT(*) FROM device_presence "
      "WHERE online = 1 AND device_id NOT LIKE 'web-%';";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

std::vector<std::pair<std::string, bool>> DeviceDBManager::BatchQueryOnline(
    const std::vector<std::string>& device_ids) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  std::vector<std::pair<std::string, bool>> result;
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in BatchQueryOnline.");
    return result;
  }
  if (device_ids.empty()) {
    return result;
  }

  std::stringstream ss;
  ss << "SELECT device_id, online FROM device_presence WHERE device_id IN (";
  for (size_t i = 0; i < device_ids.size(); ++i) {
    ss << (i == 0 ? "?" : ",?");
  }
  ss << ");";
  std::string sql = ss.str();

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }
  for (size_t i = 0; i < device_ids.size(); ++i) {
    sqlite3_bind_text(stmt, static_cast<int>(i + 1), device_ids[i].c_str(), -1,
                      SQLITE_TRANSIENT);
  }

  std::unordered_map<std::string, bool> map;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::string id = ColumnText(stmt, 0);
    int online = sqlite3_column_int(stmt, 1);
    map[id] = (online != 0);
  }
  sqlite3_finalize(stmt);

  for (const auto& id : device_ids) {
    auto it = map.find(id);
    bool online = (it != map.end()) ? it->second : false;
    result.emplace_back(id, online);
  }
  return result;
}

bool DeviceDBManager::SetUserDevices(
    const std::string& user_id, const std::vector<std::string>& device_ids) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in SetUserDevices.");
    return false;
  }

  char* err_msg = nullptr;
  if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    LOG_ERROR("Failed to begin SetUserDevices transaction: {}",
              err_msg ? err_msg : sqlite3_errmsg(db_));
    sqlite3_free(err_msg);
    return false;
  }

  const char* delete_sql = "DELETE FROM user_devices WHERE user_id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, delete_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }
  sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);

  if (!ok) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }

  const char* insert_sql =
      "INSERT OR IGNORE INTO user_devices (user_id, device_id) VALUES (?, ?);";
  if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }

  for (const auto& device_id : device_ids) {
    if (device_id.empty()) {
      continue;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, device_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      ok = false;
      break;
    }
  }
  sqlite3_finalize(stmt);

  if (!ok) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }

  if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
    LOG_ERROR("Failed to commit SetUserDevices transaction: {}",
              err_msg ? err_msg : sqlite3_errmsg(db_));
    sqlite3_free(err_msg);
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }

  return true;
}

std::vector<std::string> DeviceDBManager::GetUserDevices(
    const std::string& user_id) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  std::vector<std::string> result;
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in GetUserDevices.");
    return result;
  }

  const char* sql =
      "SELECT device_id FROM user_devices WHERE user_id = ? ORDER BY "
      "device_id;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }

  sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    result.push_back(ColumnText(stmt, 0));
  }
  sqlite3_finalize(stmt);

  return result;
}
