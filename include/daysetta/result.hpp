// DaySetta result type.  No exceptions cross an interface boundary.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace ds {

enum class Code {
    ok = 0,
    invalid = 1,
    unsupported = 2,
    denied = 3,
    not_found = 4,
    busy = 5,
    too_large = 6,
    io = 7,
    protocol = 8,
    state = 9,
    timeout = 10,
};

inline const char* code_name(Code c) {
    switch (c) {
        case Code::ok: return "ok";
        case Code::invalid: return "invalid";
        case Code::unsupported: return "unsupported";
        case Code::denied: return "denied";
        case Code::not_found: return "not_found";
        case Code::busy: return "busy";
        case Code::too_large: return "too_large";
        case Code::io: return "io";
        case Code::protocol: return "protocol";
        case Code::state: return "state";
        case Code::timeout: return "timeout";
    }
    return "unknown";
}

struct Error {
    Code code = Code::invalid;
    std::string message;
};

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : value_(std::move(error)) {}

    static Result ok(T value) { return Result(std::move(value)); }
    static Result fail(Code code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    bool has_value() const { return std::holds_alternative<T>(value_); }
    explicit operator bool() const { return has_value(); }

    T& value() { return std::get<T>(value_); }
    const T& value() const { return std::get<T>(value_); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T&& take() { return std::move(std::get<T>(value_)); }
    const Error& error() const { return std::get<Error>(value_); }
    Code code() const { return has_value() ? Code::ok : error().code; }

private:
    std::variant<T, Error> value_;
};

template <>
class Result<void> {
public:
    Result() : error_{Code::ok, {}} {}
    Result(Error error) : error_(std::move(error)) {}

    static Result ok() { return Result(); }
    static Result fail(Code code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    bool has_value() const { return error_.code == Code::ok; }
    explicit operator bool() const { return has_value(); }
    const Error& error() const { return error_; }
    Code code() const { return error_.code; }

private:
    Error error_;
};

}  // namespace ds
