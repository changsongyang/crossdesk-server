#include "device_db_manager.h"

#include <openssl/sha.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
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

std::string EscapeLikePattern(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    if (ch == '%' || ch == '_' || ch == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

bool ColumnExists(sqlite3* db, const std::string& table,
                  const std::string& column) {
  std::string sql = "PRAGMA table_info(" + table + ");";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (ColumnText(stmt, 1) == column) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);
  return found;
}

void EnsureIntegerColumn(sqlite3* db, const std::string& table,
                         const std::string& column) {
  if (ColumnExists(db, table, column)) {
    return;
  }

  std::string sql = "ALTER TABLE " + table + " ADD COLUMN " + column +
                    " INTEGER NOT NULL DEFAULT 0;";
  char* err_msg = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = SqliteExecError(db, err_msg);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to add " + table + "." + column +
                             " column: " + error);
  }
}

int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
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
      "updated_at INTEGER NOT NULL,"
      "online_since INTEGER NOT NULL DEFAULT 0,"
      "total_online_seconds INTEGER NOT NULL DEFAULT 0,"
      "total_control_seconds INTEGER NOT NULL DEFAULT 0,"
      "total_controlled_seconds INTEGER NOT NULL DEFAULT 0"
      ");";

  const char* sql_user_devices =
      "CREATE TABLE IF NOT EXISTS user_devices ("
      "user_id TEXT NOT NULL,"
      "device_id TEXT NOT NULL,"
      "PRIMARY KEY (user_id, device_id)"
      ");";

  const char* sql_remote_control_sessions =
      "CREATE TABLE IF NOT EXISTS remote_control_sessions ("
      "transmission_id TEXT NOT NULL,"
      "guest_id TEXT NOT NULL,"
      "host_id TEXT NOT NULL,"
      "started_at INTEGER NOT NULL,"
      "PRIMARY KEY (transmission_id, guest_id)"
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

  EnsureIntegerColumn(db_, "device_presence", "online_since");
  EnsureIntegerColumn(db_, "device_presence", "total_online_seconds");
  EnsureIntegerColumn(db_, "device_presence", "total_control_seconds");
  EnsureIntegerColumn(db_, "device_presence", "total_controlled_seconds");

  const char* sql_presence_backfill =
      "UPDATE device_presence SET online_since = updated_at "
      "WHERE online = 1 AND online_since = 0;";
  if (sqlite3_exec(db_, sql_presence_backfill, nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to backfill device_presence online_since: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to backfill device_presence: " + error);
  }

  if (sqlite3_exec(db_, sql_user_devices, nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to create user_devices table: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to create user_devices table: " + error);
  }

  if (sqlite3_exec(db_, sql_remote_control_sessions, nullptr, nullptr,
                   &err_msg) != SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to create remote_control_sessions table: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to create remote_control_sessions table: " +
                             error);
  }

  if (sqlite3_exec(db_, "DELETE FROM remote_control_sessions;", nullptr,
                   nullptr, &err_msg) != SQLITE_OK) {
    std::string error = SqliteExecError(db_, err_msg);
    LOG_ERROR("Failed to clear remote_control_sessions: {}", error);
    sqlite3_free(err_msg);
    throw std::runtime_error("Failed to clear remote_control_sessions: " +
                             error);
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

  const char* online_sql =
      "INSERT INTO device_presence "
      "(device_id, online, updated_at, online_since, total_online_seconds) "
      "VALUES (?, 1, CAST(strftime('%s','now') AS INTEGER), "
      "CAST(strftime('%s','now') AS INTEGER), 0) "
      "ON CONFLICT(device_id) DO UPDATE SET "
      "online=1, "
      "updated_at=CAST(strftime('%s','now') AS INTEGER), "
      "online_since=CASE "
      "WHEN device_presence.online = 1 AND device_presence.online_since > 0 "
      "THEN device_presence.online_since "
      "ELSE CAST(strftime('%s','now') AS INTEGER) END;";

  const char* offline_sql =
      "INSERT INTO device_presence "
      "(device_id, online, updated_at, online_since, total_online_seconds) "
      "VALUES (?, 0, CAST(strftime('%s','now') AS INTEGER), 0, 0) "
      "ON CONFLICT(device_id) DO UPDATE SET "
      "total_online_seconds=device_presence.total_online_seconds + "
      "CASE WHEN device_presence.online = 1 AND "
      "device_presence.online_since > 0 THEN "
      "MAX(0, CAST(strftime('%s','now') AS INTEGER) - "
      "device_presence.online_since) ELSE 0 END, "
      "online=0, "
      "updated_at=CAST(strftime('%s','now') AS INTEGER), "
      "online_since=0;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, online ? online_sql : offline_sql, -1, &stmt,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

bool DeviceDBManager::StartRemoteControlSession(
    const std::string& transmission_id, const std::string& host_id,
    const std::string& guest_id) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in StartRemoteControlSession.");
    return false;
  }
  if (transmission_id.empty() || host_id.empty() || guest_id.empty() ||
      host_id == guest_id) {
    return false;
  }

  char* err_msg = nullptr;
  if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    LOG_ERROR("Failed to begin StartRemoteControlSession transaction: {}",
              err_msg ? err_msg : sqlite3_errmsg(db_));
    sqlite3_free(err_msg);
    return false;
  }

  auto rollback = [this]() {
    char* rollback_err = nullptr;
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
    sqlite3_free(rollback_err);
  };

  auto ensure_presence = [this](const std::string& device_id) {
    const char* sql =
        "INSERT INTO device_presence "
        "(device_id, online, updated_at, online_since, total_online_seconds, "
        "total_control_seconds, total_controlled_seconds) "
        "VALUES (?, 0, CAST(strftime('%s','now') AS INTEGER), 0, 0, 0, 0) "
        "ON CONFLICT(device_id) DO NOTHING;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
  };

  if (!ensure_presence(host_id) || !ensure_presence(guest_id)) {
    rollback();
    return false;
  }

  const char* sql =
      "INSERT OR IGNORE INTO remote_control_sessions "
      "(transmission_id, guest_id, host_id, started_at) "
      "VALUES (?, ?, ?, CAST(strftime('%s','now') AS INTEGER));";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    rollback();
    return false;
  }
  sqlite3_bind_text(stmt, 1, transmission_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, guest_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, host_id.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  if (!ok) {
    rollback();
    return false;
  }

  if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
    LOG_ERROR("Failed to commit StartRemoteControlSession transaction: {}",
              err_msg ? err_msg : sqlite3_errmsg(db_));
    sqlite3_free(err_msg);
    rollback();
    return false;
  }
  return true;
}

