// Prints the search tree of a position as indented text.
//
// Usage: print_tree OUTPUT.txt [--fen FEN] [--moves e2e4,e7e5] [--config full] [--depth 4]
//                   [--max-ply 3] [--max-children 8] [--min-nodes 0]

#include <fstream>
#include <iostream>

#include "../bench/common.hpp"
#include "chess/search.hpp"
#include "tree_common.hpp"

namespace {

    /**
     * Writes a node and its children, indented two spaces per level.
     *
     * @param out Destination stream.
     * @param node The node to write.
     * @param indent Depth of the node in the printed tree.
     */
    void print(std::ostream &out, const tree::Node &node, int indent) {
        const chess::TraceNode &t = node.trace;

        out << std::string(static_cast<size_t>(indent) * 2, ' ');
        out << (t.ply == 0 ? "root" : node.label) << "  " << tree::score_text(node.white_score)
            << "  d" << t.depth << "  [" << t.alpha << ", " << t.beta << "]";
        if (t.moves_total > 0) {
            out << "  " << t.moves_searched << "/" << t.moves_total << " moves";
        }
        if (t.cutoff) {
            out << "  cutoff";
        }
        if (t.from_tt) {
            out << "  tt";
        }
        if (t.quiescence) {
            out << "  q";
        }
        if (t.null_move) {
            out << "  null-move";
        }

        out << "\n";

        for (const tree::Node *child : node.children) {
            print(out, *child, indent + 1);
        }

        if (node.hidden_children > 0) {
            out << std::string(static_cast<size_t>(indent + 1) * 2, ' ') << "... "
                << node.hidden_children << " more\n";
        }
    }

}  // namespace

int main(int argc, char **argv) {
    using namespace chess;

    if (argc < 2 || std::string(argv[1]).rfind("--", 0) == 0) {
        std::cerr << "usage: print_tree OUTPUT.txt [--fen FEN] [--moves LIST] [--config NAME]\n"
                     "                  [--depth N] [--max-ply N] [--max-children N]\n";
        return 1;
    }

    const std::string output = argv[1];
    const bench::Args args(argc, argv);

    auto root = Position::from_fen(
        args.get("fen", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    if (!root) {
        std::cerr << "invalid FEN\n";
        return 1;
    }

    std::vector<uint64_t> history;

    for (const std::string &uci : tree::split(args.get("moves", ""), ',')) {
        const Move move = move_from_uci(*root, uci);

        if (move.is_null()) {
            std::cerr << "illegal move " << uci << "\n";
            return 1;
        }

        history.push_back(root->key());
        root->make_move(move);
    }

    SearchOptions options =
        SearchOptions::preset(args.get("config", "full"), args.get_int("depth", 4));
    Searcher searcher(options);
    std::vector<TraceNode> trace;
    searcher.set_trace(&trace, args.get_int("max-ply", 3), 200000);

    const SearchResult result = searcher.search(*root, history);

    const tree::Tree built =
        tree::build(*root, trace, static_cast<size_t>(args.get_int("max-children", 8)));

    std::ofstream out(output);
    out << "# " << root->to_fen() << "\n";
    out << "# config " << args.get("config", "full") << ", depth " << options.max_depth << ", "
        << result.stats.nodes << " nodes, best " << move_to_uci(result.best_move) << " "
        << tree::score_text(result.score) << "\n";

    if (built.root != nullptr) {
        print(out, *built.root, 0);
    }

    std::cout << "wrote " << output << " (" << trace.size() << " nodes recorded of "
              << result.stats.nodes << " searched)\n";
    return 0;
}
