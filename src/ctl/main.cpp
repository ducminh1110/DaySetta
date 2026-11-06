#include "daysetta/event.hpp"
#include "daysetta/render.hpp"
#include "daysetta/sdk.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

std::string default_runtime_dir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && xdg[0] != '\0') return std::string(xdg) + "/daysetta";
    return "/tmp/daysetta-" + std::to_string(::geteuid());
}

void usage(std::FILE* out) {
    std::fprintf(
        out,
        "usage: daysetta-ctl [--runtime-dir DIR] COMMAND [OPTIONS]\n"
        "commands:\n"
        "  list | dump                 print the server scene JSON\n"
        "  ping                        ping the server\n"
        "  status                      print hello fields and the scene JSON\n"
        "  inject --type TYPE [--x N] [--y N] [--button N] [--key N] [--target N]\n"
        "                             inject pointer, button, key or scroll input\n"
        "  capture --width N --height N --out FILE\n"
        "                             capture a frame and write a PPM\n"
        "inject types: pointer_move, button_down, button_up, key_down, key_up, scroll\n");
}

int usage_error(const std::string& message) {
    std::fprintf(stderr, "daysetta-ctl: %s\n", message.c_str());
    usage(stderr);
    return 2;
}

int operational_error(const ds::Error& error) {
    std::fprintf(stderr, "daysetta-ctl: %s: %s\n", ds::code_name(error.code),
                 error.message.c_str());
    return 1;
}

struct Options {
    std::string key;
    std::string value;
};

bool parse_options(int argc, char** argv, int start, std::vector<Options>* options) {
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() < 3 || arg[0] != '-' || arg[1] != '-') return false;
        std::string key;
        std::string value;
        std::size_t equals = arg.find('=');
        if (equals != std::string::npos) {
            key = arg.substr(2, equals - 2);
            value = arg.substr(equals + 1);
        } else {
            key = arg.substr(2);
            if (i + 1 >= argc) return false;
            value = argv[++i];
        }
        if (key.empty()) return false;
        options->push_back(Options{std::move(key), std::move(value)});
    }
    return true;
}

const std::string* find_option(const std::vector<Options>& options,
                               std::string_view key) {
    for (const Options& option : options) {
        if (option.key == key) return &option.value;
    }
    return nullptr;
}

