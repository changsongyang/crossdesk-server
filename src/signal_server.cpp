#include "signal_server.h"

#include <filesystem>
#include <fstream>

#include "common.h"
#include "log.h"
#include "signal_negotiation.h"

SignalServer::SignalServer() {
  server_.set_error_channels(websocketpp::log::elevel::none);
  server_.set_access_channels(websocketpp::log::alevel::none);
  server_.init_asio();

  server_.set_open_handler(
      std::bind(&SignalServer::OnOpen, this, std::placeholders::_1));
  server_.set_close_handler(
      std::bind(&SignalServer::OnClose, this, std::placeholders::_1));
  server_.set_fail_handler(
      std::bind(&SignalServer::OnFail, this, std::placeholders::_1));
  server_.set_message_handler(std::bind(&SignalServer::OnMessage, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
  server_.set_tls_init_handler(
      std::bind(&SignalServer::OnTlsInit, this, std::placeholders::_1));
  server_.set_ping_handler(std::bind(&SignalServer::OnPing, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));
  server_.set_pong_handler(std::bind(&SignalServer::OnPong, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));

  transmission_manager_ = std::make_shared<TransmissionManager>();
  device_db_manager_ = std::make_unique<DeviceDBManager>(db_path_);
  signal_negotiation_ = std::make_unique<SignalNegotiation>(
      transmission_manager_, device_db_manager_.get());
  signal_negotiation_->SetSendMsgCallback(std::bind(&SignalServer::SendMsg,
                                                    this, std::placeholders::_1,
                                                    std::placeholders::_2));
  presence_manager_ = std::make_unique<PresenceManager>();
  presence_manager_->SetSendMsgCallback(std::bind(&SignalServer::SendMsg, this,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2));
  presence_manager_->SetDeviceDB(device_db_manager_.get());
  presence_manager_->SetSendToDeviceCallback(
      [this](const std::string& id, json msg) {
        SendMsg(transmission_manager_->GetWsHandle(id), msg);
      });
}

SignalServer::SignalServer(uint16_t port, std::string certs_dir,
                           std::string db_path)
    : port_(port), certs_dir_(certs_dir), db_path_(db_path) {
  LOG_INFO(
      "Starting CrossDesk Signaling Server on port {}, certs_dir: {}, "
      "db_path: {}",
      port_, certs_dir_, db_path_);

  server_.set_error_channels(websocketpp::log::elevel::none);
  server_.set_access_channels(websocketpp::log::alevel::none);
  server_.init_asio();

  server_.set_open_handler(
      std::bind(&SignalServer::OnOpen, this, std::placeholders::_1));
  server_.set_close_handler(
      std::bind(&SignalServer::OnClose, this, std::placeholders::_1));
  server_.set_fail_handler(
      std::bind(&SignalServer::OnFail, this, std::placeholders::_1));
  server_.set_message_handler(std::bind(&SignalServer::OnMessage, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
  server_.set_tls_init_handler(
      std::bind(&SignalServer::OnTlsInit, this, std::placeholders::_1));
  server_.set_ping_handler(std::bind(&SignalServer::OnPing, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));
  server_.set_pong_handler(std::bind(&SignalServer::OnPong, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));

  transmission_manager_ = std::make_shared<TransmissionManager>();
  device_db_manager_ = std::make_unique<DeviceDBManager>(db_path_);
  signal_negotiation_ = std::make_unique<SignalNegotiation>(
      transmission_manager_, device_db_manager_.get());
  signal_negotiation_->SetSendMsgCallback(std::bind(&SignalServer::SendMsg,
                                                    this, std::placeholders::_1,
                                                    std::placeholders::_2));
  presence_manager_ = std::make_unique<PresenceManager>();
  presence_manager_->SetSendMsgCallback(std::bind(&SignalServer::SendMsg, this,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2));
  presence_manager_->SetDeviceDB(device_db_manager_.get());
  presence_manager_->SetSendToDeviceCallback(
      [this](const std::string& id, json msg) {
        SendMsg(transmission_manager_->GetWsHandle(id), msg);
      });
}

SignalServer::~SignalServer() {}

bool SignalServer::OnOpen(websocketpp::connection_hdl hdl) {
  ws_connections_[hdl] = ws_connection_id_++;
  return true;
}

bool SignalServer::OnClose(websocketpp::connection_hdl hdl) {
  std::string user_id = transmission_manager_->ReleaseUserFromWsHandle(hdl);
  auto conn_it = ws_connections_.find(hdl);
  connection_id conn_id =
      (conn_it != ws_connections_.end()) ? conn_it->second : 0;
  if (!user_id.empty()) {
    LOG_INFO("Websocket connection [{}|{}] closed", conn_id, user_id);
    // Remove web client from database on disconnect
    if (signal_negotiation_) {
      signal_negotiation_->OnWebClientDisconnect(user_id);
    }
  }
  if (presence_manager_ && !user_id.empty()) {
    presence_manager_->OnLogout(user_id);
  }
  ws_connections_.erase(hdl);
  return true;
}

bool SignalServer::OnFail(websocketpp::connection_hdl hdl) {
  std::string user_id = transmission_manager_->ReleaseUserFromWsHandle(hdl);
  auto conn_it = ws_connections_.find(hdl);
  connection_id conn_id =
      (conn_it != ws_connections_.end()) ? conn_it->second : 0;
  if (!user_id.empty()) {
    LOG_INFO("Websocket connection [{}|{}] failed", conn_id, user_id);
    // Remove web client from database on disconnect
    if (signal_negotiation_) {
      signal_negotiation_->OnWebClientDisconnect(user_id);
    }
  }
  if (presence_manager_ && !user_id.empty()) {
    presence_manager_->OnLogout(user_id);
  }
  ws_connections_.erase(hdl);
  return true;
}

context_ptr SignalServer::OnTlsInit(websocketpp::connection_hdl hdl) {
  namespace asio = websocketpp::lib::asio;
  context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(
      asio::ssl::context::sslv23);

  try {
    ctx->set_options(
        asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 | asio::ssl::context::single_dh_use);

    std::string cert_file = certs_dir_ + "/api.crossdesk.cn_bundle.crt";
    std::string key_file = certs_dir_ + "/api.crossdesk.cn.key";

    // Check if certificate files exist
    if (!std::filesystem::exists(cert_file)) {
      LOG_ERROR("Certificate file not found: {}", cert_file);
      throw std::runtime_error("Certificate file not found: " + cert_file);
    }
    if (!std::filesystem::exists(key_file)) {
      LOG_ERROR("Private key file not found: {}", key_file);
      throw std::runtime_error("Private key file not found: " + key_file);
    }

    ctx->use_certificate_chain_file(cert_file);
    ctx->use_private_key_file(key_file, asio::ssl::context::pem);

    SSL_CTX_set_cipher_list(ctx->native_handle(),
                            "ECDHE-ECDSA-AES256-GCM-SHA384:"
                            "ECDHE-RSA-AES256-GCM-SHA384:"
                            "ECDHE-ECDSA-AES128-GCM-SHA256:"
                            "ECDHE-RSA-AES128-GCM-SHA256");
  } catch (std::exception& e) {
    LOG_ERROR("Failed to initialize TLS context: {}", e.what());
    throw;  // Re-throw to prevent invalid context from being used
  }
  return ctx;
}

bool SignalServer::OnPing(websocketpp::connection_hdl hdl, std::string s) {
  return true;
}

bool SignalServer::OnPong(websocketpp::connection_hdl hdl, std::string s) {
  transmission_manager_->UpdateWsHandleLastActiveTime(hdl);
  return true;
}

void SignalServer::Run() {
  if (!std::filesystem::exists(certs_dir_)) {
    LOG_ERROR("Certs dir [{}] not exist", certs_dir_);
    return;
  }

  // Verify certificate files exist
  std::string cert_file = certs_dir_ + "/api.crossdesk.cn_bundle.crt";
  std::string key_file = certs_dir_ + "/api.crossdesk.cn.key";
  if (!std::filesystem::exists(cert_file)) {
    LOG_ERROR("Certificate file not found: {}", cert_file);
    return;
  }
  if (!std::filesystem::exists(key_file)) {
    LOG_ERROR("Private key file not found: {}", key_file);
    return;
  }

  server_.set_reuse_addr(true);
  LOG_INFO("Signal server starting on port [{}]", port_);
  LOG_INFO("Certificate directory: [{}]", certs_dir_);
  LOG_INFO("Database path: [{}]", db_path_);

  try {
    // Listen on all interfaces (0.0.0.0)
    namespace asio = websocketpp::lib::asio;
    asio::error_code ec;
    server_.listen(asio::ip::tcp::v4(), port_, ec);
    if (ec) {
      LOG_ERROR("Failed to listen on port {}: {}", port_, ec.message());
      return;
    }
    LOG_INFO("Successfully bound to port [{}]", port_);

    server_.start_accept(ec);
    if (ec) {
      LOG_ERROR("Failed to start accepting connections: {}", ec.message());
      return;
    }
    LOG_INFO("Signal server listening on port [{}], waiting for connections...",
             port_);

    server_.run();
    LOG_INFO("Server run() returned");
  } catch (std::exception& e) {
    LOG_ERROR("Server error: {}, attempting to restart...", e.what());
    // Try to restart the server
    try {
      server_.stop();
      server_.listen(port_);
      server_.start_accept();
      server_.run();
    } catch (std::exception& e2) {
      LOG_ERROR("Failed to restart server: {}", e2.what());
    }
  } catch (...) {
    LOG_ERROR("Unknown error occurred in server");
  }
}

void SignalServer::SendMsg(websocketpp::connection_hdl hdl, json message) {
  if (hdl.expired()) {
    LOG_ERROR("Destination hdl invalid, msg: {}", message.dump());
    return;
  }

  try {
    server_.send(hdl, message.dump(), websocketpp::frame::opcode::text);
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to send message: {}", e.what());
  } catch (...) {
    LOG_ERROR("Failed to send message: unknown error");
  }
}

void SignalServer::OnMessage(websocketpp::connection_hdl hdl,
                             server::message_ptr msg) {
  if (!signal_negotiation_) {
    return;
  }

  try {
    std::string payload = msg->get_payload();
    json j;
    try {
      j = json::parse(payload);
    } catch (json::parse_error& e) {
      LOG_ERROR("Failed to parse JSON message: {}", e.what());
      return;
    }

    if (!j.contains("type") || !j["type"].is_string()) {
      LOG_ERROR("Message missing 'type' field");
      return;
    }

    std::string type = j["type"].get<std::string>();

    switch (HASH_STRING_PIECE(type.c_str())) {
      case "ping"_H: {
        if (transmission_manager_) {
          transmission_manager_->UpdateWsHandleLastActiveTime(hdl);
          json message = {{"type", "pong"}};
          server_.send(hdl, message.dump(), websocketpp::frame::opcode::text);
        }
        break;
      }
      case "login"_H:
        signal_negotiation_->login_user(hdl, j);
        if (presence_manager_) {
          std::string id = transmission_manager_->GetUserId(hdl);
          if (!id.empty()) {
            presence_manager_->OnLogin(id, id, hdl);
          }
        }
        break;
      case "user_leave_transmission"_H:
        signal_negotiation_->leave_transmission(hdl, j);
        break;
      case "query_user_id_list"_H:
        signal_negotiation_->query_user_id_list(hdl, j);
        break;
      case "join_transmission"_H:
        signal_negotiation_->join_transmission(hdl, j);
        break;
      case "offer"_H:
        signal_negotiation_->offer(hdl, j);
        break;
      case "answer"_H:
        signal_negotiation_->answer(hdl, j);
        break;
      case "new_candidate"_H:
        signal_negotiation_->new_candidate(hdl, j);
        break;
      case "new_candidate_mid"_H:
        signal_negotiation_->new_candidate_mid(hdl, j);
        break;
      case "recent_connections_presence"_H: {
        std::string user_id;
        if (!j.contains("user_id") || !j["user_id"].is_string()) {
          LOG_ERROR("recent_connections missing field: user_id");
          break;
        }
        user_id = j["user_id"].get<std::string>();
        std::vector<std::string> device_ids;
        if (j.contains("devices") && j["devices"].is_array()) {
          for (auto& v : j["devices"]) {
            if (v.is_string()) device_ids.push_back(v.get<std::string>());
          }
        }
        if (presence_manager_) {
          presence_manager_->UpdateUserDevices(user_id, device_ids);
          auto statuses = presence_manager_->BatchQuery(device_ids);
          json resp = {{"type", "presence"}, {"devices", json::array()}};
          for (const auto& p : statuses) {
            resp["devices"].push_back({{"id", p.first}, {"online", p.second}});
          }
          server_.send(hdl, resp.dump(), websocketpp::frame::opcode::text);
        }
        break;
      }
      default:
        LOG_WARN("Unknown message type: {}", type);
        break;
    }
  } catch (std::exception& e) {
    LOG_ERROR("Error processing message: {}", e.what());
  } catch (...) {
    LOG_ERROR("Unknown error processing message");
  }
}
