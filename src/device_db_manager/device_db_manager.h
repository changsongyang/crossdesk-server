/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_DB_MANAGER_H_
#define _DEVICE_DB_MANAGER_H_

#include <sqlite3.h>

#include <string>

struct DeviceCredential {
  std::string device_id;
  std::string password;
  bool update;
};

class DeviceDBManager {
 public:
  explicit DeviceDBManager(const std::string& db_path);
  ~DeviceDBManager();

  DeviceDBManager(const DeviceDBManager&) = delete;
  DeviceDBManager& operator=(const DeviceDBManager&) = delete;

  DeviceCredential AddDevice(const std::string& device_id,
                             const std::string& password);

  bool UpdatePassword(const std::string& device_id,
                      const std::string& new_password);

  int VerifyDevice(const std::string& device_id, const std::string& password);
  bool RemoveDevice(const std::string& device_id);

 private:
  void InitDB();
  std::string Sha256(const std::string& str);
  std::string GenerateDeviceId();
  std::string GeneratePassword();
  std::string GenerateSalt();

  std::string HashPasswordWithSalt(const std::string& salt,
                                   const std::string& password);

 private:
  sqlite3* db_;
};

#endif  // _DEVICE_DB_MANAGER_H_
