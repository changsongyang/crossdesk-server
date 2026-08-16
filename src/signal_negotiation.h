/*
 * @Author: DI JUNKUN
 * @Date: 2025-06-26
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SIGNAL_NEGOTIATION_H_
#define _SIGNAL_NEGOTIATION_H_

#include <nlohmann/json.hpp>

#include "device_db_manager.h"
#include "transmission_manager.h"
#include "turn_credentials.h"

using nlohmann::json;

class SignalNegotiation {
 public:
  SignalNegotiation(std::shared_ptr<TransmissionManager> transmission_manager,
                    DeviceDBManager* device_db,
                    std::shared_ptr<TurnCredentialIssuer>
                        turn_credential_issuer = nullptr);
  ~SignalNegotiation();

  void SetSendMsgCallback(
      std::function<void(websocketpp::connection_hdl, json)> send_msg) {
    send_msg_ = send_msg;
  }

  bool login_user(websocketpp::connection_hdl hdl, const json& j);
  bool leave_transmission(websocketpp::connection_hdl hdl, const json& j);
  bool query_user_id_list(websocketpp::connection_hdl hdl, const json& j);
  bool join_transmission(websocketpp::connection_hdl hdl, const json& j);
  bool offer(websocketpp::connection_hdl hdl, const json& j);
  bool answer(websocketpp::connection_hdl hdl, const json& j);
  bool new_candidate(websocketpp::connection_hdl hdl, const json& j);
  bool new_candidate_mid(websocketpp::connection_hdl hdl, const json& j);
  bool change_password(websocketpp::connection_hdl hdl, const json& j);
  bool turn_credentials(websocketpp::connection_hdl hdl, const json& j);
  void OnWebClientDisconnect(const std::string& user_id);

 private:
  void AddTurnCredentials(json& message, const std::string& user_id) const;

  std::shared_ptr<TransmissionManager> transmission_manager_;
  DeviceDBManager* device_db_manager_;
  std::shared_ptr<TurnCredentialIssuer> turn_credential_issuer_;
  std::function<void(websocketpp::connection_hdl, json)> send_msg_;
};

#endif
