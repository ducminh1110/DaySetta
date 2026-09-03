// Tiny dependency-free test runner.  Test files register cases with DS_TEST(name).
#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ds::test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

std::vector<Case>& registry();
bool add(const char* name, std::function<void()> fn);

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}
std::string show(const std::string& value);
std::string show(bool value);

}  // namespace ds::test

#define DS_TEST(name)                                                        \
    static void name();                                                      \
    static const bool name##_registered = ::ds::test::add(#name, name);      \
    static void name()

#define DS_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            throw ::ds::test::Failure(std::string(__FILE__) + ":" +          \
                                      std::to_string(__LINE__) +             \
                                      ": check failed: " #cond);             \
        }                                                                    \
    } while (0)

#define DS_CHECK_EQ(a, b)                                                    \
    do {                                                                     \
        const auto& ds_a = (a);                                              \
        const auto& ds_b = (b);                                              \
        if (!(ds_a == ds_b)) {                                               \
            throw ::ds::test::Failure(std::string(__FILE__) + ":" +          \
                                      std::to_string(__LINE__) +             \
                                      ": expected " #a " == " #b " (" +      \
                                      ::ds::test::show(ds_a) + " vs " +      \
                                      ::ds::test::show(ds_b) + ")");         \
        }                                                                    \
    } while (0)