bool parse_i64(std::string_view text, std::int64_t* value) {
    if (text.empty()) return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), *value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool parse_u64(std::string_view text, std::uint64_t* value) {
    if (text.empty() || text.front() == '-') return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), *value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool parse_i32_option(const std::vector<Options>& options, std::string_view key,
                      std::int32_t* value) {
    const std::string* text = find_option(options, key);
    if (text == nullptr) return true;
    std::int64_t parsed = 0;
    if (!parse_i64(*text, &parsed)) return false;
    if (parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    *value = static_cast<std::int32_t>(parsed);
    return true;
}

bool parse_u64_option(const std::vector<Options>& options, std::string_view key,
                      std::uint64_t* value) {
    const std::string* text = find_option(options, key);
    if (text == nullptr) return true;
    return parse_u64(*text, value);
}

std::optional<ds::event::Type> parse_event_type(std::string_view name) {
    using ds::event::Type;
    if (name == "pointer_move") return Type::pointer_move;
    if (name == "button_down") return Type::button_down;
    if (name == "button_up") return Type::button_up;
    if (name == "key_down") return Type::key_down;
    if (name == "key_up") return Type::key_up;
    if (name == "scroll") return Type::scroll;
    return std::nullopt;
}

bool only_keys(const std::vector<Options>& options,
               std::initializer_list<std::string_view> allowed) {
    for (const Options& option : options) {
        bool found = false;
        for (std::string_view key : allowed) {
            if (option.key == key) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

int command_list(const std::string& runtime_dir) {
    auto client = ds::sdk::Client::connect(runtime_dir, "daysetta-ctl",
                                           ds::proto::Role::control);
    if (!client) return operational_error(client.error());
    auto json = client.value().scene_dump();
    if (!json) return operational_error(json.error());
    std::cout << json.value() << '\n';
    return 0;
}

int command_ping(const std::string& runtime_dir) {
    auto client = ds::sdk::Client::connect(runtime_dir, "daysetta-ctl",
                                           ds::proto::Role::control);
    if (!client) return operational_error(client.error());
    auto token = client.value().ping(0x44535431ull);
    if (!token) return operational_error(token.error());
    std::cout << token.value() << '\n';
    return 0;
}

int command_status(const std::string& runtime_dir) {
    auto client = ds::sdk::Client::connect(runtime_dir, "daysetta-ctl",
                                           ds::proto::Role::control);
    if (!client) return operational_error(client.error());
    auto json = client.value().scene_dump();
    if (!json) return operational_error(json.error());

    const ds::sdk::HelloInfo& hello = client.value().hello();
    std::cout << "status (hello fields plus scene_dump; ds/1 has no SDK status call)\n";
    std::cout << "server_version: " << hello.server_version << '\n';
    std::cout << "session_id: " << hello.session_id << '\n';
    std::cout << "capabilities: 0x" << std::hex << hello.capabilities << std::dec << '\n';
    std::cout << "max_message: " << hello.max_message << '\n';
    std::cout << "max_windows: " << hello.max_windows << '\n';
    std::cout << "scene_dump: " << json.value() << '\n';
    return 0;
}

int command_inject(const std::string& runtime_dir, int argc, char** argv, int start) {
    std::vector<Options> options;
    if (!parse_options(argc, argv, start, &options)) {
        return usage_error("invalid inject options");
    }
    if (!only_keys(options, {"type", "x", "y", "button", "key", "target"})) {
        return usage_error("unknown inject option");
    }
    const std::string* type_name = find_option(options, "type");
    if (type_name == nullptr) return usage_error("inject requires --type");
    auto type = parse_event_type(*type_name);
    if (!type) return usage_error("unknown inject type: " + *type_name);

    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t button = 0;
    std::int32_t key = 0;
    std::uint64_t target = 0;
    if (!parse_i32_option(options, "x", &x) ||
        !parse_i32_option(options, "y", &y) ||
        !parse_i32_option(options, "button", &button) ||
        !parse_i32_option(options, "key", &key) ||
        !parse_u64_option(options, "target", &target)) {
        return usage_error("invalid inject numeric option");
    }

    ds::event::Event input;
    input.type = *type;
    input.target_window = target;
    input.timestamp_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    switch (input.type) {
        case ds::event::Type::pointer_move:
            input.a = x;
            input.b = y;
            break;
        case ds::event::Type::button_down:
        case ds::event::Type::button_up:
            input.a = x;
            input.b = y;
            input.c = button;
            break;
        case ds::event::Type::key_down:
        case ds::event::Type::key_up:
            input.a = key;
            break;
        case ds::event::Type::scroll:
            input.a = x;
            input.b = y;
            input.c = button;
            break;
        default:
            return usage_error("inject type is not supported");
    }

    auto client = ds::sdk::Client::connect(runtime_dir, "daysetta-ctl",
                                           ds::proto::Role::control);
    if (!client) return operational_error(client.error());
    auto hit = client.value().inject_input(input);
    if (!hit) return operational_error(hit.error());
    std::cout << hit.value() << '\n';
    return 0;
}

int command_capture(const std::string& runtime_dir, int argc, char** argv, int start) {
    std::vector<Options> options;
    if (!parse_options(argc, argv, start, &options)) {
        return usage_error("invalid capture options");
    }
    if (!only_keys(options, {"width", "height", "out"})) {
        return usage_error("unknown capture option");
    }
    const std::string* width_text = find_option(options, "width");
    const std::string* height_text = find_option(options, "height");
    const std::string* out = find_option(options, "out");
    if (width_text == nullptr || height_text == nullptr || out == nullptr ||
        out->empty()) {
        return usage_error("capture requires --width, --height and --out");
    }
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    if (!parse_u64(*width_text, &width) || !parse_u64(*height_text, &height) ||
        width == 0 || height == 0 ||
        width > std::numeric_limits<std::uint32_t>::max() ||
        height > std::numeric_limits<std::uint32_t>::max()) {
        return usage_error("capture dimensions are invalid");
    }

    auto client = ds::sdk::Client::connect(runtime_dir, "daysetta-ctl",
                                           ds::proto::Role::control);
    if (!client) return operational_error(client.error());
    auto frame = client.value().capture(static_cast<std::uint32_t>(width),
                                        static_cast<std::uint32_t>(height));
    if (!frame) return operational_error(frame.error());
    auto written = ds::render::write_ppm(*out, frame.value());
    if (!written) return operational_error(written.error());
    const std::string hash = ds::render::sha256_hex(frame.value().pixels);
    std::cout << frame.value().width << ',' << frame.value().height << ',' << hash
              << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string runtime_dir = default_runtime_dir();
    int index = 1;
    while (index < argc) {
        std::string arg = argv[index];
        if (arg == "--runtime-dir") {
            if (index + 1 >= argc) {
                return usage_error("--runtime-dir needs a value");
            }
            runtime_dir = argv[index + 1];
            index += 2;
        } else if (arg.rfind("--runtime-dir=", 0) == 0) {
            runtime_dir = arg.substr(std::string("--runtime-dir=").size());
            ++index;
        } else if (arg == "--help" || arg == "-h") {
            usage(stdout);
            return 0;
        } else if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
            return usage_error("unknown global option: " + arg);
        } else {
            break;
        }
    }

    if (index >= argc) return usage_error("missing command");
    const std::string command = argv[index++];

    if (command == "list" || command == "dump") {
        if (index != argc) return usage_error("command takes no options");
        return command_list(runtime_dir);
    }
    if (command == "ping") {
        if (index != argc) return usage_error("command takes no options");
        return command_ping(runtime_dir);
    }
    if (command == "status") {
        if (index != argc) return usage_error("command takes no options");
        return command_status(runtime_dir);
    }
    if (command == "inject") {
        return command_inject(runtime_dir, argc, argv, index);
    }
    if (command == "capture") {
        return command_capture(runtime_dir, argc, argv, index);
    }
    return usage_error("unknown command: " + command);
}
