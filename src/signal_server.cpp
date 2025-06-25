#include "signal_server.h"

#include <fstream>

#include "common.h"
#include "log.h"
#include "signal_negotiation.h"

SignalServer::SignalServer() {
  server_.set_error_channels(websocketpp::log::elevel::all);
  server_.set_access_channels(websocketpp::log::alevel::none);
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
  server_.set_ping_handler(std::bind(&SignalServer::on_ping, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));
  server_.set_pong_handler(std::bind(&SignalServer::on_pong, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));

  signal_negotiation_ = std::make_unique<SignalNegotiation>();
}

SignalServer::~SignalServer() {}

bool SignalServer::on_open(websocketpp::connection_hdl hdl) {
  ws_connections_[hdl] = ws_connection_id_++;
  return true;
}

bool SignalServer::on_close(websocketpp::connection_hdl hdl) {
  ws_connections_.erase(hdl);
  return true;
}

bool SignalServer::on_fail(websocketpp::connection_hdl hdl) { return true; }

bool SignalServer::on_ping(websocketpp::connection_hdl hdl, std::string s) {
  return true;
}

bool SignalServer::on_pong(websocketpp::connection_hdl hdl, std::string s) {
  return true;
}

void SignalServer::run(uint16_t port) {
  server_.set_reuse_addr(true);
  server_.set_tls_init_handler([](websocketpp::connection_hdl) {
    namespace asio = websocketpp::lib::asio;
    auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12);
    try {
      ctx->set_options(asio::ssl::context::default_workarounds |
                       asio::ssl::context::no_sslv2 |
                       asio::ssl::context::no_sslv3 |
                       asio::ssl::context::single_dh_use);
      ctx->use_certificate_chain_file("cert/crossdesk.cn_bundle.crt");
      ctx->use_private_key_file("cert/crossdesk.cn.key",
                                asio::ssl::context::pem);
    } catch (std::exception& e) {
      LOG_ERROR("TLS init failed: {}", e.what());
    }
    return ctx;
  });

  LOG_INFO("Signal server runs on port [{}]", port);

  server_.listen(port);
  server_.start_accept();
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
  if (!signal_negotiation_) {
    return;
  }

  std::string payload = msg->get_payload();
  auto j = json::parse(payload);
  std::string type = j["type"].get<std::string>();

  switch (HASH_STRING_PIECE(type.c_str())) {
    case "login"_H:
      signal_negotiation_->login_user(hdl, j);
      break;
    case "leave_transmission"_H:
      signal_negotiation_->leave_transmission(hdl, j);
      break;
    case "query_user_id_list"_H:
      signal_negotiation_->query_user_id_list(hdl, j);
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
    default:
      break;
  }
}
