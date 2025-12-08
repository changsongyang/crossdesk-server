/*
 * @Author: DI JUNKUN
 * @Date: 2025-09-08
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include <iostream>

#include "log/log.h"
#include "signal_server.h"

int main(int argc, char* argv[]) {
  std::string port = "9090";
  std::string log_dir = "/var/log/crossdesk";
  std::string certs_dir = "/var/lib/crossdesk/certs";
  std::string db_path = "/var/lib/crossdesk/db/crossdesk-server.db";

  if (argc > 1) {
    port = argv[1];
  }

  InitLogger(log_dir);

  try {
    SignalServer s(std::stoi(port), certs_dir, db_path);
    s.Run();
  } catch (std::exception& e) {
    LOG_ERROR("Fatal error: {}", e.what());
    return 1;
  } catch (...) {
    LOG_ERROR("Unknown fatal error occurred");
    return 1;
  }

  return 0;
}

#endif