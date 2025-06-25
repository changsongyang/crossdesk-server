#include "signal_negotiation.h"

#include "log.h"

SignalNegotiation::SignalNegotiation() {
  device_db_manager_ = std::make_unique<DeviceDBManager>("devices.db");
}

SignalNegotiation::~SignalNegotiation() {}

bool SignalNegotiation::login_user(websocketpp::connection_hdl hdl,
                                   const json& j) {
  std::string host_id_with_pwd = j["user_id"].get<std::string>();
  std::string host_id;
  std::string password;

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

    bool register_success =
        (ret_host_id != "" && ret_password != "") && (ret_host_id != host_id);

    if (register_success) {
      LOG_INFO("New client, assign id [{}] to it", ret_host_id);
      // send message
    }
  }

  // Bind user to ws handle logic
  return true;
}

bool SignalNegotiation::leave_transmission(websocketpp::connection_hdl hdl,
                                           const json& j) {
  std::string transmission_id = j["transmission_id"].get<std::string>();
  std::string user_id = j["user_id"].get<std::string>();
  LOG_INFO("[{}] leaves transmission [{}]", user_id.c_str(),
           transmission_id.c_str());

  // Handle transmission leave logic
  return true;
}

bool SignalNegotiation::query_user_id_list(websocketpp::connection_hdl hdl,
                                           const json& j) {
  std::string transmission_id = j["transmission_id"].get<std::string>();
  int ret = device_db_manager_->VerifyDevice(transmission_id,
                                             j["password"].get<std::string>());

  if (ret == 0) {
    std::vector<std::string> user_id_list;
    json message = {{"type", "user_id_list"},
                    {"transmission_id", transmission_id},
                    {"user_id_list", user_id_list},
                    {"status", "success"}};
    // send message
  } else {
    json message = {{"type", "user_id_list"},
                    {"transmission_id", transmission_id},
                    {"status", "failed"},
                    {"reason", "Incorrect password or transmission not found"}};
    // send message
  }
  return true;
}

bool SignalNegotiation::offer(websocketpp::connection_hdl hdl, const json& j) {
  std::string transmission_id = j["transmission_id"].get<std::string>();
  std::string user_id = j["user_id"].get<std::string>();
  std::string remote_user_id = j["remote_user_id"].get<std::string>();

  json message = {{"type", "offer"},
                  {"transmission_id", transmission_id},
                  {"remote_user_id", user_id},
                  {"sdp", j["sdp"].get<std::string>()}};
  // send offer message
  return true;
}

bool SignalNegotiation::answer(websocketpp::connection_hdl hdl, const json& j) {
  json message = {{"type", "answer"},
                  {"sdp", j["sdp"].get<std::string>()},
                  {"transmission_id", j["transmission_id"].get<std::string>()}};
  // send answer message
  return true;
}

bool SignalNegotiation::new_candidate(websocketpp::connection_hdl hdl,
                                      const json& j) {
  json message = {{"type", "new_candidate"},
                  {"sdp", j["sdp"].get<std::string>()},
                  {"remote_user_id", j["remote_user_id"].get<std::string>()}};
  // send candidate message
  return true;
}
