#include "server/TCPServer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [-p port] [-N count] [-M mode]\n"
              << "  -p  listen port, default 9006\n"
              << "  -N  count, default 0 in mode 0 and 1 in mode 1\n"
              << "  -M  run mode: 0 normal, 1 SO_REUSEPORT, default 1\n";
}
} // namespace

int main(int argc, char* argv[]) {
    TCPServer server;
    int port = 9006;
    int N = 0;
    int M = 1; // 0: normal server, 1: SO_REUSEPORT server

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p") {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            port = std::atoi(argv[++i]);
        } else if (arg == "-N") {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            N = std::atoi(argv[++i]);
        } else if (arg == "-M") {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            M = std::atoi(argv[++i]);
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (M == 1) {
        if (N <= 0) { N = 1; }
        TCPServer::REUSEPORT_RUNNING(N, port);
        return 0;
    }

    if (server.init(port) < 0) { return 1; }
    if (server.Listening() < 0) { return 1; }
    if (server.running(N) < 0) { return 1; }

    return 0;
}
