#include "app/application.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        return d2x::runGame(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "D2X: " << e.what() << '\n';
        return 1;
    }
}
