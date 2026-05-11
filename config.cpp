#include "config.h"

#include <cstdlib>
#include <getopt.h>

Config::Config()
    : port(9006), M(1), N(1), poolSize(3), // poolSize: worker thread pool size per reactor
      logWrite(1),                         // logWrite: 0 sync, 1 async
      closeLog(0),                         // closeLog: 0 open, 1 close
      listenTriggerMode(TriggerMode::EdgeTrigger), connTriggerMode(TriggerMode::EdgeTrigger),
      dbHost("127.0.0.1"), dbPort(3306), dbUser("tcpserver"), dbPassword("123456"),
      dbName("tcpserver") {}
void Config::parser(int argc, char* argv[]) {
    int opt;
    enum {
        OPT_DB_HOST = 1000,
        OPT_DB_PORT,
        OPT_DB_USER,
        OPT_DB_PASSWORD,
        OPT_DB_NAME,
    };
    static option longOptions[] = {
        {"db-host", required_argument, nullptr, OPT_DB_HOST},
        {"db-port", required_argument, nullptr, OPT_DB_PORT},
        {"db-user", required_argument, nullptr, OPT_DB_USER},
        {"db-password", required_argument, nullptr, OPT_DB_PASSWORD},
        {"db-name", required_argument, nullptr, OPT_DB_NAME},
        {nullptr, 0, nullptr, 0},
    };
    const char* options = "p:M:N:S:l:c:T:";
    while ((opt = getopt_long(argc, argv, options, longOptions, nullptr)) != -1) {
        switch (opt) {
        case 'p': port = std::atoi(optarg); break;
        case 'M': M = std::atoi(optarg); break;
        case 'N': N = std::atoi(optarg); break;
        case 'S': poolSize = std::atoi(optarg); break;
        case 'l': logWrite = std::atoi(optarg); break;
        case 'c': closeLog = std::atoi(optarg); break;
        case 'T': applyTriggerCombination(std::atoi(optarg)); break;
        case OPT_DB_HOST: dbHost = optarg; break;
        case OPT_DB_PORT: dbPort = std::atoi(optarg); break;
        case OPT_DB_USER: dbUser = optarg; break;
        case OPT_DB_PASSWORD: dbPassword = optarg; break;
        case OPT_DB_NAME: dbName = optarg; break;
        default: break;
        }
    }
    if (port <= 0 || port > 65535) { port = 9006; }
    if (poolSize < 0) { poolSize = 0; }
    if (N < 0) { N = 0; }
    if (dbPort <= 0 || dbPort > 65535) { dbPort = 3306; }
    if (dbHost.empty()) { dbHost = "127.0.0.1"; }
    if (dbUser.empty()) { dbUser = "tcpserver"; }
    if (dbName.empty()) { dbName = "tcpserver"; }
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
