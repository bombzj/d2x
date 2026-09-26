#include "app/application.hpp"
#include "app/crash_report.hpp"
#include <iostream>
int main(int argc, char **argv) {
    d2x::installCrashReporting();
    try {
        return d2x::runGame(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "D2X: " << e.what() << '\n';
        d2x::writeFatalErrorReport(e.what());
        return 1;
    } catch (...) {
        std::cerr << "D2X: unknown fatal exception\n";
        d2x::writeFatalErrorReport("Unknown C++ exception; captured after unwinding to main.");
        return 1;
    }
}
