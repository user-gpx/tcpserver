#pragma once

#include "reactor/Epoller.h"

#include <string>

class Config {
  public:
    int port;
    int M;
    int N;
    int poolSize;
    int logWrite;
    int closeLog;
    TriggerMode listenTriggerMode;
    TriggerMode connTriggerMode;
    std::string dbHost;
    int dbPort;
    std::string dbUser;
    std::string dbPassword;
    std::string dbName;

    Config();
    void parser(int argc, char* argv[]);

  private:
    void applyTriggerCombination(int mode);
};
