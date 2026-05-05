#include "config.h"

#include <cstdlib>
#include <getopt.h>

Config::Config()
    : port(9006), M(1), N(1), poolSize(3), // poolSize: worker thread pool size per reactor
      logWrite(1),                         // logWrite: 0 sync, 1 async
      closeLog(0),                         // closeLog: 0 open, 1 close
      listenTriggerMode(TriggerMode::EdgeTrigger), connTriggerMode(TriggerMode::EdgeTrigger) {}
void Config::parser(int argc, char* argv[]) {
    int opt;
    const char* options = "p:M:N:S:l:c:T:";
    while ((opt = getopt(argc, argv, options)) != -1) {
        switch (opt) {
        case 'p': port = std::atoi(optarg); break;
        case 'M': M = std::atoi(optarg); break;
        case 'N': N = std::atoi(optarg); break;
        case 'S': poolSize = std::atoi(optarg); break;
        case 'l': logWrite = std::atoi(optarg); break;
        case 'c': closeLog = std::atoi(optarg); break;
        case 'T': applyTriggerCombination(std::atoi(optarg)); break;
        default: break;
        }
    }
    if (port <= 0 || port > 65535) { port = 9006; }
    if (poolSize < 0) { poolSize = 0; }
    if (N < 0) { N = 0; }
}

void Config::applyTriggerCombination(int mode) { // 0 LT/LT, 1 LT/ET, 2 ET/LT, 3 ET/ET
    switch (mode) {
    case 0:
        listenTriggerMode = TriggerMode::LevelTrigger;
        connTriggerMode = TriggerMode::LevelTrigger;
        break;
    case 1:
        listenTriggerMode = TriggerMode::LevelTrigger;
        connTriggerMode = TriggerMode::EdgeTrigger;
        break;
    case 2:
        listenTriggerMode = TriggerMode::EdgeTrigger;
        connTriggerMode = TriggerMode::LevelTrigger;
        break;
    case 3:
        listenTriggerMode = TriggerMode::EdgeTrigger;
        connTriggerMode = TriggerMode::EdgeTrigger;
        break;
    default: break;
    }
}
