#include "signal_server.h"

#include "common.h"
#include "log.h"

SignalServer::SignalServer() {
  // Set logging settings
  server_.set_error_channels(websocketpp::log::elevel::all);
  server_.set_access_channels(websocketpp::log::alevel::none);

  // Initialize Asio
  server_.init_asio();

  server_.set_open_handler(
      std::bind(&SignalServer::on_open, this, std::placeholders::_1));

  server_.set_close_handler(
      std::bind(&SignalServer::on_close, this, std::placeholders::_1));

  server_.set_fail_handler(
      std::bind(&SignalServer::on_fail, this, std::placeholders::_1));

  server_.set_message_handler(std::bind(&SignalServer::on_message, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));

  server_.set_ping_handler(bind(&SignalServer::on_ping, this,
                                std::placeholders::_1, std::placeholders::_2));

  server_.set_pong_handler(bind(&SignalServer::on_pong, this,
                                std::placeholders::_1, std::placeholders::_2));
}

SignalServer::~SignalServer() {}

bool SignalServer::on_open(websocketpp::connection_hdl hdl) {
  ws_connections_[hdl] = ws_connection_id_++;

  device_db_manager_ = std::make_unique<DeviceDBManager>("devices.db");
  return true;
}

bool SignalServer::on_close(websocketpp::connection_hdl hdl) {
  std::string user_id = transmission_manager_.ReleaseUserFromeWsHandle(hdl);
  if (!user_id.empty()) {
    LOG_INFO("Websocket connection [{}|{}] closed", ws_connections_[hdl],
             user_id);

    // check user is host or not
    std::string transmission_id_host = transmission_manager_.IsHost(user_id);
    if (!transmission_id_host.empty()) {
      transmission_manager_.ReleaseTransmission(transmission_id_host);
      LOG_INFO("Release transmission [{}] due to host [{}] leaves",
               transmission_id_host, user_id);

      // notify all users in transmission
      json message = {{"type", "user_leave_transmission"},
                      {"transmission_id", transmission_id_host},
                      {"user_id", user_id}};

      std::vector<std::string> user_id_list =
          transmission_manager_.GetAllUserIdOfTransmission(
              transmission_id_host);

      for (const auto& user_id : user_id_list) {
        send_msg(transmission_manager_.GetWsHandle(user_id), message);
      }
    }

    // check user is guest or not
    std::string transmission_id_guest = transmission_manager_.IsGuest(user_id);
    if (!transmission_id_guest.empty()) {
      transmission_manager_.ReleaseGuestFromTransmission(user_id);
      LOG_INFO("Release guest [{}] from transmission [{}]", user_id,
               transmission_id_guest);

      // notify all users in transmission
      json message = {{"type", "user_leave_transmission"},
                      {"transmission_id", transmission_id_guest},
                      {"user_id", user_id}};

      std::vector<std::string> user_id_list =
          transmission_manager_.GetAllUserIdOfTransmission(
              transmission_id_guest);

      for (const auto& user_id : user_id_list) {
        send_msg(transmission_manager_.GetWsHandle(user_id), message);
      }
    }
  }

  ws_connections_.erase(hdl);

  return true;
}

bool SignalServer::on_fail(websocketpp::connection_hdl hdl) {
  std::string user_id = transmission_manager_.GetUserId(hdl);
  if (!user_id.empty()) {
    LOG_INFO("Websocket connection [{}|{}] failed", ws_connections_[hdl],
             user_id);
  }
  return true;
}

bool SignalServer::on_ping(websocketpp::connection_hdl hdl, std::string s) {
  transmission_manager_.UpdateWsHandleLastActiveTime(hdl);
  return true;
}

bool SignalServer::on_pong(websocketpp::connection_hdl hdl, std::string s) {
  return true;
}

void SignalServer::run(uint16_t port) {
  LOG_INFO("Signal server runs on port [{}]", port);

  server_.set_reuse_addr(true);
  server_.listen(port);

  // Queues a connection accept operation
  server_.start_accept();

  // Start the Asio io_service run loop
  server_.run();
}

void SignalServer::send_msg(websocketpp::connection_hdl hdl, json message) {
  if (!hdl.expired()) {
    server_.send(hdl, message.dump(), websocketpp::frame::opcode::text);
  } else {
    LOG_ERROR("Destination hdl invalid");
  }
}

