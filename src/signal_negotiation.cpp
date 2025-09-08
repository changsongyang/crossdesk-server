#include "signal_negotiation.h"

#include "log.h"

SignalNegotiation::SignalNegotiation(
    std::shared_ptr<TransmissionManager> transmission_manager,
    std::string db_path)
    : transmission_manager_(transmission_manager) {
  device_db_manager_ = std::make_unique<DeviceDBManager>(db_path);
}

SignalNegotiation::~SignalNegotiation() {}

bool SignalNegotiation::login_user(websocketpp::connection_hdl hdl,
                                   const json& j) {
  std::string host_id_with_pwd = j["user_id"].get<std::string>();
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
  std::string transmission_id = j["transmission_id"].get<std::string>();
  std::string user_id = j["user_id"].get<std::string>();
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

  transmission_manager_->ReleaseUserFromWsHandle(hdl);

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
  std::string transmission_id_pwd = j["transmission_id"].get<std::string>();
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

bool SignalNegotiation::offer(websocketpp::connection_hdl hdl, const json& j) {
  std::string transmission_id = j["transmission_id"].get<std::string>();
  std::string remote_user_id = j["remote_user_id"].get<std::string>();
  std::string user_id = j["user_id"].get<std::string>();

  transmission_manager_->BindGuestToTransmission(user_id, transmission_id);

  websocketpp::connection_hdl destination_hdl =
      transmission_manager_->GetWsHandle(remote_user_id);

  if (j.contains("sdp")) {
    std::string sdp = j["sdp"].get<std::string>();
    json message = {
        {"type", "offer"},
        {"transmission_id", transmission_id},
        {"remote_user_id", user_id},
        {"sdp", sdp},
    };
    LOG_INFO("[{}] send offer to [{}]", user_id, remote_user_id);
    send_msg_(destination_hdl, message);

  } else {
    LOG_ERROR("Invalid offer msg");
  }

  return true;
}

bool SignalNegotiation::answer(websocketpp::connection_hdl hdl, const json& j) {
  std::string transmission_id = j["transmission_id"].get<std::string>();
  std::string remote_user_id = j["remote_user_id"].get<std::string>();
  std::string user_id = j["user_id"].get<std::string>();

  websocketpp::connection_hdl destination_hdl =
      transmission_manager_->GetWsHandle(remote_user_id);

  if (j.contains("sdp")) {
    std::string sdp = j["sdp"].get<std::string>();
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
  std::string transmission_id = j["transmission_id"].get<std::string>();
  std::string candidate = j["sdp"].get<std::string>();
  std::string user_id = j["user_id"].get<std::string>();
  std::string remote_user_id = j["remote_user_id"].get<std::string>();

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
