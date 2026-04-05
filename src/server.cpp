#include "server.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <ws2tcpip.h>

VortexServer::VortexServer(VortexDB* database, int p) : db(database), port(p), running(false) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        exit(1);
    }
}

VortexServer::~VortexServer() {
    stop();
    WSACleanup();
}

void VortexServer::start() {
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        return;
    }

    int opt = 1;
    setsockopt(listen_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        return;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        return;
    }

    running = true;
    std::cout << "[SERVER] VortexDB listening on port " << port << "...\n";

    while (running) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket != INVALID_SOCKET) {
            setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
            
            std::thread(&VortexServer::handle_client, this, client_socket).detach();
        }
    }
}

void VortexServer::stop() {
    running = false;
    closesocket(listen_socket);
}

void VortexServer::handle_client(SOCKET client_socket) {
    std::string welcome = "--- Welcome to VortexDB Server ---\r\n";
    send(client_socket, welcome.c_str(), (int)welcome.length(), 0);

    char buffer[1024];
    while (true) {
        int bytes_received = recv(client_socket, buffer, 1024, 0);
        if (bytes_received <= 0) break;

        std::string raw_cmd(buffer, bytes_received);
        
        while (!raw_cmd.empty() && (raw_cmd.back() == '\n' || raw_cmd.back() == '\r')) {
            raw_cmd.pop_back();
        }

        if (raw_cmd.empty()) continue;

        std::string response = process_command(raw_cmd) + "\r\n";
        send(client_socket, response.c_str(), (int)response.length(), 0);
    }
    closesocket(client_socket);
}

std::string VortexServer::process_command(const std::string& raw_cmd) {
    std::stringstream ss(raw_cmd);
    std::string action, key, value;
    
    if (!(ss >> action)) return "ERROR: Missing Action";
    
    for (auto & c: action) c = toupper(c);

    if (action == "SET") {
        if (!(ss >> key)) return "ERROR: Missing Key";
        std::getline(ss >> std::ws, value);
        
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
        }
        
        db->put(key, value);
        return "OK";
    } 
    
    if (action == "GET") {
        if (!(ss >> key)) return "ERROR: Missing Key";

        std::string upper_key = key;
        for (auto & c: upper_key) c = toupper(c);

        if (upper_key == "RANGE") {
            std::string start_key, end_key;
            if (!(ss >> start_key >> end_key)) return "ERROR: Missing Range Bounds";

            if (!start_key.empty() && start_key.front() == '"') start_key.erase(0, 1);
            if (!start_key.empty() && start_key.back() == '"') start_key.pop_back();
            if (!end_key.empty() && end_key.front() == '"') end_key.erase(0, 1);
            if (!end_key.empty() && end_key.back() == '"') end_key.pop_back();

            auto results = db->get_range(start_key, end_key);
            if (results.empty()) return "ERROR: No matching keys found";
            
            std::string res_str = "--- RANGE MATCHES ---\r\n";
            for (const auto& p : results) {
                res_str += "[" + p.first + "] => " + p.second + "\r\n";
            }
            res_str += "----------------------";
            
            while (!res_str.empty() && (res_str.back() == '\r' || res_str.back() == '\n')) {
                res_str.pop_back();
            }
            return res_str;
        }
        
        if (!key.empty() && key.back() == '*') {
            std::string prefix = key.substr(0, key.length() - 1);
            auto results = db->get_prefix(prefix);
            
            if (results.empty()) return "ERROR: No matching keys found";
            
            std::string res_str = "--- PREFIX MATCHES ---\r\n";
            for (const auto& p : results) {
                res_str += "[" + p.first + "] => " + p.second + "\r\n";
            }
            res_str += "----------------------";
            
            while (!res_str.empty() && (res_str.back() == '\r' || res_str.back() == '\n')) {
                res_str.pop_back();
            }
            return res_str;
        }

        return db->get(key);
    } 
    
    if (action == "DEL") {
        if (!(ss >> key)) return "ERROR: Missing Key";
        db->del(key);
        return "OK";
    }

    if (action == "CONVERT") {
        std::string arg;
        std::getline(ss >> std::ws, arg);
        if (!arg.empty() && arg.front() == '"') arg.erase(0, 1);
        if (!arg.empty() && arg.back() == '"') arg.pop_back();
        if (!arg.empty()) {
            db->convert_to_subdb(arg);
            return "OK";
        }
        return "ERROR: Missing Key";
    }

    if (action == "SAVE") {
        std::string arg1, arg2;
        if (!(ss >> arg1)) return "ERROR: Missing Argument";
        if (ss >> arg2) {
            db->save_state(arg2, arg1);
        } else {
            db->save_state(arg1);
        }
        return "OK";
    }

    if (action == "LOAD") {
        std::string arg;
        if (!(ss >> arg)) return "ERROR: Missing State Name";
        db->load_state(arg);
        return "OK";
    }

    if (action == "STATS") {
        VortexStats s = db->get_stats();
        return "LIVE:" + std::to_string(s.live_keys) + " TOTAL:" + std::to_string(s.total_entries);
    }

    return "ERROR: Unknown Command '" + action + "'";
}