bool DeviceDBManager::EndRemoteControlSession(
    const std::string& transmission_id, const std::string& host_id,
    const std::string& guest_id) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in EndRemoteControlSession.");
    return false;
  }
  if (transmission_id.empty() || guest_id.empty()) {
    return false;
  }

  const char* select_sql =
      "SELECT host_id, started_at FROM remote_control_sessions "
      "WHERE transmission_id = ? AND guest_id = ?;";
  sqlite3_stmt* select_stmt = nullptr;
  if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(select_stmt, 1, transmission_id.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(select_stmt, 2, guest_id.c_str(), -1, SQLITE_TRANSIENT);

  std::string stored_host_id;
  int64_t started_at = 0;
  bool found = false;
  if (sqlite3_step(select_stmt) == SQLITE_ROW) {
    stored_host_id = ColumnText(select_stmt, 0);
    started_at = sqlite3_column_int64(select_stmt, 1);
    found = true;
  }
  sqlite3_finalize(select_stmt);
  if (!found) {
    return true;
  }

  std::string effective_host_id =
      stored_host_id.empty() ? host_id : stored_host_id;
  if (effective_host_id.empty()) {
    return false;
  }
  int64_t duration = std::max<int64_t>(0, NowSeconds() - started_at);

  char* err_msg = nullptr;
  if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    LOG_ERROR("Failed to begin EndRemoteControlSession transaction: {}",
              err_msg ? err_msg : sqlite3_errmsg(db_));
    sqlite3_free(err_msg);
    return false;
  }

  auto rollback = [this]() {
    char* rollback_err = nullptr;
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
    sqlite3_free(rollback_err);
  };

  auto add_duration = [this](const char* column, const std::string& device_id,
                             int64_t value) {
    std::string sql =
        std::string("UPDATE device_presence SET ") + column + " = " + column +
        " + ? WHERE device_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
      return false;
    }
    sqlite3_bind_int64(stmt, 1, value);
    sqlite3_bind_text(stmt, 2, device_id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
  };

  if (!add_duration("total_control_seconds", guest_id, duration) ||
      !add_duration("total_controlled_seconds", effective_host_id, duration)) {
    rollback();
    return false;
  }

  const char* delete_sql =
      "DELETE FROM remote_control_sessions "
      "WHERE transmission_id = ? AND guest_id = ?;";
  sqlite3_stmt* delete_stmt = nullptr;
  if (sqlite3_prepare_v2(db_, delete_sql, -1, &delete_stmt, nullptr) !=
      SQLITE_OK) {
    rollback();
    return false;
  }
  sqlite3_bind_text(delete_stmt, 1, transmission_id.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(delete_stmt, 2, guest_id.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = (sqlite3_step(delete_stmt) == SQLITE_DONE);
  sqlite3_finalize(delete_stmt);
  if (!ok) {
    rollback();
    return false;
  }

  if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
    LOG_ERROR("Failed to commit EndRemoteControlSession transaction: {}",
              err_msg ? err_msg : sqlite3_errmsg(db_));
    sqlite3_free(err_msg);
    rollback();
    return false;
  }
  return true;
}

