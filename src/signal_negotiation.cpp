#include "signal_negotiation.h"

#include <utility>

#include "log.h"

namespace {

bool GetStringField(const json& j, const char* key, std::string& value) {
  if (!j.contains(key) || !j[key].is_string()) {
    return false;
  }
  value = j[key].get<std::string>();
  return true;
}

}  // namespace

SignalNegotiation::SignalNegotiation(
    std::shared_ptr<TransmissionManager> transmission_manager,
    DeviceDBManager* device_db,
    std::shared_ptr<TurnCredentialIssuer> turn_credential_issuer)
    : transmission_manager_(transmission_manager),
      device_db_manager_(device_db),
      turn_credential_issuer_(std::move(turn_credential_issuer)) {}

SignalNegotiation::~SignalNegotiation() {}

void SignalNegotiation::AddTurnCredentials(
    json& message, const std::string& user_id) const {
  if (!turn_credential_issuer_ || user_id.empty()) {
    return;
  }

  const TurnCredentials credentials = turn_credential_issuer_->Issue(user_id);
  message["turn"] = {{"host", credentials.host},
                     {"port", credentials.port},
                     {"username", credentials.username},
                     {"password", credentials.password},
                     {"expires_at", credentials.expires_at}};
}

bool SignalNegotiation::login_user(websocketpp::connection_hdl hdl,
                                   const json& j) {
  std::string host_id_with_pwd;
  if (!GetStringField(j, "user_id", host_id_with_pwd)) {
    LOG_ERROR("login_user missing or invalid field: user_id");
    return false;
  }

  std::string host_id;
  std::string password;
  std::string return_host_id;

  if (host_id_with_pwd.find("@") != std::string::npos) {
    host_id = host_id_with_pwd.substr(0, host_id_with_pwd.find("@"));
    password = host_id_with_pwd.substr(host_id_with_pwd.find("@") + 1);
  } else {
    host_id = host_id_with_pwd;
    password = "";
  }

  if (host_id.find("C-") == std::string::npos) {
    DeviceCredential dev_cred =
        device_db_manager_->AddDevice(host_id, password);

    std::string ret_host_id = dev_cred.device_id;
    std::string ret_password = dev_cred.password;
    bool update_password = dev_cred.update;

    // Check if AddDevice failed
    if (ret_host_id.empty()) {
      LOG_ERROR("Failed to add device for host_id [{}]", host_id);
      json message = {{"type", "login"},
                      {"user_id", ""},
                      {"status", "fail"},
                      {"reason", "Failed to register device"}};
      send_msg_(hdl, message);
      return true;
    }

    bool update_success = ret_host_id != "" && update_password;
    bool login_success =
        (ret_host_id != "" && ret_password == "") && !update_password;
    bool register_success =
        (ret_host_id != "" && ret_password != "") && (ret_host_id != host_id);

    if (register_success) {
      LOG_INFO("New client, assign id [{}] to it", ret_host_id);
      return_host_id = ret_host_id + "@" + ret_password;
    } else if (login_success) {
      LOG_INFO("Receive login request with id [{}]", ret_host_id);
      return_host_id = ret_host_id;
    } else if (update_success) {
      LOG_INFO("Client [{}] update password", ret_host_id);
      return_host_id = ret_host_id;
    }

    bool success = transmission_manager_->BindUserToWsHandle(ret_host_id, hdl);
    transmission_manager_->BindHostToTransmission(ret_host_id, ret_host_id);

    if (success) {
      json message = {{"type", "login"},
                      {"user_id", return_host_id},
                      {"status", "success"}};
      AddTurnCredentials(message, ret_host_id);
      send_msg_(hdl, message);
    } else {
      json message = {
          {"type", "login"}, {"user_id", return_host_id}, {"status", "fail"}};
      send_msg_(hdl, message);
    }
  } else {
    bool success = transmission_manager_->BindUserToWsHandle(host_id, hdl);
    transmission_manager_->BindHostToTransmission(host_id, host_id);
    LOG_INFO("Receive login request with id [{}]", host_id);

    if (success) {
      json message = {
          {"type", "login"}, {"user_id", host_id}, {"status", "success"}};
      AddTurnCredentials(message, host_id);
      send_msg_(hdl, message);
    } else {
      json message = {
          {"type", "login"}, {"user_id", host_id}, {"status", "fail"}};
      send_msg_(hdl, message);
    }
  }

  return true;
}

