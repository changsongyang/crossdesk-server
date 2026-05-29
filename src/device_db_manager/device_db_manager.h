/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_DB_MANAGER_H_
#define _DEVICE_DB_MANAGER_H_

#include <sqlite3.h>

#include <mutex>
#include <string>
#include <vector>

struct DeviceCredential {
  std::string device_id;
  std::string password;
  bool update;
};

struct OnlineDeviceInfo {
  std::string device_id;
  bool online = false;
  int64_t updated_at = 0;
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

  bool SetDeviceOnline(const std::string& device_id, bool online);
  int GetOnlineDeviceCount();
  std::vector<OnlineDeviceInfo> ListOnlineDevices();
  std::vector<std::pair<std::string, bool>> BatchQueryOnline(
      const std::vector<std::string>& device_ids);
  bool SetUserDevices(const std::string& user_id,
                      const std::vector<std::string>& device_ids);
  std::vector<std::string> GetUserDevices(const std::string& user_id);

 private:
  void InitDB();
  std::string Sha256(const std::string& str);
  std::string GenerateDeviceId();
  std::string GeneratePassword();
  std::string GenerateSalt();
  bool DeviceIdExists(const std::string& device_id);

  std::string HashPasswordWithSalt(const std::string& salt,
                                   const std::string& password);

 private:
  sqlite3* db_;
  mutable std::recursive_mutex db_mutex_;
};

#endif  // _DEVICE_DB_MANAGER_H_
