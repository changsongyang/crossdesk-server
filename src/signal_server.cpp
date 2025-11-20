#include "signal_server.h"

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
  server_.set_pong_handler(std::bind(&SignalServer::OnPing, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));

  transmission_manager_ = std::make_shared<TransmissionManager>();
  signal_negotiation_ =
      std::make_unique<SignalNegotiation>(transmission_manager_, db_path_);
  signal_negotiation_->SetSendMsgCallback(std::bind(&SignalServer::SendMsg,
                                                    this, std::placeholders::_1,
                                                    std::placeholders::_2));
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
  server_.set_pong_handler(std::bind(&SignalServer::OnPing, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));

  transmission_manager_ = std::make_shared<TransmissionManager>();
  signal_negotiation_ =
      std::make_unique<SignalNegotiation>(transmission_manager_, db_path_);
  signal_negotiation_->SetSendMsgCallback(std::bind(&SignalServer::SendMsg,
                                                    this, std::placeholders::_1,
                                                    std::placeholders::_2));
}

SignalServer::~SignalServer() {}

bool SignalServer::OnOpen(websocketpp::connection_hdl hdl) {
  ws_connections_[hdl] = ws_connection_id_++;
  return true;
}

bool SignalServer::OnClose(websocketpp::connection_hdl hdl) {
  std::string user_id = transmission_manager_->ReleaseUserFromWsHandle(hdl);
  if (!user_id.empty()) {
    LOG_INFO("Websocket connection [{}|{}] closed", ws_connections_[hdl],
             user_id);
  }
  ws_connections_.erase(hdl);
  return true;
}

bool SignalServer::OnFail(websocketpp::connection_hdl hdl) {
  std::string user_id = transmission_manager_->ReleaseUserFromWsHandle(hdl);
  if (!user_id.empty()) {
    LOG_INFO("Websocket connection [{}|{}] failed", ws_connections_[hdl],
             user_id);
  }
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

    std::string cert_file = certs_dir_ + "/crossdesk.cn_bundle.crt";
    std::string key_file = certs_dir_ + "/crossdesk.cn.key";
    ctx->use_certificate_chain_file(cert_file);
    ctx->use_private_key_file(key_file, asio::ssl::context::pem);

    SSL_CTX_set_cipher_list(ctx->native_handle(),
                            "ECDHE-ECDSA-AES256-GCM-SHA384:"
                            "ECDHE-RSA-AES256-GCM-SHA384:"
                            "ECDHE-ECDSA-AES128-GCM-SHA256:"
                            "ECDHE-RSA-AES128-GCM-SHA256");
  } catch (std::exception& e) {
    std::cout << "Exception: " << e.what() << std::endl;
  }
  return ctx;
}

bool SignalServer::OnPing(websocketpp::connection_hdl hdl, std::string s) {
  transmission_manager_->UpdateWsHandleLastActiveTime(hdl);
  return true;
}

bool SignalServer::OnPong(websocketpp::connection_hdl hdl, std::string s) {
  return true;
}

void SignalServer::Run() {
  if (!std::filesystem::exists(certs_dir_)) {
    LOG_ERROR("Certs dir [{}] not exist", certs_dir_);
    return;
  }

  server_.set_reuse_addr(true);
  LOG_INFO("Signal server runs on port [{}]", port_);

  server_.listen(port_);
  server_.start_accept();
  server_.run();
}

void SignalServer::SendMsg(websocketpp::connection_hdl hdl, json message) {
  if (!hdl.expired()) {
    server_.send(hdl, message.dump(), websocketpp::frame::opcode::text);
  } else {
    LOG_ERROR("Destination hdl invalid, msg: {}", message.dump());
  }
}

void SignalServer::OnMessage(websocketpp::connection_hdl hdl,
                             server::message_ptr msg) {
  if (!signal_negotiation_) {
    return;
  }

  std::string payload = msg->get_payload();
  auto j = json::parse(payload);
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
    default:
      break;
  }
}
