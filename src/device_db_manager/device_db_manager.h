/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_DB_MANAGER_H_
#define _DEVICE_DB_MANAGER_H_

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
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
  int64_t online_since = 0;
  int64_t online_duration_seconds = 0;
  int64_t total_online_seconds = 0;
  int64_t total_control_seconds = 0;
  int64_t total_controlled_seconds = 0;
  int64_t current_control_seconds = 0;
  int64_t current_controlled_seconds = 0;
  int64_t active_control_count = 0;
  int64_t active_controlled_count = 0;
  std::vector<std::string> active_control_targets;
  std::vector<std::string> active_controlled_by;
  std::string client_ip;
  std::string country;
  std::string region;
  std::string city;
  std::string location;
};

struct ClientNetworkInfo {
  std::string client_ip;
  std::string country;
  std::string region;
  std::string city;
  std::string location;
};

struct OnlineDurationStats {
  int64_t current_online_seconds = 0;
  int64_t total_online_seconds = 0;
  int64_t total_control_seconds = 0;
  int64_t total_controlled_seconds = 0;
};

struct RemoteControlSessionInfo {
  std::string transmission_id;
  std::string host_id;
  std::vector<std::string> guest_ids;
  int64_t started_at = 0;
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
  bool UpdateDeviceNetworkInfo(const std::string& device_id,
                               const ClientNetworkInfo& network_info);
  bool RecordRuntimeHeartbeat();
  bool StartRemoteControlSession(const std::string& transmission_id,
                                 const std::string& host_id,
                                 const std::string& guest_id);
  bool EndRemoteControlSession(const std::string& transmission_id,
                               const std::string& host_id,
                               const std::string& guest_id);
  bool EndRemoteControlTransmission(const std::string& transmission_id);
  int CountActiveRemoteControlConnections();
  int CountRemoteControlTransmissions(const std::string& search = "");
  std::vector<RemoteControlSessionInfo> ListRemoteControlSessions(
      size_t limit, size_t offset, const std::string& search = "");
  int GetOnlineDeviceCount();
  int CountOnlineDevices(const std::string& search = "");
  int CountDevicePresence(const std::string& search = "",
                          const std::string& filter = "all");
  OnlineDurationStats GetOnlineDurationStats();
  std::vector<OnlineDeviceInfo> ListOnlineDevices();
  std::vector<OnlineDeviceInfo> ListOnlineDevices(
      size_t limit, size_t offset, const std::string& search);
  std::vector<OnlineDeviceInfo> ListDevicePresence(
      size_t limit, size_t offset, const std::string& search,
      const std::string& filter = "all",
      const std::string& sort = "status",
      const std::string& order = "desc");
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
  int64_t GetRuntimeLastSeen();

  std::string HashPasswordWithSalt(const std::string& salt,
                                   const std::string& password);

 private:
  sqlite3* db_;
  mutable std::recursive_mutex db_mutex_;
};

#endif  // _DEVICE_DB_MANAGER_H_