int DeviceDBManager::GetOnlineDeviceCount() {
  return CountOnlineDevices();
}

int DeviceDBManager::CountOnlineDevices(const std::string& search) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in CountOnlineDevices.");
    return 0;
  }

  std::string sql =
      "SELECT COUNT(*) FROM device_presence "
      "WHERE online = 1 "
      "AND device_id NOT LIKE 'web-%' "
      "AND device_id NOT LIKE 'C-%' ";
  if (!search.empty()) {
    sql += "AND device_id LIKE ? ESCAPE '\\' ";
  }
  sql += ";";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return 0;
  }
  if (!search.empty()) {
    std::string pattern = "%" + EscapeLikePattern(search) + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
  }

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

int DeviceDBManager::CountDevicePresence(const std::string& search) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in CountDevicePresence.");
    return 0;
  }

  std::string sql =
      "SELECT COUNT(*) FROM device_presence "
      "WHERE device_id NOT LIKE 'web-%' "
      "AND device_id NOT LIKE 'C-%' ";
  if (!search.empty()) {
    sql += "AND device_id LIKE ? ESCAPE '\\' ";
  }
  sql += ";";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return 0;
  }
  if (!search.empty()) {
    std::string pattern = "%" + EscapeLikePattern(search) + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
  }

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

OnlineDurationStats DeviceDBManager::GetOnlineDurationStats() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  OnlineDurationStats stats;
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in GetOnlineDurationStats.");
    return stats;
  }

  const char* sql =
      "SELECT "
      "COALESCE(SUM(CASE WHEN online = 1 AND online_since > 0 THEN "
      "MAX(0, CAST(strftime('%s','now') AS INTEGER) - online_since) "
      "ELSE 0 END), 0), "
      "COALESCE(SUM(total_online_seconds + "
      "CASE WHEN online = 1 AND online_since > 0 THEN "
      "MAX(0, CAST(strftime('%s','now') AS INTEGER) - online_since) "
      "ELSE 0 END), 0), "
      "COALESCE(SUM(total_control_seconds + COALESCE(("
      "SELECT SUM(MAX(0, CAST(strftime('%s','now') AS INTEGER) - started_at)) "
      "FROM remote_control_sessions "
      "WHERE guest_id = device_presence.device_id), 0)), 0), "
      "COALESCE(SUM(total_controlled_seconds + COALESCE(("
      "SELECT SUM(MAX(0, CAST(strftime('%s','now') AS INTEGER) - started_at)) "
      "FROM remote_control_sessions "
      "WHERE host_id = device_presence.device_id), 0)), 0) "
      "FROM device_presence "
      "WHERE device_id NOT LIKE 'web-%' "
      "AND device_id NOT LIKE 'C-%';";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return stats;
  }

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    stats.current_online_seconds = sqlite3_column_int64(stmt, 0);
    stats.total_online_seconds = sqlite3_column_int64(stmt, 1);
    stats.total_control_seconds = sqlite3_column_int64(stmt, 2);
    stats.total_controlled_seconds = sqlite3_column_int64(stmt, 3);
  }
  sqlite3_finalize(stmt);
  return stats;
}

std::vector<OnlineDeviceInfo> DeviceDBManager::ListOnlineDevices() {
  return ListOnlineDevices(static_cast<size_t>(std::numeric_limits<int>::max()),
                           0, "");
}

