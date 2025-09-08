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
  SignalServer s;
  std::string port = "9090";
  std::string log_dir = "./logs";

  if (argc > 1) {
    port = argv[1];
  }

  if (argc > 2) {
    log_dir = argv[2];
  }

  InitLogger(log_dir);
  s.Run(std::stoi(port));
  return 0;
}

#endif