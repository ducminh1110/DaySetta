// Process entry points shared by the executables and the integration test.
#pragma once

#include <cstdint>
#include <string>

#include "daysetta/result.hpp"

namespace ds {

struct BrokerConfig {
    std::string runtime_dir;
    bool verbose = false;
};

struct ServerConfig {
    std::string runtime_dir;
    std::string broker_path;      // empty: runtime_dir + "/broker.sock"
    std::string service_name = "org.daysetta.server";
    std::string server_socket;    // empty: runtime_dir + "/server.sock"
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t tick_ms = 16;
    std::string frame_dir;        // empty: no frame files
    std::uint32_t max_frames = 0; // 0: unlimited
    bool verbose = false;
    bool serve_dialect = false;   // reserved for ds-compat (M3)
};

Result<void> run_broker(const BrokerConfig& config);
Result<void> run_server(const ServerConfig& config);

}  // namespace ds