std::vector<OnlineDeviceInfo> DeviceDBManager::ListOnlineDevices(
    size_t limit, size_t offset, const std::string& search) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  std::vector<OnlineDeviceInfo> result;
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in ListOnlineDevices.");
    return result;
  }

  std::string sql =
      "SELECT device_id, online, updated_at, online_since, "
      "CASE WHEN online = 1 AND online_since > 0 THEN "
      "MAX(0, CAST(strftime('%s','now') AS INTEGER) - online_since) "
      "ELSE 0 END AS online_duration_seconds, "
      "total_online_seconds + CASE WHEN online = 1 AND online_since > 0 "
      "THEN MAX(0, CAST(strftime('%s','now') AS INTEGER) - online_since) "
      "ELSE 0 END AS total_online_seconds, "
      "total_control_seconds + COALESCE(("
      "SELECT SUM(MAX(0, CAST(strftime('%s','now') AS INTEGER) - started_at)) "
      "FROM remote_control_sessions "
      "WHERE guest_id = device_presence.device_id), 0) "
      "AS total_control_seconds, "
      "total_controlled_seconds + COALESCE(("
      "SELECT SUM(MAX(0, CAST(strftime('%s','now') AS INTEGER) - started_at)) "
      "FROM remote_control_sessions "
      "WHERE host_id = device_presence.device_id), 0) "
      "AS total_controlled_seconds "
      "FROM device_presence "
      "WHERE online = 1 "
      "AND device_id NOT LIKE 'web-%' "
      "AND device_id NOT LIKE 'C-%' ";
  if (!search.empty()) {
    sql += "AND device_id LIKE ? ESCAPE '\\' ";
  }
  sql += "ORDER BY updated_at DESC, device_id ASC LIMIT ? OFFSET ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return result;
  }
  int bind_index = 1;
  if (!search.empty()) {
    std::string pattern = "%" + EscapeLikePattern(search) + "%";
    sqlite3_bind_text(stmt, bind_index++, pattern.c_str(), -1,
                      SQLITE_TRANSIENT);
  }
  sqlite3_bind_int64(stmt, bind_index++, static_cast<sqlite3_int64>(limit));
  sqlite3_bind_int64(stmt, bind_index++, static_cast<sqlite3_int64>(offset));

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    OnlineDeviceInfo info;
    info.device_id = ColumnText(stmt, 0);
    info.online = sqlite3_column_int(stmt, 1) != 0;
    info.updated_at = sqlite3_column_int64(stmt, 2);
    info.online_since = sqlite3_column_int64(stmt, 3);
    info.online_duration_seconds = sqlite3_column_int64(stmt, 4);
    info.total_online_seconds = sqlite3_column_int64(stmt, 5);
    info.total_control_seconds = sqlite3_column_int64(stmt, 6);
    info.total_controlled_seconds = sqlite3_column_int64(stmt, 7);
    result.push_back(info);
  }
  sqlite3_finalize(stmt);

  return result;
}

std::vector<OnlineDeviceInfo> DeviceDBManager::ListDevicePresence(
    size_t limit, size_t offset, const std::string& search) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  std::vector<OnlineDeviceInfo> result;
  if (db_ == nullptr) {
    LOG_ERROR("Database is not initialized in ListDevicePresence.");
    return result;
  }

  std::string sql =
      "SELECT device_id, online, updated_at, online_since, "
      "CASE WHEN online = 1 AND online_since > 0 THEN "
      "MAX(0, CAST(strftime('%s','now') AS INTEGER) - online_since) "
      "ELSE 0 END AS online_duration_seconds, "
      "total_online_seconds + CASE WHEN online = 1 AND online_since > 0 "
      "THEN MAX(0, CAST(strftime('%s','now') AS INTEGER) - online_since) "
      "ELSE 0 END AS total_online_seconds, "
      "total_control_seconds + COALESCE(("
      "SELECT SUM(MAX(0, CAST(strftime('%s','now') AS INTEGER) - started_at)) "
      "FROM remote_control_sessions "
      "WHERE guest_id = device_presence.device_id), 0) "
      "AS total_control_seconds, "
      "total_controlled_seconds + COALESCE(("
      "SELECT SUM(MAX(0, CAST(strftime('%s','now') AS INTEGER) - started_at)) "
      "FROM remote_control_sessions "
      "WHERE host_id = device_presence.device_id), 0) "
      "AS total_controlled_seconds "
      "FROM device_presence "
      "WHERE device_id NOT LIKE 'web-%' "
      "AND device_id NOT LIKE 'C-%' ";
  if (!search.empty()) {
    sql += "AND device_id LIKE ? ESCAPE '\\' ";
  }
  sql += "ORDER BY online DESC, updated_at DESC, device_id ASC "
         "LIMIT ? OFFSET ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return result;
  }
  int bind_index = 1;
  if (!search.empty()) {
    std::string pattern = "%" + EscapeLikePattern(search) + "%";
    sqlite3_bind_text(stmt, bind_index++, pattern.c_str(), -1,
                      SQLITE_TRANSIENT);
  }
  sqlite3_bind_int64(stmt, bind_index++, static_cast<sqlite3_int64>(limit));
  sqlite3_bind_int64(stmt, bind_index++, static_cast<sqlite3_int64>(offset));

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    OnlineDeviceInfo info;
    info.device_id = ColumnText(stmt, 0);
    info.online = sqlite3_column_int(stmt, 1) != 0;
    info.updated_at = sqlite3_column_int64(stmt, 2);
    info.online_since = sqlite3_column_int64(stmt, 3);
    info.online_duration_seconds = sqlite3_column_int64(stmt, 4);
    info.total_online_seconds = sqlite3_column_int64(stmt, 5);
    info.total_control_seconds = sqlite3_column_int64(stmt, 6);
    info.total_controlled_seconds = sqlite3_column_int64(stmt, 7);
    result.push_back(info);
  }
  sqlite3_finalize(stmt);

  return result;
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
