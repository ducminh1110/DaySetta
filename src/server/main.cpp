#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "daysetta/daemon.hpp"

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  --runtime-dir DIR   session directory (default $XDG_RUNTIME_DIR/daysetta)\n"
                 "  --broker PATH       broker socket (default DIR/broker.sock)\n"
                 "  --service NAME      registered name (default org.daysetta.server)\n"
                 "  --socket PATH       server socket (default DIR/server.sock)\n"
                 "  --width N           output width (default 1280)\n"
                 "  --height N          output height (default 720)\n"
                 "  --tick-ms N         poll timeout (default 16)\n"
                 "  --frame-dir DIR     write PPM/BGRA frames\n"
                 "  --max-frames N      stop after N rendered frames (0 = unlimited)\n"
                 "  --verbose           log frames and clients\n",
                 argv0);
}

bool parse_u32(const char* text, std::uint32_t* out) {
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value > 0xffffffffUL) return false;
    *out = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    ds::ServerConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--runtime-dir") config.runtime_dir = next("--runtime-dir");
        else if (arg == "--broker") config.broker_path = next("--broker");
        else if (arg == "--service") config.service_name = next("--service");
        else if (arg == "--socket") config.server_socket = next("--socket");
        else if (arg == "--width") { if (!parse_u32(next("--width"), &config.width)) return 2; }
        else if (arg == "--height") { if (!parse_u32(next("--height"), &config.height)) return 2; }
        else if (arg == "--tick-ms") { if (!parse_u32(next("--tick-ms"), &config.tick_ms)) return 2; }
        else if (arg == "--frame-dir") config.frame_dir = next("--frame-dir");
        else if (arg == "--max-frames") { if (!parse_u32(next("--max-frames"), &config.max_frames)) return 2; }
        else if (arg == "--verbose") config.verbose = true;
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }
    if (config.width == 0 || config.height == 0 || config.width > 16384 || config.height > 16384) {
        std::fprintf(stderr, "invalid output size\n");
        return 2;
    }
    auto result = ds::run_server(config);
    if (!result) {
        std::fprintf(stderr, "daysetta-server: %s\n", result.error().message.c_str());
        return 1;
    }
    return 0;
}
