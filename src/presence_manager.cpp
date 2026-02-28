#include "presence_manager.h"

#include <nlohmann/json.hpp>

void PresenceManager::OnLogin(const std::string& user_id,
                              const std::string& device_id,
                              websocketpp::connection_hdl hdl) {
  if (db_) {
    db_->SetDeviceOnline(device_id, true);
  }
  NotifyUserDevices(user_id, device_id, true);
}

void PresenceManager::OnLogout(const std::string& device_id) {
  std::string user_id;
  user_id = device_id;
  if (db_) {
    db_->SetDeviceOnline(device_id, false);
  }
  if (!user_id.empty()) {
    NotifyUserDevices(user_id, device_id, false);
  }
}

bool PresenceManager::IsOnline(const std::string& device_id) const {
  if (!db_) return false;
  auto res = db_->BatchQueryOnline({device_id});
  return !res.empty() && res[0].second;
}

std::vector<std::pair<std::string, bool>> PresenceManager::BatchQuery(
    const std::vector<std::string>& device_ids) const {
  std::vector<std::pair<std::string, bool>> result;
  if (db_) {
    return db_->BatchQueryOnline(device_ids);
  }
  return result;
}

void PresenceManager::NotifyUserDevices(const std::string& user_id,
                                        const std::string& changed_device_id,
                                        bool online) {
  if (!db_ || !send_msg_) return;
  auto devices = db_->GetUserDevices(user_id);
  if (devices.empty()) return;
  auto statuses = db_->BatchQueryOnline(devices);
  std::vector<std::string> targets;
  for (const auto& p : statuses) {
    if (p.first == changed_device_id) continue;
    if (p.second) {
      targets.push_back(p.first);
    }
  }
  nlohmann::json j = {
      {"type", "presence_update"},
      {"id", changed_device_id},
      {"online", online},
  };
  for (auto& id : targets) {
    if (send_to_device_) {
      send_to_device_(id, j);
    }
  }
}

void PresenceManager::UpdateUserDevices(
    const std::string& user_id, const std::vector<std::string>& device_ids) {
  if (!db_) return;
  db_->SetUserDevices(user_id, device_ids);
}
