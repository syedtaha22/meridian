// Runs the original engine (commit 9c2ae73) on the benchmark suites and
// writes CSV and JSON in the shapes the benchmarks use.
//
// The original engine only plays Black correctly, so every position it
// searches has Black to move. It finds a move with `GenerateMove`, which is
// the call the desktop game makes on its engine thread.
//
// The `play` mode speaks the small part of UCI that a match needs. It plays the side to
// move from the position it is given, to a fixed depth. White is played by mirroring the
// board, so that the engine always searches as Black, and mirroring its move back.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "headers/Engine/ChessEngine.h"

namespace {

    /**
     * One position of a benchmark suite.
     */
    struct Entry {
        std::string region;
        std::string name;
        std::string fen;
    };

    /**
     * Reads a suite file: tab separated region, name and FEN, with `#` for comments.
     */
    std::vector<Entry> read_suite(const std::string &path) {
        std::ifstream in(path);
        std::vector<Entry> entries;
        std::string line;

        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream fields(line);
            Entry entry;
            std::getline(fields, entry.region, '\t');
            std::getline(fields, entry.name, '\t');
            std::getline(fields, entry.fen, '\t');
            entries.push_back(entry);
        }

        return entries;
    }

    /**
     * Counts the leaf nodes of the move tree of `board` to `depth`. Moves are made the way the
     * game makes them, so that side to move, castling and promotion state change as in play.
     */
    uint64_t perft(ChessBoard &board, int depth) {
        if (depth == 0) {
            return 1;
        }

        int color = board.isCurrentPlayerWhite() ? White : Black;
        std::vector<std::string> moves = board.GetAllPossibleMovesInChessNotation(color);

        if (depth == 1) {
            return moves.size();
        }

        uint64_t nodes = 0;

        for (const std::string &move : moves) {
            ChessBoard next(board);
            std::pair<int, int> squares = ConvertNotation()(move);
            next.MakeCompleteMove(squares.first, squares.second, move);
            nodes += perft(next, depth - 1);
        }

        return nodes;
    }

    /**
     * Splits text at whitespace.
     */
    std::vector<std::string> split_fields(const std::string &text) {
        std::istringstream in(text);
        std::vector<std::string> fields;
        std::string field;

        while (in >> field) {
            fields.push_back(field);
        }

        return fields;
    }

    /**
     * The letter with its case exchanged.
     */
    char swap_case(char c) {
        return std::isupper(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c))
                                                           : static_cast<char>(std::toupper(c));
    }

    /**
     * The position with the colors exchanged and the board flipped top to bottom.
     */
    std::string mirror_fen(const std::string &fen) {
        const std::vector<std::string> f = split_fields(fen);
        std::vector<std::string> ranks;
        std::istringstream rows(f[0]);
        std::string row;

        while (std::getline(rows, row, '/')) {
            ranks.push_back(row);
        }

        std::string placement;

        for (size_t i = ranks.size(); i-- > 0;) {
            for (char c : ranks[i]) {
                placement += swap_case(c);
            }

            placement += i > 0 ? "/" : "";
        }

        std::string castling;

        for (char c : f[2]) {
            castling += c == '-' ? c : swap_case(c);
        }

        std::string ep = f[3];

        if (ep != "-") {
            ep[1] = static_cast<char>('0' + 9 - (ep[1] - '0'));
        }

        std::string out = placement + " " + (f[1] == "w" ? "b" : "w") + " " + castling + " " + ep;

        for (size_t i = 4; i < f.size(); ++i) {
            out += " " + f[i];
        }

        return out;
    }

    /**
     * Answers the UCI commands of a match on standard input. `go depth N` searches the position
     * given by `position fen` to depth N and prints the move.
     */
    int play() {
        std::string fen;
        std::string line;

        while (std::getline(std::cin, line)) {
            const std::vector<std::string> t = split_fields(line);

            if (t.empty()) {
                continue;
            }

            if (t[0] == "uci") {
                std::cout << "id name original-engine\nuciok" << std::endl;
            } else if (t[0] == "isready") {
                std::cout << "readyok" << std::endl;
            } else if (t[0] == "position" && t.size() >= 8 && t[1] == "fen") {
                fen.clear();

                for (size_t i = 2; i < t.size() && t[i] != "moves"; ++i) {
                    fen += (fen.empty() ? "" : " ") + t[i];
                }
            } else if (t[0] == "go") {
                int depth = 3;

                for (size_t i = 1; i + 1 < t.size(); ++i) {
                    if (t[i] == "depth") {
                        depth = std::stoi(t[i + 1]);
                    }
                }

                const bool mirrored = split_fields(fen)[1] == "w";
                ChessBoard board;
                board.initializeBoardFromFEN(mirrored ? mirror_fen(fen) : fen, false);
                board.ComputeOpponentMoves();
                ChessEngine engine(Black, false);
                engine.state.setDepth(depth);
                engine.state.transpostionTable = TranspositionTables();
                std::string move;

                try {
                    move = engine.GenerateMove(board);
                } catch (...) {
                    // The original throws when it loses track of a king; no move comes back.
                    move.clear();
                }

                if (move.size() < 4) {
                    std::cout << "bestmove 0000" << std::endl;
                    continue;
                }

                if (mirrored) {
                    move[1] = static_cast<char>('0' + 9 - (move[1] - '0'));
                    move[3] = static_cast<char>('0' + 9 - (move[3] - '0'));
                }

                for (size_t i = 4; i < move.size(); ++i) {
                    move[i] = static_cast<char>(std::tolower(move[i]));
                }

                std::cout << "bestmove " << move << std::endl;
            } else if (t[0] == "quit") {
                break;
            }
        }

        return 0;
    }

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "play") {
        return play();
    }

    if (argc < 4) {
        std::cerr << "usage: legacy_adapter play\n"
                  << "       legacy_adapter search <suite> <depth>\n"
                  << "       legacy_adapter perft <fen> <depth>\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "perft") {
        ChessBoard board;
        board.initializeBoardFromFEN(argv[2], false);
        board.ComputeOpponentMoves();
        auto start = std::chrono::steady_clock::now();
        uint64_t nodes = perft(board, std::stoi(argv[3]));
        double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "perft\t" << nodes << "\t" << seconds << "\n";

        return 0;
    }

    int depth = std::stoi(argv[3]);

    for (const Entry &entry : read_suite(argv[2])) {
        ChessBoard board;
        board.initializeBoardFromFEN(entry.fen, false);
        board.ComputeOpponentMoves();

        ChessEngine engine(Black, false);
        engine.state.setDepth(depth);
        engine.state.transpostionTable = TranspositionTables();

        auto start = std::chrono::steady_clock::now();
        std::string move = engine.GenerateMove(board);
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::cout << "legacy\t" << depth << "\t" << entry.region << "\t" << entry.name << "\t"
                  << move << "\t" << Heuristics.NumberofMovesLookedAhead << "\t" << ms << "\n";
    }

    return 0;
}
