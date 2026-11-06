#include <iostream>

#include "test.hpp"

namespace ds::test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

bool add(const char* name, std::function<void()> fn) {
    registry().push_back(Case{name, std::move(fn)});
    return true;
}

std::string show(const std::string& value) { return value; }
std::string show(bool value) { return value ? "true" : "false"; }

}  // namespace ds::test

int main(int argc, char** argv) {
    std::string filter;
    if (argc > 1) filter = argv[1];

    int failed = 0;
    int ran = 0;
    for (const auto& test : ds::test::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        ++ran;
        try {
            test.fn();
            std::cout << "ok   " << test.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL " << test.name << ": " << e.what() << "\n";
        } catch (...) {
            ++failed;
            std::cout << "FAIL " << test.name << ": unknown exception\n";
        }
    }
    std::cout << ran - failed << "/" << ran << " tests passed\n";
    return failed == 0 && ran > 0 ? 0 : 1;
}
