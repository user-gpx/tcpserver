#include "config.h"
#include "db/MySQLStore.h"
#include "server/TCPServer.h"

#include <iostream>

namespace {
void print_usage(const char* program) {
    std::cerr << "Usage: " << program
              << " [-p port] [-M mode] [-N count] [-S poolsize] [-l 0|1] [-c 0|1]"
                 " [-T 0..3] [--db-host host] [--db-port port] [--db-user user]"
                 " [--db-password password] [--db-name name]\n"
              << "  -p  listen port, default 9006\n"
              << "  -M  run mode: 0 normal, 1 SO_REUSEPORT, default 1\n"
              << "  -N  subreactor/reuseport count, default 1\n"
              << "  -S  worker thread pool size per reactor, default 3\n"
              << "  -l  log write mode: 0 sync, 1 async, default 1\n"
              << "  -c  close log: 0 open, 1 close, default 0\n"
              << "  -T  trigger combination: 0 LT/LT, 1 LT/ET, 2 ET/LT, 3 ET/ET\n"
              << "  --db-host      MySQL host, default 127.0.0.1\n"
              << "  --db-port      MySQL port, default 3306\n"
              << "  --db-user      MySQL user, default tcpserver\n"
              << "  --db-password  MySQL password, default 123456\n"
              << "  --db-name      MySQL database, default tcpserver\n";
}
} // namespace

int main(int argc, char* argv[]) {
    Config config;
    config.parser(argc, argv);
    MySQLStore::configure(config);

    TCPServer server;
    if (config.M != 0 && config.M != 1) {
        print_usage(argv[0]);
        return 1;
    }

    if (config.M == 1) {
        if (config.N <= 0) { config.N = 1; }
        TCPServer::REUSEPORT_RUNNING(config);
        return 0;
    }

    if (server.init(config) < 0) { return 1; }
    if (server.Listening() < 0) { return 1; }
    if (server.running(config.N) < 0) { return 1; }

    return 0;
}
