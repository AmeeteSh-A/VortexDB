#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <thread>
#include "vortex.h"
#include "server.h"

namespace fs = std::filesystem;

int main() {
    VortexDB root("root");
    VortexServer server(&root, 8080);

    std::thread server_thread([&server]() {
        server.start();
    });
    server_thread.detach();

    std::vector<VortexDB*> path_stack;
    std::vector<std::string> name_stack;

    path_stack.push_back(&root);
    name_stack.push_back("root");

    std::cout << "[SYSTEM] Network server started on port 8080\n";

    while (true) {
        std::cout << "Vortex [";
        for (size_t i = 0; i < name_stack.size(); ++i) {
            std::cout << name_stack[i];
            if (i < name_stack.size() - 1) std::cout << "/";
        }
        std::cout << "] > ";

        std::string input;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        std::stringstream ss(input);
        std::string cmd;
        ss >> cmd;

        VortexDB* current_db = path_stack.back();

        if (cmd == "exit") {
            server.stop();
            break;
        } else if (cmd == "ls") {
            std::vector<std::string> keys = current_db->get_all_keys();
            for (const std::string& key : keys) {
                if (current_db->get_type(key) == ValueType::SUB_VORTEX) {
                    std::cout << "[DIR]   " << key << "\n";
                } else {
                    std::cout << "[FILE] " << key << "\n";
                }
            }
        } else if (cmd == "spawn") {
            std::string arg;
            ss >> arg;
            if (!arg.empty()) {
                current_db->spawn_vortex(arg);
            }
        } else if (cmd == "convert") {
            std::string arg;
            std::getline(ss >> std::ws, arg);
            if (!arg.empty() && arg.front() == '"') arg.erase(0, 1);
            if (!arg.empty() && arg.back() == '"') arg.pop_back();
            if (!arg.empty()) {
                current_db->convert_to_subdb(arg);
            }
        } else if (cmd == "cd") {
            std::string arg;
            ss >> arg;
            if (arg == "..") {
                if (path_stack.size() > 1) {
                    path_stack.pop_back();
                    name_stack.pop_back();
                }
            } else if (!arg.empty()) {
                if (current_db->get_type(arg) == ValueType::SUB_VORTEX) {
                    std::stringstream path_ss(arg);
                    std::string part;
                    VortexDB* traverse_db = current_db;
                    
                    while (std::getline(path_ss, part, '/')) {
                        if (part.empty()) continue;
                        traverse_db = traverse_db->resolve_path(part);
                        if (traverse_db) {
                            path_stack.push_back(traverse_db);
                            name_stack.push_back(part);
                        }
                    }
                } else {
                    std::cout << "[SYSTEM] Error: '" << arg << "' is not a valid directory.\n";
                }
            }
        } else if (cmd == "put") {
            std::string key, val;
            ss >> key;
            std::getline(ss, val);
            if (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);
            if (!key.empty() && !val.empty()) {
                current_db->put(key, val);
            }
        } else if (cmd == "get") {
            std::string key;
            ss >> key;
            if (!key.empty()) {
                if (key == "range" || key == "RANGE") {
                    std::string start_key, end_key;
                    if (ss >> start_key >> end_key) {
                        if (!start_key.empty() && start_key.front() == '"') start_key.erase(0, 1);
                        if (!start_key.empty() && start_key.back() == '"') start_key.pop_back();
                        if (!end_key.empty() && end_key.front() == '"') end_key.erase(0, 1);
                        if (!end_key.empty() && end_key.back() == '"') end_key.pop_back();

                        auto results = current_db->get_range(start_key, end_key);
                        if (results.empty()) {
                            std::cout << "ERROR: No matching keys found\n";
                        } else {
                            std::cout << "--- RANGE MATCHES ---\n";
                            for (const auto& p : results) {
                                std::cout << "[" << p.first << "] => " << p.second << "\n";
                            }
                            std::cout << "----------------------\n";
                        }
                    } else {
                        std::cout << "[SYSTEM] Error: Missing Range Bounds\n";
                    }
                } else if (key.back() == '*') {
                    std::string prefix = key.substr(0, key.length() - 1);
                    auto results = current_db->get_prefix(prefix);
                    if (results.empty()) {
                        std::cout << "ERROR: No matching keys found\n";
                    } else {
                        std::cout << "--- PREFIX MATCHES ---\n";
                        for (const auto& p : results) {
                            std::cout << "[" << p.first << "] => " << p.second << "\n";
                        }
                        std::cout << "----------------------\n";
                    }
                } else {
                    std::cout << current_db->get(key) << "\n";
                }
            }
        } else if (cmd == "del") {
            std::string key;
            ss >> key;
            if (!key.empty()) {
                current_db->del(key);
            }
        } else if (cmd == "stats") {
            VortexStats stats = current_db->get_stats();
            std::cout << "\n--- [" << current_db->get_db_name() << "] HEALTH REPORT ---\n";
            std::cout << "   Live Keys     : " << stats.live_keys << "\n";
            std::cout << "   Total Entries : " << stats.total_entries << "\n";
            std::cout << "   Waste Ratio   : " << std::fixed << std::setprecision(2) << stats.waste_ratio * 100 << "%\n";
            std::cout << "--------------------------------\n\n";
        } else if (cmd == "compact") {
            current_db->compact();
        } else if (cmd == "compact_all") {
            std::cout << "[SYSTEM] Initiating recursive compaction...\n";
            current_db->compact_all();
            std::cout << "[SYSTEM] Full database compaction complete.\n";
        } else if (cmd == "save") {
            std::string arg1, arg2;
            ss >> arg1;
            if (ss >> arg2) {
                root.save_state(arg2, arg1);
            } else if (!arg1.empty()) {
                current_db->save_state(arg1);
            }
        } else if (cmd == "load") {
            std::string arg;
            ss >> arg;
            if (!arg.empty()) {
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::string auto_name = "autosave_" + std::to_string(time);
                root.save_state(auto_name);
                std::cout << "[SYSTEM] Safety backup created: " << auto_name << "\n";
                
                root.load_state(arg);
                path_stack.clear();
                name_stack.clear();
                path_stack.push_back(&root);
                name_stack.push_back("root");
                std::cout << "[SYSTEM] State loaded. Shell reset to root.\n";
            }
        } else if (cmd == "loadlast") {
            std::string newest_state = "";
            fs::file_time_type last_time = (fs::file_time_type::min)();
            
            for (const auto& entry : fs::directory_iterator(".")) {
                if (entry.path().extension() == ".meta") {
                    std::string fname = entry.path().stem().string();
                    if (fname.find("autosave_") == std::string::npos) {
                        auto ftime = fs::last_write_time(entry);
                        if (ftime > last_time) {
                            last_time = ftime;
                            newest_state = fname;
                        }
                    }
                }
            }
            if (!newest_state.empty()) {
                std::cout << "[SYSTEM] Loading most recent backup: " << newest_state << "\n";
                root.load_state(newest_state);
                path_stack.clear();
                name_stack.clear();
                path_stack.push_back(&root);
                name_stack.push_back("root");
            } else {
                std::cout << "[SYSTEM] No valid backups found.\n";
            }
        } else {
            std::cout << "[SYSTEM] Unknown command.\n";
        }
    }
    return 0;
}