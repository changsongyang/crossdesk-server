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
#include <websocketpp/http/constants.hpp>
#include <websocketpp/server.hpp>

#include "device_db_manager.h"
#include "presence_manager.h"
#include "signal_negotiation.h"

using nlohmann::json;

typedef websocketpp::server<websocketpp::config::asio_tls> server;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>
    context_ptr;
typedef unsigned int connection_id;

class SignalServer {
 public:
  SignalServer();
  SignalServer(uint16_t port, std::string certs_dir, std::string db_path);
  ~SignalServer();

  bool OnOpen(websocketpp::connection_hdl hdl);
  bool OnClose(websocketpp::connection_hdl hdl);
  bool OnFail(websocketpp::connection_hdl hdl);
    void OnHttp(websocketpp::connection_hdl hdl);
  context_ptr OnTlsInit(websocketpp::connection_hdl hdl);
  bool OnPing(websocketpp::connection_hdl hdl, std::string s);
  bool OnPong(websocketpp::connection_hdl hdl, std::string s);

  void Run();
  void SendMsg(websocketpp::connection_hdl hdl, json message);
  void OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg);

 private:
  server server_;
  uint16_t port_ = 9090;
  std::string certs_dir_ = "/var/lib/crossdesk/certs";
  std::string db_path_ = "/var/lib/crossdesk/db/crossdesk-server.db";
  std::map<websocketpp::connection_hdl, connection_id,
           std::owner_less<websocketpp::connection_hdl>>
      ws_connections_;
  unsigned int ws_connection_id_ = 0;

  std::shared_ptr<TransmissionManager> transmission_manager_;
  std::unique_ptr<DeviceDBManager> device_db_manager_;
  std::unique_ptr<SignalNegotiation> signal_negotiation_;
  std::unique_ptr<PresenceManager> presence_manager_;
};

#endif
