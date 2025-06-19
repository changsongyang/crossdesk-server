#include "device_db_manager.h"

#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include "log.h"

DeviceDBManager::DeviceDBManager(const std::string& dbPath) : db(nullptr) {
  if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
    LOG_ERROR("Failed to open database: {} with error msg {}", dbPath,
              sqlite3_errmsg(db));
  }
  initDB();
}

DeviceDBManager::~DeviceDBManager() {
  if (db) sqlite3_close(db);
}

void DeviceDBManager::initDB() {
  const char* sql =
      "CREATE TABLE IF NOT EXISTS devices ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "device_id TEXT UNIQUE NOT NULL,"
      "password_hash TEXT NOT NULL);";

  char* errMsg = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    LOG_ERROR("Failed to initialize DB: {}", errMsg);
    sqlite3_free(errMsg);
  }
}

std::string DeviceDBManager::sha256(const std::string& str) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(str.c_str()), str.size(), hash);

  std::stringstream ss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

  return ss.str();
}

std::string DeviceDBManager::generateDeviceId() {
  static std::mt19937 rng(static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  std::uniform_int_distribution<int> dist(0, 9);

  std::string id;
  for (int i = 0; i < 9; ++i) {
    id += '0' + dist(rng);
  }
  return id;
}

std::string DeviceDBManager::addDevice(const std::string& password) {
  std::string hash = sha256(password);

  const int maxTry = 10;
  for (int i = 0; i < maxTry; ++i) {
    std::string deviceId = generateDeviceId();

    const char* sql =
        "INSERT INTO devices (device_id, password_hash) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      LOG_ERROR("Failed to prepare insert statement");
      return "";
    }

    sqlite3_bind_text(stmt, 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
      return deviceId;
    } else if (rc == SQLITE_CONSTRAINT) {
      continue;
    } else {
      LOG_ERROR("Failed to insert device: {}", sqlite3_errmsg(db));
      return "";
    }
  }

  LOG_ERROR("Failed to generate unique device ID after {} attempts.", maxTry);
  return "";
}

bool DeviceDBManager::verifyDevice(const std::string& deviceId,
                                   const std::string& password) {
  std::string hash = sha256(password);
  const char* sql =
      "SELECT COUNT(*) FROM devices WHERE device_id = ? AND password_hash = ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERROR("Failed to prepare verify statement.");
    return false;
  }

  sqlite3_bind_text(stmt, 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);

  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int count = sqlite3_column_int(stmt, 0);
    found = (count > 0);
  }

  sqlite3_finalize(stmt);
  return found;
}

bool DeviceDBManager::removeDevice(const std::string& deviceId) {
  const char* sql = "DELETE FROM devices WHERE device_id = ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERROR("Failed to prepare delete statement.");
    return false;
  }

  sqlite3_bind_text(stmt, 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);

  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}