bool SignalNegotiation::leave_transmission(websocketpp::connection_hdl hdl,
                                           const json& j) {
  std::string transmission_id;
  std::string user_id;
  if (!GetStringField(j, "transmission_id", transmission_id) ||
      !GetStringField(j, "user_id", user_id)) {
    LOG_ERROR("leave_transmission missing required fields");
    return false;
  }

  LOG_INFO("[{}] leaves transmission [{}]", user_id.c_str(),
           transmission_id.c_str());

  json message = {{"type", "user_leave_transmission"},
                  {"transmission_id", transmission_id},
                  {"user_id", user_id}};

  std::vector<std::string> user_id_list =
      transmission_manager_->GetAllUserIdOfTransmission(transmission_id);

  for (const auto& id : user_id_list) {
    if (id != user_id) {
      send_msg_(transmission_manager_->GetWsHandle(id), message);
    }
  }

  // transmission_manager_->ReleaseUserFromWsHandle(hdl);

  bool is_host =
      transmission_manager_->IsHostOfTransmission(user_id, transmission_id);

  if (is_host) {
    transmission_manager_->ReleaseTransmission(transmission_id);
    LOG_INFO("Release transmission [{}] due to host leaves", transmission_id);
  } else {
    transmission_manager_->ReleaseGuestFromTransmission(user_id);
  }

  return true;
}

bool SignalNegotiation::query_user_id_list(websocketpp::connection_hdl hdl,
                                           const json& j) {
  std::string transmission_id_pwd;
  if (!GetStringField(j, "transmission_id", transmission_id_pwd)) {
    LOG_ERROR("query_user_id_list missing or invalid field: transmission_id");
    return false;
  }

  std::string transmission_id;
  std::string password;

  if (transmission_id_pwd.find("@") != std::string::npos) {
    transmission_id =
        transmission_id_pwd.substr(0, transmission_id_pwd.find("@"));
    password = transmission_id_pwd.substr(transmission_id_pwd.find("@") + 1);
  } else {
    transmission_id = transmission_id_pwd;
    password = "";
  }

  int ret = device_db_manager_->VerifyDevice(transmission_id, password);

  if (0 == ret) {
    std::vector<std::string> user_id_list =
        transmission_manager_->GetAllUserIdOfTransmission(transmission_id);

    json message = {{"type", "user_id_list"},
                    {"transmission_id", transmission_id},
                    {"user_id_list", user_id_list},
                    {"status", "success"}};

    send_msg_(hdl, message);
  } else if (-1 == ret) {
    std::vector<std::string> user_id_list;
    json message = {{"type", "user_id_list"},
                    {"transmission_id", transmission_id},
                    {"user_id_list", user_id_list},
                    {"status", "failed"},
                    {"reason", "Incorrect password"}};

    send_msg_(hdl, message);
  } else if (-2 == ret) {
    std::vector<std::string> user_id_list;
    json message = {{"type", "user_id_list"},
                    {"transmission_id", transmission_id},
                    {"user_id_list", user_id_list},
                    {"status", "failed"},
                    {"reason", "No such transmission id"}};

    send_msg_(hdl, message);
  }

  return true;
}

