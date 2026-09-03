#include "test.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "daysetta/ipc.hpp"
#include "daysetta/sdk.hpp"

namespace {

std::string make_temp_dir(const char* tag) {
    std::string pattern =
        "/tmp/daysetta-test-" + std::to_string(::getpid()) + "-" + tag + "-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* made = ::mkdtemp(buffer.data());
    if (made == nullptr) {
        throw ds::test::Failure(std::string("mkdtemp failed: ") + std::strerror(errno));
    }
    return std::string(made);
}

struct TempDir {
    std::string path;
    ~TempDir() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
};

struct BrokerProcess {
    pid_t pid = -1;

    ~BrokerProcess() {
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            for (int i = 0; i < 200; ++i) {
                int status = 0;
                if (::waitpid(pid, &status, WNOHANG) == pid) {
                    pid = -1;
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
            pid = -1;
        }
    }
};

void put_u32(std::vector<std::uint8_t>* bytes, std::size_t offset, std::uint32_t value) {
    (*bytes)[offset + 0] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    (*bytes)[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    (*bytes)[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::string test_json_escape(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

ds::sdk::broker_proto::Response broker_call(
    const std::string& runtime_dir, const ds::sdk::broker_proto::Request& request) {
    auto channel = ds::ipc::Channel::connect(runtime_dir + "/broker.sock");
    if (!channel) {
        throw ds::test::Failure("broker connect: " + channel.error().message);
    }
    auto sent = ds::sdk::broker_proto::send_request(channel.value(), request);
    if (!sent) {
        throw ds::test::Failure("broker send: " + sent.error().message);
    }
    auto ready = ds::ipc::wait_readable(channel.value().fd(), 5000);
    if (!ready) {
        throw ds::test::Failure("broker reply wait: " + ready.error().message);
    }
    auto response = ds::sdk::broker_proto::receive_response(channel.value());
    if (!response) {
        throw ds::test::Failure("broker receive: " + response.error().message);
    }
    return response.take();
}

bool wait_for_socket(const std::string& path, pid_t pid, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        struct stat st {};
        if (::lstat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) return true;
        int status = 0;
        pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool wait_for_exit(pid_t pid, int* status, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t result = ::waitpid(pid, status, WNOHANG);
        if (result == pid) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

}  // namespace

DS_TEST(broker_proto_roundtrip) {
    using namespace ds::sdk::broker_proto;
    const Op ops[] = {Op::register_service, Op::lookup_service, Op::unregister_service,
                      Op::list_services, Op::ping};
    for (Op op : ops) {
        Request request;
        request.op = op;
        request.name = "org.daysetta.test";
        request.path = "/tmp/daysetta.sock";
        std::vector<std::uint8_t> bytes = encode_request(request);
        DS_CHECK(bytes.size() > 16);
        bool ok = false;
        Request decoded = decode_request(bytes, &ok);
        DS_CHECK(ok);
        DS_CHECK(decoded.op == op);
        DS_CHECK_EQ(decoded.name, request.name);
        DS_CHECK_EQ(decoded.path, request.path);
    }

    Request empty;
    empty.op = Op::ping;
    empty.name.clear();
    empty.path.clear();
    bool ok = false;
    Request decoded_empty = decode_request(encode_request(empty), &ok);
    DS_CHECK(ok);
    DS_CHECK(decoded_empty.name.empty());
    DS_CHECK(decoded_empty.path.empty());

    Request binary;
    binary.op = Op::register_service;
    binary.name = std::string("na\0ve", 5);
    binary.name += "\xC3\xAF\xE2\x9C\x93";
    binary.path = "/tmp/sp ace/\xE2\x9C\x93.sock";
    Request decoded_binary = decode_request(encode_request(binary), &ok);
    DS_CHECK(ok);
    DS_CHECK_EQ(decoded_binary.name.size(), binary.name.size());
    DS_CHECK(decoded_binary.name == binary.name);
    DS_CHECK(decoded_binary.path == binary.path);

    Response response;
    response.code = ds::Code::denied;
    response.detail = "no \"permission\"\nfor\\this";
    response.name = "svc";
    response.path = "/tmp/a\"b\\c.sock";
    response.token = 0x1122334455667788ull;
    response.json = "[{\"name\":\"svc\"}]";
    std::vector<std::uint8_t> response_bytes = encode_response(response);
    Response decoded_response = decode_response(response_bytes, &ok);
    DS_CHECK(ok);
    DS_CHECK(decoded_response.code == response.code);
    DS_CHECK_EQ(decoded_response.detail, response.detail);
    DS_CHECK_EQ(decoded_response.name, response.name);
    DS_CHECK_EQ(decoded_response.path, response.path);
    DS_CHECK_EQ(decoded_response.token, response.token);
    DS_CHECK_EQ(decoded_response.json, response.json);

    Response empty_response;
    Response decoded_empty_response = decode_response(encode_response(empty_response), &ok);
    DS_CHECK(ok);
    DS_CHECK(decoded_empty_response.code == ds::Code::ok);
    DS_CHECK(decoded_empty_response.detail.empty());
    DS_CHECK(decoded_empty_response.name.empty());
    DS_CHECK(decoded_empty_response.path.empty());
    DS_CHECK_EQ(decoded_empty_response.token, 0ull);
    DS_CHECK(decoded_empty_response.json.empty());

    Response long_detail;
    long_detail.detail.assign(65536, 'a');
    Response decoded_long = decode_response(encode_response(long_detail), &ok);
    DS_CHECK(ok);
    DS_CHECK_EQ(decoded_long.detail.size(), 65536u);
}

DS_TEST(broker_proto_malformed) {
    using namespace ds::sdk::broker_proto;
    Request request;
    request.op = Op::register_service;
    request.name = "org.daysetta.test";
    request.path = "/tmp/daysetta.sock";
    std::vector<std::uint8_t> valid = encode_request(request);

    bool ok = true;
    decode_request({}, &ok);
    DS_CHECK(!ok);

    std::vector<std::uint8_t> bad_magic = valid;
    bad_magic[0] ^= 0xff;
    ok = true;
    decode_request(bad_magic, &ok);
    DS_CHECK(!ok);

    std::vector<std::uint8_t> bad_version = valid;
    put_u32(&bad_version, 4, 2);
    ok = true;
    decode_request(bad_version, &ok);
    DS_CHECK(!ok);

    for (std::size_t length = 0; length < valid.size(); ++length) {
        ok = true;
        decode_request(std::span<const std::uint8_t>(valid.data(), length), &ok);
        DS_CHECK(!ok);
    }

    std::vector<std::uint8_t> huge(16 + 4 + 65537, 0);
    std::memcpy(huge.data(), valid.data(), 16);
    put_u32(&huge, 16, 65537);
    ok = true;
    decode_request(huge, &ok);
    DS_CHECK(!ok);

    std::vector<std::uint8_t> enormous = valid;
    put_u32(&enormous, 16, 0xffffffffu);
    ok = true;
    decode_request(enormous, &ok);
    DS_CHECK(!ok);

    std::vector<std::uint8_t> trailing = valid;
    trailing.push_back(0);
    ok = true;
    decode_request(trailing, &ok);
    DS_CHECK(!ok);

    Response response;
    response.code = ds::Code::busy;
    response.detail = "busy";
    std::vector<std::uint8_t> response_bytes = encode_response(response);
    ok = true;
    decode_response({}, &ok);
    DS_CHECK(!ok);
    for (std::size_t length = 0; length < response_bytes.size(); ++length) {
        ok = true;
        decode_response(std::span<const std::uint8_t>(response_bytes.data(), length),
                        &ok);
        DS_CHECK(!ok);
    }
    std::vector<std::uint8_t> response_trailing = response_bytes;
    response_trailing.push_back(1);
    ok = true;
    decode_response(response_trailing, &ok);
    DS_CHECK(!ok);
    std::vector<std::uint8_t> response_status = response_bytes;
    put_u32(&response_status, 12, 99);
    ok = true;
    decode_response(response_status, &ok);
    DS_CHECK(!ok);
}

DS_TEST(broker_live_registry) {
    using namespace ds::sdk::broker_proto;
    TempDir temp{make_temp_dir("broker")};
    const std::string broker_socket = temp.path + "/broker.sock";

    pid_t pid = ::fork();
    DS_CHECK(pid >= 0);
    if (pid == 0) {
        int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) ::close(null_fd);
        }
        ::execl(DS_BROKER_BIN, "daysetta-broker", "--runtime-dir", temp.path.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    BrokerProcess process;
    process.pid = pid;
    DS_CHECK(wait_for_socket(broker_socket, pid, 5000));

    const std::string service_name = "org.daysetta.test";
    const std::string service_socket = temp.path + "/service.sock";
    auto service_listener = ds::ipc::Channel::listen(service_socket);
    if (!service_listener) {
        throw ds::test::Failure("service listen: " + service_listener.error().message);
    }

    Request register_request;
    register_request.op = Op::register_service;
    register_request.name = service_name;
    register_request.path = service_socket;
    Response registered = broker_call(temp.path, register_request);
    DS_CHECK(registered.code == ds::Code::ok);
    DS_CHECK_EQ(registered.name, service_name);
    DS_CHECK(registered.token != 0);
    const std::string canonical_service =
        std::filesystem::weakly_canonical(service_socket).string();
    DS_CHECK_EQ(registered.path, canonical_service);

    Response duplicate = broker_call(temp.path, register_request);
    DS_CHECK(duplicate.code == ds::Code::busy);

    Request bad_name = register_request;
    bad_name.name = "1invalid";
    Response rejected = broker_call(temp.path, bad_name);
    DS_CHECK(rejected.code == ds::Code::invalid);

    Request lookup_request;
    lookup_request.op = Op::lookup_service;
    lookup_request.name = service_name;
    Response looked_up = broker_call(temp.path, lookup_request);
    DS_CHECK(looked_up.code == ds::Code::ok);
    DS_CHECK_EQ(looked_up.token, registered.token);
    DS_CHECK_EQ(looked_up.path, canonical_service);

    Request list_request;
    list_request.op = Op::list_services;
    Response listed = broker_call(temp.path, list_request);
    DS_CHECK(listed.code == ds::Code::ok);
    DS_CHECK(listed.json.find("\"name\":\"" + service_name + "\"") !=
             std::string::npos);
    DS_CHECK(listed.json.find("\"path\":\"" + test_json_escape(canonical_service) +
                              "\"") != std::string::npos);

    const std::string escaped_socket = temp.path + "/a\"b\\c.sock";
    auto escaped_listener = ds::ipc::Channel::listen(escaped_socket);
    if (!escaped_listener) {
        throw ds::test::Failure("escaped listen: " + escaped_listener.error().message);
    }
    Request escaped_request;
    escaped_request.op = Op::register_service;
    escaped_request.name = "org.daysetta.escaped";
    escaped_request.path = escaped_socket;
    Response escaped_registered = broker_call(temp.path, escaped_request);
    DS_CHECK(escaped_registered.code == ds::Code::ok);
    Response escaped_list = broker_call(temp.path, list_request);
    const std::string escaped_canonical =
        std::filesystem::weakly_canonical(escaped_socket).string();
    DS_CHECK(escaped_list.json.find("\"path\":\"" +
                                    test_json_escape(escaped_canonical) + "\"") !=
             std::string::npos);

    Request unregister_request;
    unregister_request.op = Op::unregister_service;
    unregister_request.name = "org.daysetta.escaped";
    Response escaped_removed = broker_call(temp.path, unregister_request);
    DS_CHECK(escaped_removed.code == ds::Code::ok);

    unregister_request.name = service_name;
    Response unregistered = broker_call(temp.path, unregister_request);
    DS_CHECK(unregistered.code == ds::Code::ok);
    Response missing = broker_call(temp.path, lookup_request);
    DS_CHECK(missing.code == ds::Code::not_found);

    // UNVERIFIED: rejecting a different uid needs a second uid (root/CAP_SETUID);
    // this test process only exercises the same-uid path.

    DS_CHECK_EQ(::kill(pid, SIGTERM), 0);
    int status = 0;
    DS_CHECK(wait_for_exit(pid, &status, 5000));
    process.pid = -1;
    DS_CHECK(WIFEXITED(status));
    DS_CHECK_EQ(WEXITSTATUS(status), 0);
    struct stat st {};
    DS_CHECK(::lstat(broker_socket.c_str(), &st) != 0);
    DS_CHECK_EQ(errno, ENOENT);
}
