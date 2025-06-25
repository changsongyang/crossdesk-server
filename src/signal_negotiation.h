#ifndef _SIGNAL_NEGOTIATION_H_
#define _SIGNAL_NEGOTIATION_H_

#include <nlohmann/json.hpp>

#include "device_db_manager.h"
#include "transmission_manager.h"

using nlohmann::json;

class SignalNegotiation {
 public:
  SignalNegotiation();
  ~SignalNegotiation();

  bool login_user(websocketpp::connection_hdl hdl, const json& j);
  bool leave_transmission(websocketpp::connection_hdl hdl, const json& j);
  bool query_user_id_list(websocketpp::connection_hdl hdl, const json& j);
  bool offer(websocketpp::connection_hdl hdl, const json& j);
  bool answer(websocketpp::connection_hdl hdl, const json& j);
  bool new_candidate(websocketpp::connection_hdl hdl, const json& j);

 private:
  TransmissionManager transmission_manager_;
  std::unique_ptr<DeviceDBManager> device_db_manager_;
};

#endif