bool SignalNegotiation::join_transmission(websocketpp::connection_hdl hdl,
                                          const json& j) {
  std::string transmission_id_pwd;
  if (!GetStringField(j, "transmission_id", transmission_id_pwd)) {
    LOG_ERROR("join_transmission missing or invalid field: transmission_id");
    return false;
  }

  std::string transmission_id;
  std::string password;

  if (transmission_id_pwd.find("@") != std::string::npos) {
    transmission_id =
        transmission_id_pwd.substr(0, transmission_id_pwd.find("@"));
    password = transmission_id_pwd.substr(transmission_id_pwd.find("@") + 1);
  } else {
    transmission_id = transmission_id_pwd;
    password = "";
  }

  std::string user_id;
  if (!GetStringField(j, "user_id", user_id)) {
    LOG_ERROR("join_transmission missing or invalid field: user_id");
    return false;
  }

  LOG_INFO("[{}] joins transmission [{}]", user_id.c_str(),
           transmission_id.c_str());

  int ret = device_db_manager_->VerifyDevice(transmission_id, password);

  if (0 == ret) {
    std::string host_id =
        transmission_manager_->GetHostIdOfTransmission(transmission_id);
    websocketpp::connection_hdl host_hdl =
        transmission_manager_->GetWsHandle(host_id);

    if (host_id.empty() || host_hdl.expired()) {
      LOG_WARN("Remote [{}] is unavailable, cannot join transmission",
               transmission_id.c_str());
      json message = {{"type", "user_join_transmission"},
                      {"transmission_id", transmission_id},
                      {"status", "failed"},
                      {"reason", "Remote unavailable"}};
      send_msg_(hdl, message);
      return true;
    }

    transmission_manager_->BindGuestToTransmission(user_id, transmission_id);

    json message = {{"type", "user_join_transmission"},
                    {"transmission_id", transmission_id},
                    {"user_id", user_id},
                    {"status", "success"}};

    AddTurnCredentials(message, host_id);
    send_msg_(host_hdl, message);
  } else if (-1 == ret) {
    LOG_ERROR("Password incorrect for transmission id [{}]",
              transmission_id.c_str());
    json message = {{"type", "user_join_transmission"},
                    {"transmission_id", transmission_id},
                    {"status", "failed"},
                    {"reason", "Incorrect password"}};

    send_msg_(hdl, message);
  } else if (-2 == ret) {
    LOG_ERROR("No such transmission id [{}]", transmission_id.c_str());
    json message = {{"type", "user_join_transmission"},
                    {"transmission_id", transmission_id},
                    {"status", "failed"},
                    {"reason", "No such transmission id"}};

    send_msg_(hdl, message);
  }

  return true;
}

bool SignalNegotiation::offer(websocketpp::connection_hdl hdl, const json& j) {
  std::string transmission_id;
  std::string remote_user_id;
  std::string user_id;
  if (!GetStringField(j, "transmission_id", transmission_id) ||
      !GetStringField(j, "remote_user_id", remote_user_id) ||
      !GetStringField(j, "user_id", user_id)) {
    LOG_ERROR("offer missing required fields");
    return false;
  }

  transmission_manager_->BindGuestToTransmission(user_id, transmission_id);

  websocketpp::connection_hdl destination_hdl =
      transmission_manager_->GetWsHandle(remote_user_id);

  std::string sdp;
  if (GetStringField(j, "sdp", sdp)) {
    json message = {
        {"type", "offer"},
        {"transmission_id", transmission_id},
        {"remote_user_id", user_id},
        {"sdp", sdp},
    };
    AddTurnCredentials(message, remote_user_id);
    LOG_INFO("[{}] send offer to [{}]", user_id, remote_user_id);
    send_msg_(destination_hdl, message);

  } else {
    LOG_ERROR("Invalid offer msg");
  }

  return true;
}

