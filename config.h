#pragma once

#include "reactor/Epoller.h"

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

    Config();
    void parser(int argc, char* argv[]);

  private:
    void applyTriggerCombination(int mode);
};
