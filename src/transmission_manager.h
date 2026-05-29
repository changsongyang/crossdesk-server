/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-26
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _TRANSMISSION_MANAGER_H_
#define _TRANSMISSION_MANAGER_H_

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <websocketpp/server.hpp>

class TransmissionManager {
 public:
  TransmissionManager();
  ~TransmissionManager();

  bool IsTransmissionExist(const std::string& transmission_id);
  bool ReleaseTransmission(const std::string& transmission_id);

  std::string IsHost(const std::string& user_id);
  std::string IsGuest(const std::string& user_id);
  bool IsHostOfTransmission(const std::string& user_id,
                            const std::string& transmission_id);

  std::vector<std::string> GetAllUserIdOfTransmission(
      const std::string& transmission_id);

  std::string GetHostIdOfTransmission(const std::string& transmission_id);

  bool BindHostToTransmission(const std::string& host_id,
                              const std::string& transmission_id);
  bool BindGuestToTransmission(const std::string& guest_id,
                               const std::string& transmission_id);
  bool BindUserToWsHandle(const std::string& user_id,
                          websocketpp::connection_hdl hdl);

  bool ReleaseGuestFromTransmission(const std::string& guest_id);
  std::string ReleaseUserSession(websocketpp::connection_hdl hdl);
  std::string ReleaseUserFromWsHandle(websocketpp::connection_hdl hdl);
  void RemoveWsHandleLastActiveTime(websocketpp::connection_hdl hdl);

  websocketpp::connection_hdl GetWsHandle(const std::string& user_id);
  std::string GetUserId(websocketpp::connection_hdl hdl);

  int UpdateWsHandleLastActiveTime(websocketpp::connection_hdl hdl);
  size_t GetActiveConnectionCount();

 private:
  void AliveChecker();

 private:
  std::map<std::string, std::string> transmission_host_id_list_;
  std::map<std::string, std::vector<std::string>> transmission_guest_id_list_;
  std::map<std::string, websocketpp::connection_hdl> user_id_ws_hdl_list_;
  std::map<websocketpp::connection_hdl, uint32_t,
           std::owner_less<websocketpp::connection_hdl>>
      ws_hdl_last_active_time_map_;

  std::thread ws_hdl_alive_checker_;
  std::recursive_mutex ws_hdl_alive_checker_mutex_;
  std::atomic<bool> exit_alive_checker_{false};
  std::atomic<size_t> active_connection_count_{0};
};

#endif