bool SignalNegotiation::answer(websocketpp::connection_hdl hdl, const json& j) {
  std::string transmission_id;
  std::string remote_user_id;
  std::string user_id;
  if (!GetStringField(j, "transmission_id", transmission_id) ||
      !GetStringField(j, "remote_user_id", remote_user_id) ||
      !GetStringField(j, "user_id", user_id)) {
    LOG_ERROR("answer missing required fields");
    return false;
  }

  websocketpp::connection_hdl destination_hdl =
      transmission_manager_->GetWsHandle(remote_user_id);

  std::string sdp;
  if (GetStringField(j, "sdp", sdp)) {
    json message = {{"type", "answer"},
                    {"sdp", sdp},
                    {"remote_user_id", user_id},
                    {"transmission_id", transmission_id}};
    LOG_INFO("[{}] send answer to [{}]", user_id, remote_user_id);
    send_msg_(destination_hdl, message);
  } else {
    LOG_ERROR("Invalid answer msg");
  }

  return true;
}

bool SignalNegotiation::new_candidate(websocketpp::connection_hdl hdl,
                                      const json& j) {
  std::string transmission_id;
  std::string candidate;
  std::string user_id;
  std::string remote_user_id;
  if (!GetStringField(j, "transmission_id", transmission_id) ||
      !GetStringField(j, "sdp", candidate) ||
      !GetStringField(j, "user_id", user_id) ||
      !GetStringField(j, "remote_user_id", remote_user_id)) {
    LOG_ERROR("new_candidate missing required fields");
    return false;
  }

  websocketpp::connection_hdl destination_hdl =
      transmission_manager_->GetWsHandle(remote_user_id);

  // LOG_INFO("send candidate [{}]", candidate.c_str());
  json message = {{"type", "new_candidate"},
                  {"sdp", candidate},
                  {"remote_user_id", user_id},
                  {"transmission_id", transmission_id}};
  send_msg_(destination_hdl, message);

  return true;
}

bool SignalNegotiation::new_candidate_mid(websocketpp::connection_hdl hdl,
                                          const json& j) {
  std::string transmission_id;
  std::string user_id;
  std::string remote_user_id;
  std::string candidate;
  std::string mid;
  if (!GetStringField(j, "transmission_id", transmission_id) ||
      !GetStringField(j, "user_id", user_id) ||
      !GetStringField(j, "remote_user_id", remote_user_id) ||
      !GetStringField(j, "candidate", candidate) ||
      !GetStringField(j, "mid", mid)) {
    LOG_ERROR("new_candidate_mid missing required fields");
    return false;
  }

  websocketpp::connection_hdl destination_hdl =
      transmission_manager_->GetWsHandle(remote_user_id);

  // LOG_INFO("send candidate [{}]", candidate.c_str());
  json message = {{"type", "new_candidate_mid"},
                  {"remote_user_id", user_id},
                  {"transmission_id", transmission_id},
                  {"candidate", candidate},
                  {"mid", mid}};
  send_msg_(destination_hdl, message);

  return true;
}

bool SignalNegotiation::turn_credentials(websocketpp::connection_hdl hdl,
                                         const json& j) {
  (void)j;
  json message = {{"type", "turn_credentials"}};
  const std::string user_id = transmission_manager_->GetUserId(hdl);
  if (user_id.empty()) {
    message["status"] = "fail";
    message["reason"] = "Not authenticated";
  } else if (!turn_credential_issuer_) {
    message["status"] = "fail";
    message["reason"] = "TURN credentials are not configured";
  } else {
    message["status"] = "success";
    AddTurnCredentials(message, user_id);
  }
  send_msg_(hdl, message);
  return true;
}

void SignalNegotiation::OnWebClientDisconnect(const std::string& user_id) {
  // Extract pure user_id (remove password part if exists)
  std::string pure_user_id = user_id;
  size_t at_pos = user_id.find("@");
  if (at_pos != std::string::npos) {
    pure_user_id = user_id.substr(0, at_pos);
  }

  // Check if this is a web client (starts with "web-")
  if (pure_user_id.find("web-") == 0) {
    if (!device_db_manager_->RemoveDevice(pure_user_id)) {
      LOG_WARN("Failed to remove web client device [{}] from database",
               pure_user_id);
    }
  }
}
