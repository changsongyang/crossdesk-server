#include "transmission_manager.h"

#include <algorithm>
#include <chrono>

#include "log.h"

TransmissionManager::TransmissionManager() {
  ws_hdl_alive_checker_ = std::thread(&TransmissionManager::AliveChecker, this);
}

TransmissionManager::~TransmissionManager() {
  exit_alive_checker_ = true;
  if (ws_hdl_alive_checker_.joinable()) {
    ws_hdl_alive_checker_.join();
  }
}

bool TransmissionManager::IsTransmissionExist(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  return transmission_host_id_list_.count(transmission_id);
}

bool TransmissionManager::ReleaseTransmission(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  auto guest_it = transmission_guest_id_list_.find(transmission_id);
  if (guest_it != transmission_guest_id_list_.end()) {
    active_connection_count_ -= guest_it->second.size();
    transmission_guest_id_list_.erase(guest_it);
  }
  transmission_host_id_list_.erase(transmission_id);
  return true;
}

std::string TransmissionManager::IsHost(const std::string& user_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  for (const auto& pair : transmission_host_id_list_) {
    if (pair.second == user_id) return pair.first;
  }
  return "";
}

std::string TransmissionManager::IsGuest(const std::string& user_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  for (const auto& pair : transmission_guest_id_list_) {
    const auto& list = pair.second;
    if (std::find(list.begin(), list.end(), user_id) != list.end())
      return pair.first;
  }
  return "";
}

bool TransmissionManager::IsHostOfTransmission(
    const std::string& user_id, const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  if (transmission_host_id_list_.count(transmission_id)) {
    return transmission_host_id_list_[transmission_id] == user_id;
  } else {
    LOG_WARN("Transmission [{}] does not exist", transmission_id);
    return false;
  }
}

std::vector<std::string> TransmissionManager::GetAllUserIdOfTransmission(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  std::vector<std::string> result;
  if (transmission_host_id_list_.count(transmission_id)) {
    result.push_back(transmission_host_id_list_[transmission_id]);
  }
  auto& guests = transmission_guest_id_list_[transmission_id];
  result.insert(result.end(), guests.begin(), guests.end());
  return result;
}

std::string TransmissionManager::GetHostIdOfTransmission(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  if (transmission_host_id_list_.count(transmission_id)) {
    return transmission_host_id_list_[transmission_id];
  }

  return "";
}

bool TransmissionManager::BindHostToTransmission(
    const std::string& host_id, const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  if (!transmission_host_id_list_.count(transmission_id)) {
    transmission_host_id_list_[transmission_id] = host_id;
    LOG_INFO("Bind host [{}] to transmission [{}]", host_id, transmission_id);
    return true;
  }
  LOG_WARN("Transmission [{}] already has host", transmission_id);
  return false;
}

bool TransmissionManager::BindGuestToTransmission(
    const std::string& guest_id, const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  auto& guests = transmission_guest_id_list_[transmission_id];
  if (std::find(guests.begin(), guests.end(), guest_id) != guests.end()) {
    return false;
  }
  guests.push_back(guest_id);
  ++active_connection_count_;
  LOG_INFO("Bind guest [{}] to transmission [{}]", guest_id, transmission_id);
  return true;
}

bool TransmissionManager::BindUserToWsHandle(const std::string& user_id,
                                             websocketpp::connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  user_id_ws_hdl_list_[user_id] = hdl;
  return true;
}

bool TransmissionManager::ReleaseGuestFromTransmission(
    const std::string& guest_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  for (auto map_it = transmission_guest_id_list_.begin();
       map_it != transmission_guest_id_list_.end(); ++map_it) {
    auto& list = map_it->second;
    auto it = std::find(list.begin(), list.end(), guest_id);
    if (it != list.end()) {
      list.erase(it);
      --active_connection_count_;
      if (list.empty()) {
        transmission_guest_id_list_.erase(map_it);
      }
      return true;
    }
  }
  return false;
}

std::string TransmissionManager::ReleaseUserSession(
    websocketpp::connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  std::string user_id = ReleaseUserFromWsHandle(hdl);
  if (user_id.empty()) {
    return "";
  }

  std::string transmission_id = IsHost(user_id);
  if (!transmission_id.empty()) {
    LOG_INFO("Host [{}] disconnected, releasing transmission [{}]", user_id,
             transmission_id);
    ReleaseTransmission(transmission_id);
    return user_id;
  }

  if (ReleaseGuestFromTransmission(user_id)) {
    LOG_INFO("Guest [{}] disconnected, releasing it from transmission", user_id);
  }
  return user_id;
}

std::string TransmissionManager::ReleaseUserFromWsHandle(
    websocketpp::connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  for (auto it = user_id_ws_hdl_list_.begin(); it != user_id_ws_hdl_list_.end();
       ++it) {
    if (it->second.lock().get() == hdl.lock().get()) {
      std::string user_id = it->first;
      user_id_ws_hdl_list_.erase(it);
      return user_id;
    }
  }
  return "";
}

void TransmissionManager::RemoveWsHandleLastActiveTime(
    websocketpp::connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  ws_hdl_last_active_time_map_.erase(hdl);
}

websocketpp::connection_hdl TransmissionManager::GetWsHandle(
    const std::string& user_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  auto it = user_id_ws_hdl_list_.find(user_id);
  if (it != user_id_ws_hdl_list_.end()) {
    return it->second;
  }

  return websocketpp::connection_hdl();
}

std::string TransmissionManager::GetUserId(websocketpp::connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  for (const auto& pair : user_id_ws_hdl_list_) {
    if (pair.second.lock().get() == hdl.lock().get()) return pair.first;
  }
  return "";
}

int TransmissionManager::UpdateWsHandleLastActiveTime(
    websocketpp::connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  uint32_t now = static_cast<uint32_t>(
      std::chrono::system_clock::now().time_since_epoch() /
      std::chrono::seconds(1));
  ws_hdl_last_active_time_map_[hdl] = now;
  return 0;
}

size_t TransmissionManager::GetActiveConnectionCount() {
  return active_connection_count_.load();
}

void TransmissionManager::AliveChecker() {
  while (!exit_alive_checker_) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
    uint32_t now = static_cast<uint32_t>(
        std::chrono::system_clock::now().time_since_epoch() /
        std::chrono::seconds(1));

    for (auto it = ws_hdl_last_active_time_map_.begin();
         it != ws_hdl_last_active_time_map_.end();) {
      auto hdl = it->first;
      auto sp = hdl.lock();

      if (!sp) {
        it = ws_hdl_last_active_time_map_.erase(it);
        continue;
      }

      uint32_t last_active = it->second;
      if (now - last_active > 10) {
        LOG_INFO("Inactive websocket [{}] detected", sp.get());

        ReleaseUserSession(hdl);
        it = ws_hdl_last_active_time_map_.erase(it);
      } else {
        ++it;
      }
    }
  }
}