void SignalServer::on_message(websocketpp::connection_hdl hdl,
                              server::message_ptr msg) {
  transmission_manager_.UpdateWsHandleLastActiveTime(hdl);
  std::string payload = msg->get_payload();

  auto j = json::parse(payload);
  std::string type = j["type"].get<std::string>();

  switch (HASH_STRING_PIECE(type.c_str())) {
    case "login"_H: {
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
        bool register_success = (ret_host_id != "" && ret_password != "") &&
                                (ret_host_id != host_id);

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

        bool success =
            transmission_manager_.BindUserToWsHandle(ret_host_id, hdl);
        transmission_manager_.BindHostToTransmission(ret_host_id, ret_host_id);

        if (success) {
          json message = {{"type", "login"},
                          {"user_id", return_host_id},
                          {"status", "success"}};
          send_msg(hdl, message);
        } else {
          json message = {{"type", "login"},
                          {"user_id", return_host_id},
                          {"status", "fail"}};
          send_msg(hdl, message);
        }
      } else {
        bool success = transmission_manager_.BindUserToWsHandle(host_id, hdl);
        transmission_manager_.BindHostToTransmission(host_id, host_id);
        LOG_INFO("Receive login request with id [{}]", host_id);

        if (success) {
          json message = {
              {"type", "login"}, {"user_id", host_id}, {"status", "success"}};
          send_msg(hdl, message);
        } else {
          json message = {
              {"type", "login"}, {"user_id", host_id}, {"status", "fail"}};
          send_msg(hdl, message);
        }
      }

      break;
    }
    case "leave_transmission"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string user_id = j["user_id"].get<std::string>();
      LOG_INFO("[{}] leaves transmission [{}]", user_id.c_str(),
               transmission_id.c_str());

      json message = {{"type", "user_leave_transmission"},
                      {"transmission_id", transmission_id},
                      {"user_id", user_id}};

      std::vector<std::string> user_id_list =
          transmission_manager_.GetAllUserIdOfTransmission(transmission_id);

      for (const auto& user_id : user_id_list) {
        send_msg(transmission_manager_.GetWsHandle(user_id), message);
      }

      bool is_host =
          transmission_manager_.IsHostOfTransmission(user_id, transmission_id);

      if (is_host) {
        transmission_manager_.ReleaseTransmission(transmission_id);
        LOG_INFO("Release transmission [{}] due to host leaves",
                 transmission_id);
      } else {
        transmission_manager_.ReleaseGuestFromTransmission(user_id);
      }

      break;
    }
    case "query_user_id_list"_H: {
      std::string transmission_id_pwd = j["transmission_id"].get<std::string>();
      std::string transmission_id;
      std::string password;

      if (transmission_id_pwd.find("@") != std::string::npos) {
        transmission_id =
            transmission_id_pwd.substr(0, transmission_id_pwd.find("@"));
        password =
            transmission_id_pwd.substr(transmission_id_pwd.find("@") + 1);
      } else {
        transmission_id = transmission_id_pwd;
        password = "";
      }

      int ret = device_db_manager_->VerifyDevice(transmission_id, password);

      if (0 == ret) {
        std::vector<std::string> user_id_list =
            transmission_manager_.GetAllUserIdOfTransmission(transmission_id);

        json message = {{"type", "user_id_list"},
                        {"transmission_id", transmission_id},
                        {"user_id_list", user_id_list},
                        {"status", "success"}};

        send_msg(hdl, message);
      } else if (-1 == ret) {
        std::vector<std::string> user_id_list;
        json message = {{"type", "user_id_list"},
                        {"transmission_id", transmission_id},
                        {"user_id_list", user_id_list},
                        {"status", "failed"},
                        {"reason", "Incorrect password"}};

        send_msg(hdl, message);
      } else if (-2 == ret) {
        std::vector<std::string> user_id_list;
        json message = {{"type", "user_id_list"},
                        {"transmission_id", transmission_id},
                        {"user_id_list", user_id_list},
                        {"status", "failed"},
                        {"reason", "No such transmission id"}};

        send_msg(hdl, message);
      }

      // LOG_INFO("Send member_list: [{}]", message.dump());

      break;
    }
    case "offer"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();
      std::string user_id = j["user_id"].get<std::string>();

      transmission_manager_.BindGuestToTransmission(user_id, transmission_id);

      websocketpp::connection_hdl destination_hdl =
          transmission_manager_.GetWsHandle(remote_user_id);

      if (j.contains("sdp")) {
        std::string sdp = j["sdp"].get<std::string>();
        json message = {
            {"type", "offer"},
            {"transmission_id", transmission_id},
            {"remote_user_id", user_id},
            {"sdp", sdp},
        };
        LOG_INFO("[{}] send offer to [{}]", user_id, remote_user_id);
        send_msg(destination_hdl, message);

      } else {
        LOG_ERROR("Invalid offer msg");
      }

      break;
    }
    case "answer"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();
      std::string user_id = j["user_id"].get<std::string>();

      websocketpp::connection_hdl destination_hdl =
          transmission_manager_.GetWsHandle(remote_user_id);

      if (j.contains("sdp")) {
        std::string sdp = j["sdp"].get<std::string>();
        json message = {{"type", "answer"},
                        {"sdp", sdp},
                        {"remote_user_id", user_id},
                        {"transmission_id", transmission_id}};
        LOG_INFO("[{}] send answer to [{}]", user_id, remote_user_id);
        send_msg(destination_hdl, message);
      } else {
        LOG_ERROR("Invalid answer msg");
      }

      break;
    }
    case "new_candidate"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string candidate = j["sdp"].get<std::string>();
      std::string user_id = j["user_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();

      websocketpp::connection_hdl destination_hdl =
          transmission_manager_.GetWsHandle(remote_user_id);

      // LOG_INFO("send candidate [{}]", candidate.c_str());
      json message = {{"type", "new_candidate"},
                      {"sdp", candidate},
                      {"remote_user_id", user_id},
                      {"transmission_id", transmission_id}};
      send_msg(destination_hdl, message);
      break;
    }
    default:
      break;
  }

  // std::string sdp = j["sdp"];

  // LOG_INFO("Message type: {}", type);
  // LOG_INFO("Message body: {}", sdp);

  // server_.send(hdl, msg->get_payload(), msg->get_opcode());
}