// Meridian as a UCI engine.
//
// Usage: meridian             reads UCI commands from standard input
//        meridian bench [N]   searches the built-in positions to depth N (default 10)
//        meridian help        prints the commands and the options
//        meridian version     prints the version

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "uci.hpp"

int main(int argc, char **argv) {
    if (argc > 1) {
        const std::string argument = argv[1];

        if (argument == "help" || argument == "--help" || argument == "-h") {
            std::cout << chess::uci::help();
            return 0;
        }

        if (argument == "version" || argument == "--version" || argument == "-v") {
            std::cout << chess::uci::banner() << std::endl;
            return 0;
        }

        if (argument == "bench") {
            chess::uci::Engine engine(std::cin, std::cout);
            engine.bench(argc > 2 ? std::max(1, std::atoi(argv[2])) : 10);
            return 0;
        }

        std::cerr << "Unknown argument: '" << argument << "'. Type meridian help for more "
                  << "information." << std::endl;
        return 1;
    }

    std::cout << chess::uci::banner() << std::endl;
    chess::uci::Engine engine(std::cin, std::cout);
    return engine.run();
}
