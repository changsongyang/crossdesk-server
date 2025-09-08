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
  std::string log_dir = "./logs";
  std::string certs_dir = "./cert";
  std::string db_path = "devices.db";

  if (argc > 1) {
    port = argv[1];
  }

  if (argc > 2) {
    certs_dir = argv[2];
  }

  if (argc > 3) {
    db_path = argv[3];
  }

  if (argc > 4) {
    log_dir = argv[4];
  }

  InitLogger(log_dir);

  SignalServer s;
  s.Run(std::stoi(port), certs_dir, db_path);
  return 0;
}

#endif