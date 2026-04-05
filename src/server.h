#ifndef VORTEX_SERVER_H
#define VORTEX_SERVER_H

#include <winsock2.h>
#include <string>
#include <vector>
#include <thread>
#include "vortex.h"

#pragma comment(lib, "ws2_32.lib")

class VortexServer {
private:
    VortexDB* db; 
    SOCKET listen_socket;
    int port;
    bool running;

    void handle_client(SOCKET client_socket);
    std::string process_command(const std::string& raw_cmd);

public:
    VortexServer(VortexDB* database, int p = 8080);
    ~VortexServer();

    void start();
    void stop();
};

#endif