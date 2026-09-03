#include "daysetta/daemon.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

std::string default_runtime_dir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && xdg[0] != '\0') return std::string(xdg) + "/daysetta";
    return "/tmp/daysetta-" + std::to_string(::geteuid());
}

void usage(std::FILE* out) {
    std::fprintf(out,
                 "usage: daysetta-broker [--runtime-dir DIR] [--verbose]\n"
                 "  --runtime-dir DIR  directory holding broker.sock\n"
                 "  --verbose          log connections and registry changes\n");
}

}  // namespace

int main(int argc, char** argv) {
    ds::BrokerConfig config;
    config.runtime_dir = default_runtime_dir();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--runtime-dir") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "daysetta-broker: --runtime-dir needs a value\n");
                return 2;
            }
            config.runtime_dir = argv[++i];
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(stdout);
            return 0;
        } else {
            std::fprintf(stderr, "daysetta-broker: unknown argument: %s\n", arg.c_str());
            usage(stderr);
            return 2;
        }
    }

    auto result = ds::run_broker(config);
    if (!result) {
        std::fprintf(stderr, "daysetta-broker: %s\n", result.error().message.c_str());
        return 1;
    }
    return 0;
}
