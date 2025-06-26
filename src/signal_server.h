/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-26
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SIGNAL_SERVER_H_
#define _SIGNAL_SERVER_H_

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include "signal_negotiation.h"

using nlohmann::json;

typedef websocketpp::server<websocketpp::config::asio_tls> server;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>
    context_ptr;
typedef unsigned int connection_id;

class SignalServer {
 public:
  SignalServer();
  ~SignalServer();

  bool on_open(websocketpp::connection_hdl hdl);
  bool on_close(websocketpp::connection_hdl hdl);
  bool on_fail(websocketpp::connection_hdl hdl);
  context_ptr on_tls_init(websocketpp::connection_hdl hdl);
  bool on_ping(websocketpp::connection_hdl hdl, std::string s);
  bool on_pong(websocketpp::connection_hdl hdl, std::string s);

  void run(uint16_t port);
  void send_msg(websocketpp::connection_hdl hdl, json message);
  void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg);

 private:
  server server_;
  std::map<websocketpp::connection_hdl, connection_id,
           std::owner_less<websocketpp::connection_hdl>>
      ws_connections_;
  unsigned int ws_connection_id_ = 0;

  std::shared_ptr<TransmissionManager> transmission_manager_;
  std::unique_ptr<SignalNegotiation> signal_negotiation_;
};

#endif