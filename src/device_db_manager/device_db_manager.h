/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_DB_MANAGER_H_
#define _DEVICE_DB_MANAGER_H_

#include <sqlite3.h>

#include <string>

class DeviceDBManager {
 public:
  explicit DeviceDBManager(const std::string& dbPath);
  ~DeviceDBManager();

  DeviceDBManager(const DeviceDBManager&) = delete;
  DeviceDBManager& operator=(const DeviceDBManager&) = delete;

 public:
  std::string addDevice(const std::string& password);

  bool verifyDevice(const std::string& deviceId, const std::string& password);
  bool removeDevice(const std::string& deviceId);

 private:
  std::string sha256(const std::string& str);
  void initDB();
  std::string generateDeviceId();

 private:
  sqlite3* db;
};

#endif  // _DEVICE_DB_MANAGER_H_
