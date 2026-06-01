#include "transmission_manager.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>

#include "log.h"

namespace {

bool HasConnectionOwner(websocketpp::connection_hdl hdl) {
  static const websocketpp::connection_hdl empty_hdl;
  std::owner_less<websocketpp::connection_hdl> less;
  return less(hdl, empty_hdl) || less(empty_hdl, hdl);
}

bool SameConnection(websocketpp::connection_hdl lhs,
                    websocketpp::connection_hdl rhs) {
  if (!HasConnectionOwner(lhs) || !HasConnectionOwner(rhs)) {
    return false;
  }

  std::owner_less<websocketpp::connection_hdl> less;
  return !less(lhs, rhs) && !less(rhs, lhs);
}

void SubtractActiveConnectionCount(std::atomic<size_t>& count,
                                   size_t amount) {
  size_t current = count.load();
  count.store(amount > current ? 0 : current - amount);
}

bool ContainsText(const std::string& value, const std::string& search) {
  return value.find(search) != std::string::npos;
}

bool TransmissionMatchesSearch(const std::string& transmission_id,
                               const std::string& host_id,
                               const std::vector<std::string>* guest_ids,
                               const std::string& search) {
  if (search.empty()) {
    return true;
  }
  if (ContainsText(transmission_id, search) || ContainsText(host_id, search)) {
    return true;
  }
  if (!guest_ids) {
    return false;
  }
  return std::any_of(guest_ids->begin(), guest_ids->end(),
                     [&search](const std::string& guest_id) {
                       return ContainsText(guest_id, search);
                     });
}

}  // namespace

TransmissionManager::TransmissionManager() {
  ws_hdl_alive_checker_ = std::thread(&TransmissionManager::AliveChecker, this);
}

TransmissionManager::~TransmissionManager() {
  exit_alive_checker_ = true;
  ws_hdl_alive_checker_cv_.notify_all();
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
    SubtractActiveConnectionCount(active_connection_count_,
                                  guest_it->second.size());
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
  auto host_it = transmission_host_id_list_.find(transmission_id);
  if (host_it == transmission_host_id_list_.end()) {
    LOG_WARN("Transmission [{}] does not exist", transmission_id);
    return false;
  }
  return host_it->second == user_id;
}

std::vector<std::string> TransmissionManager::GetAllUserIdOfTransmission(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  std::vector<std::string> result;
  auto host_it = transmission_host_id_list_.find(transmission_id);
  if (host_it != transmission_host_id_list_.end()) {
    result.push_back(host_it->second);
  }
  auto guest_it = transmission_guest_id_list_.find(transmission_id);
  if (guest_it != transmission_guest_id_list_.end()) {
    result.insert(result.end(), guest_it->second.begin(),
                  guest_it->second.end());
  }
  return result;
}

std::vector<TransmissionSnapshot> TransmissionManager::GetTransmissionSnapshots() {
  size_t ignored_count = 0;
  return GetTransmissionSnapshots(std::numeric_limits<size_t>::max(), 0, "",
                                  &ignored_count);
}

std::vector<TransmissionSnapshot> TransmissionManager::GetTransmissionSnapshots(
    size_t limit, size_t offset, const std::string& search,
    size_t* filtered_count) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  std::vector<TransmissionSnapshot> result;
  if (limit > 0) {
    result.reserve(std::min(limit, transmission_host_id_list_.size()));
  }

  size_t matched_count = 0;
  for (const auto& host_pair : transmission_host_id_list_) {
    auto guest_it = transmission_guest_id_list_.find(host_pair.first);
    const std::vector<std::string>* guest_ids =
        guest_it != transmission_guest_id_list_.end() ? &guest_it->second
                                                      : nullptr;
    if (!TransmissionMatchesSearch(host_pair.first, host_pair.second,
                                   guest_ids, search)) {
      continue;
    }
    if (matched_count++ < offset) {
      continue;
    }
    if (result.size() >= limit) {
      continue;
    }

    TransmissionSnapshot snapshot;
    snapshot.transmission_id = host_pair.first;
    snapshot.host_id = host_pair.second;
    if (guest_ids) {
      snapshot.guest_ids = guest_it->second;
    }
    snapshot.participant_count = 1 + snapshot.guest_ids.size();
    snapshot.active = true;
    result.push_back(snapshot);
  }

  if (filtered_count) {
    *filtered_count = matched_count;
  }
  return result;
}

std::string TransmissionManager::GetHostIdOfTransmission(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  auto host_it = transmission_host_id_list_.find(transmission_id);
  if (host_it != transmission_host_id_list_.end()) {
    return host_it->second;
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
  auto host_it = transmission_host_id_list_.find(transmission_id);
  if (host_it != transmission_host_id_list_.end() &&
      host_it->second == guest_id) {
    return false;
  }

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
      SubtractActiveConnectionCount(active_connection_count_, 1);
      if (list.empty()) {
        transmission_guest_id_list_.erase(map_it);
      }
      return true;
    }
  }
  return false;
}

bool TransmissionManager::DisconnectTransmission(
    const std::string& transmission_id) {
  std::lock_guard<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
  if (!IsTransmissionExist(transmission_id)) {
    return true;
  }
  return ReleaseTransmission(transmission_id);
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
    if (SameConnection(it->second, hdl)) {
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
    if (SameConnection(pair.second, hdl)) return pair.first;
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
    std::unique_lock<std::recursive_mutex> lock(ws_hdl_alive_checker_mutex_);
    if (ws_hdl_alive_checker_cv_.wait_for(
            lock, std::chrono::seconds(10),
            [this]() { return exit_alive_checker_.load(); })) {
      break;
    }

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
