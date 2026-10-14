/*
  Meridian, a UCI chess engine
  Copyright (C) 2026 Syed Taha

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
