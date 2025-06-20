#include "device_db_manager.h"

#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include "log.h"

DeviceDBManager::DeviceDBManager(const std::string& db_path) : db_(nullptr) {
  if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
    LOG_ERROR("Failed to open database, {}", sqlite3_errmsg(db_));
  }
  InitDB();
}

DeviceDBManager::~DeviceDBManager() {
  if (db_) sqlite3_close(db_);
}

void DeviceDBManager::InitDB() {
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

  char* err_msg = nullptr;

  if (sqlite3_exec(db_, sql_devices, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    LOG_ERROR("Failed to create devices table: {}", err_msg);
    sqlite3_free(err_msg);
    return;
  }

  if (sqlite3_exec(db_, sql_seq, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    LOG_ERROR("Failed to create device_id_seq table: {}", err_msg);
    sqlite3_free(err_msg);
    return;
  }

  if (sqlite3_exec(db_, sql_seq_init, nullptr, nullptr, &err_msg) !=
      SQLITE_OK) {
    LOG_ERROR("Failed to initialize device_id_seq: {}", err_msg);
    sqlite3_free(err_msg);
    return;
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

std::string DeviceDBManager::GenerateDeviceId() {
  sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, "SELECT next_id FROM device_id_seq;", -1,
                              &stmt, nullptr);
  if (rc != SQLITE_OK) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return {};
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return {};
  }
  int next_id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  const int MIN_ID = 100000000;
  const int MAX_ID = 999999999;

  std::mt19937 rng(next_id);
  std::uniform_int_distribution<int> dist(MIN_ID, MAX_ID);
  int obfuscated_id = dist(rng);

  rc = sqlite3_prepare_v2(db_, "UPDATE device_id_seq SET next_id = ?;", -1,
                          &stmt, nullptr);
  if (rc != SQLITE_OK) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return {};
  }
  sqlite3_bind_int(stmt, 1, next_id + 1);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return {};
  }

  sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

  char buf[10] = {0};
  snprintf(buf, sizeof(buf), "%09d", obfuscated_id);

  return std::string(buf);
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
  if (!device_id.empty()) {
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
      std::string salt(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
      std::string stored_hash(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
      std::string hash = HashPasswordWithSalt(salt, password);

      sqlite3_finalize(stmt);
      if (stored_hash != hash) {
        // Update password
        const char* update_sql =
            "UPDATE devices SET password_hash = ? WHERE device_id = ?;";
        if (sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr) !=
            SQLITE_OK) {
          LOG_ERROR("Failed to prepare update statement.");
          return {};
        }
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, device_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
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
  for (int i = 0; i < 10; ++i) {
    std::string new_id = GenerateDeviceId();
    std::string new_pwd = GeneratePassword();

    std::string salt = GenerateSalt();
    std::string hash = HashPasswordWithSalt(salt, new_pwd);
    const char* insert_sql =
        "INSERT INTO devices (device_id, password_hash, password_salt) VALUES "
        "(?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
      LOG_ERROR("Failed to prepare insert statement.");
      return {};
    }

    sqlite3_bind_text(stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
      return {new_id, new_pwd, false};
    } else if (rc == SQLITE_CONSTRAINT) {
      LOG_ERROR("{}:{} Insert failed: rc={}, err={}", new_id, new_pwd, rc,
                sqlite3_errmsg(db_));
      continue;
    } else {
      LOG_ERROR("Insert device failed: {}", sqlite3_errmsg(db_));
      return {};
    }
  }

  LOG_ERROR("Failed to generate unique device_id after multiple attempts.");
  return {};
}

int DeviceDBManager::VerifyDevice(const std::string& device_id,
                                  const std::string& password) {
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
    std::string salt(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    std::string stored_hash(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));

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
