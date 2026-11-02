// Exercise the production command runner with this executable as a fake ksud.
#include "susfs_backend.cpp"
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    if (argc > 1) {
        const std::string command = argv[1];
        if (command == "ok") { std::cout << argv[2]; std::cerr << " stderr"; return 0; }
        if (command == "empty") return 0;
        if (command == "fail") { std::cerr << "denied"; return 7; }
        if (command == "overflow") { std::cout << std::string(20000, 'x'); return 0; }
        if (command == "closed") { close(1); close(2); }
        if (command == "descendant") {
            const pid_t child = fork();
            if (child < 0) return 1;
            if (child > 0) return 0; // descendant keeps the command pipe open
        }
        sleep(10);
        return 0;
    }
    using namespace lh;
    std::string output, error;
    auto run = [&](std::vector<std::string> args, int milliseconds = 1000) {
        error.clear();
        return run_ksud(args, output, Clock::now() + std::chrono::milliseconds(milliseconds), error);
    };
    assert(run({"ok", "literal ' $(id); spaces"}));
    assert(output == "literal ' $(id); spaces stderr");
    assert(run({"empty"}) && output.empty());
    assert(!run({"fail"}) && error.find("denied") != std::string::npos);
    assert(!run({"overflow"}) && error.find("exceeds limit") != std::string::npos);
    assert(!run({"ok", "too late"}, -1) && error.find("budget") != std::string::npos);
    for (const char *mode : {"sleep", "closed", "descendant"}) {
        const auto start = Clock::now();
        assert(!run({mode}, 150));
        assert(error.find("timed out") != std::string::npos);
        assert(Clock::now() - start < std::chrono::seconds(2));
    }
    std::cout << "command runner tests passed\n";
}
