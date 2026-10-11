// Runs a search and draws the search tree below the root as an SVG image and an interactive
// HTML page.
//
// Every position is a card with a small board, the evaluation, the depth left, how many of
// its moves were searched, and badges for a beta cutoff, a hash table answer, quiescence and
// a null move. The card border has the color of the evaluation, and a yellow highlight on its
// board marks the move that led to the position. Edges are moves, labelled with the move and
// the number of positions searched below it. The best line, which is the principal
// variation, is drawn in orange, and dashed purple edges lead to a position first reached by
// another path. Only the first moves tried at each position are drawn, and a card says how
// many it leaves out. The HTML page adds panning, zooming, and a larger board with the move
// table of the hovered position. The root is on top and the tree grows downwards.
//
// Usage: visualize_tree OUTPUT_BASE [--fen FEN] [--moves e2e4,e7e5] [--config full]
//                       [--depth 5] [--max-depth 3] [--top-children 4] [--scale 2]
// Writes OUTPUT_BASE.svg and OUTPUT_BASE.html.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../bench/common.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "tree_common.hpp"
#include "tree_page.hpp"

namespace {

    using json = nlohmann::json;
    using namespace chess;

    constexpr double kCardWidth = 92;
    constexpr double kBoardSize = 76;
    constexpr double kHeaderHeight = 26;
    constexpr double kFooterHeight = 46;
    constexpr double kCardHeight = kHeaderHeight + kBoardSize + kFooterHeight;
    constexpr double kLevelGap = kCardHeight + 96;
    constexpr double kSiblingGap = 28;
    constexpr double kMargin = 36;
    constexpr double kTitleHeight = 130;

    constexpr const char *kBestLine = "#e8590c";
    constexpr const char *kMainEdge = "#4a5568";
    constexpr const char *kSideEdge = "#a0aec0";
    constexpr const char *kTransposition = "#8b5cf6";
    constexpr const char *kInk = "#1a202c";
    constexpr const char *kMuted = "#5a6677";
    constexpr const char *kLightSquare = "#edf0f5";
    constexpr const char *kDarkSquare = "#8e9bb2";

    /**
     * White ahead is warm, Black ahead is cool.
     */
    constexpr int kWhiteAhead[3] = {217, 130, 43};
    constexpr int kNeutral[3] = {244, 241, 236};
    constexpr int kBlackAhead[3] = {47, 111, 176};

    /**
     * One position drawn as a card.
     */
    struct Card {
        int id = -1;
        int parent = -1;
        int trace = -1;  // index into the recorded nodes
        Move move;
        std::string label;
        Position position;
        uint64_t key = 0;
        int ply = 0;
        int depth = 0;
        int alpha = 0;
        int beta = 0;
        int white = 0;     // evaluation from White's side, centipawns
        int total = 0;     // legal moves at the position
        int searched = 0;  // moves searched before it returned
        int searches = 1;  // times the position was searched anywhere in the tree
        int subtree = 1;   // recorded nodes below and including it
        bool cutoff = false;
        bool from_tt = false;
        bool quiescence = false;
        bool null_move = false;
        bool terminal = false;
        bool best_line = false;
        bool main = false;  // the child the parent would play
        int hidden = 0;
        std::vector<int> kids;  // cards drawn below it
        double x = 0;           // center of the card
        double y = 0;
    };

    /**
     * A move that connects two cards.
     */
    struct Edge {
        int from = 0;
        int to = 0;
        std::string label;
        int nodes = 0;
        bool best = false;
        bool main = false;
        bool transposition = false;
    };

    /**
     * The text with the characters that XML reserves replaced.
     */
    std::string escape(const std::string &text) {
        std::string out;

        for (char c : text) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:  out += c;
            }
        }

        return out;
    }

    /**
     * The value with a fixed number of decimals.
     */
    std::string fixed(double value, int precision = 1) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return buffer;
    }

    /**
     * The color that lies at `t` from `rgb` towards `other`, as #rrggbb.
     */
    std::string hex_color(const int rgb[3], const int other[3], double t) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x",
                      static_cast<int>(std::lround(rgb[0] + (other[0] - rgb[0]) * t)),
                      static_cast<int>(std::lround(rgb[1] + (other[1] - rgb[1]) * t)),
                      static_cast<int>(std::lround(rgb[2] + (other[2] - rgb[2]) * t)));
        return buffer;
    }

    /**
     * Evaluation to a color: warm when White is ahead, cool when Black is.
     */
    std::string evaluation_color(int white_centipawns, double scale_pawns) {
        const double t = std::clamp(white_centipawns / (scale_pawns * 100.0), -1.0, 1.0);
        return t >= 0 ? hex_color(kNeutral, kWhiteAhead, t) : hex_color(kNeutral, kBlackAhead, -t);
    }

    /**
     * A score from White's side as text: "+0.35", "-1.20" or "M3".
     */
    std::string evaluation_text(int white_centipawns) {
        const Score s = to_display_score(white_centipawns);
        char buffer[32];

        if (s.is_mate) {
            std::snprintf(buffer, sizeof(buffer), "%sM%d", s.value < 0 ? "-" : "",
                          std::abs(s.value));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%+.2f", s.value / 100.0);
        }
        return buffer;
    }

    /**
     * The builder: trace, then cards, then positions on a page.
     */
    class Tree {
    public:
        Tree(const Position &root, const std::vector<TraceNode> &trace, const std::vector<Move> &pv,
             int max_depth, size_t top_children, double scale)
            : root_(root), trace_(trace), pv_(pv), max_depth_(max_depth), top_(top_children),
              scale_(scale) {
            children_.resize(trace_.size());

            for (size_t i = 0; i < trace_.size(); ++i) {
                const int parent = trace_[i].parent;

                if (parent < 0) {
                    continue;
                }

                // A move searched twice (a zero-window search, then a full one) is one move:
                // the later record is the one that decided the result.
                auto &list = children_[static_cast<size_t>(parent)];
                auto same = std::find_if(list.begin(), list.end(), [&](int other) {
                    return trace_[static_cast<size_t>(other)].move == trace_[i].move &&
                           trace_[static_cast<size_t>(other)].null_move == trace_[i].null_move;
                });

                if (same != list.end()) {
                    *same = static_cast<int>(i);
                } else {
                    list.push_back(static_cast<int>(i));
                }
            }

            subtree_.assign(trace_.size(), 0);

            if (!trace_.empty()) {
                count(0);

                // Position of every recorded node, by replaying the moves.
                positions_.resize(trace_.size());
                keys_.resize(trace_.size());
                fill_positions(0, root_);

                for (size_t i = 0; i < trace_.size(); ++i) {
                    if (!positions_[i].has_value()) {
                        continue;
                    }

                    ++searches_[keys_[i]];
                }

                place(0, -1, 0);
                mark_best_line();
                layout();
            }
        }

        const std::vector<Card> &cards() const {
            return cards_;
        }

        const std::vector<Edge> &edges() const {
            return edges_;
        }

        double width() const {
            return width_;
        }

        double height() const {
            return height_;
        }

        double maximum_nodes() const {
            return static_cast<double>(std::max(1, max_subtree_));
        }

        double scale() const {
            return scale_;
        }

        std::vector<int> best_line_cards() const {
            std::vector<int> ids;

            for (const Card &c : cards_) {
                if (c.best_line) {
                    ids.push_back(c.id);
                }
            }
            return ids;
        }

        /**
         * Recorded children of a card, all of them, in the order they were searched.
         */
        json moves_of(const Card &card) const {
            json rows = json::array();
            std::set<int> shown;

            for (int kid : card.kids) {
                shown.insert(cards_[static_cast<size_t>(kid)].trace);
            }

            for (int child : children_[static_cast<size_t>(card.trace)]) {
                const TraceNode &t = trace_[static_cast<size_t>(child)];
                const bool white_moves = card.position.side_to_move() == kWhite;

                // The child's value is from its own mover, the opposite side.
                const int white_value = white_moves ? -t.score : t.score;
                json row;
                row["move"] = t.null_move ? "null" : move_label(card, t.move);
                row["score"] = evaluation_text(white_value);
                row["depth"] = t.depth;
                row["alpha"] = t.alpha;
                row["beta"] = t.beta;
                row["nodes"] = subtree_[static_cast<size_t>(child)];
                row["cutoff"] = t.cutoff;
                row["tt"] = t.from_tt;
                row["quiescence"] = t.quiescence;
                row["drawn"] = shown.count(child) > 0;
                rows.push_back(row);
            }

            return rows;
        }

    private:
        std::string move_label(const Card &parent, Move move) const {
            return move_to_san(parent.position, move);
        }

        int count(int index) {
            int total = 1;

            for (int child : children_[static_cast<size_t>(index)]) {
                total += count(child);
            }

            subtree_[static_cast<size_t>(index)] = total;
            max_subtree_ = std::max(max_subtree_, total);
            return total;
        }

        void fill_positions(int index, const Position &position) {
            positions_[static_cast<size_t>(index)] = position;
            keys_[static_cast<size_t>(index)] = position.key();

            for (int child : children_[static_cast<size_t>(index)]) {
                const TraceNode &t = trace_[static_cast<size_t>(child)];
                Position next = position;

                if (t.null_move) {
                    next.make_null_move();
                } else if (!next.make_move(t.move)) {
                    continue;
                }

                fill_positions(child, next);
            }
        }

        int place(int index, int parent, int depth) {
            const TraceNode &t = trace_[static_cast<size_t>(index)];
            Card card;

            card.id = static_cast<int>(cards_.size());
            card.parent = parent;
            card.trace = index;

            card.move = t.move;
            card.position = *positions_[static_cast<size_t>(index)];
            card.key = keys_[static_cast<size_t>(index)];

            card.ply = t.ply;
            card.depth = t.depth;
            card.alpha = t.alpha;
            card.beta = t.beta;

            card.total = t.moves_total;
            card.searched = t.moves_searched;
            card.cutoff = t.cutoff;
            card.from_tt = t.from_tt;
            card.quiescence = t.quiescence;
            card.null_move = t.null_move;

            card.subtree = subtree_[static_cast<size_t>(index)];
            card.searches = searches_[card.key];

            const bool white_to_move = card.position.side_to_move() == kWhite;
            card.white = white_to_move ? t.score : -t.score;

            if (parent < 0) {
                card.label = "root";
            } else if (t.null_move) {
                card.label = "null";
            } else {
                card.label = move_to_san(cards_[static_cast<size_t>(parent)].position, t.move);
            }

            {
                MoveList legal;
                generate_legal(card.position, legal);
                card.terminal = legal.empty();
            }

            cards_.push_back(card);
            const int id = card.id;
            by_key_.emplace(card.key, id);

            if (depth >= max_depth_) {
                cards_[static_cast<size_t>(id)].hidden =
                    static_cast<int>(children_[static_cast<size_t>(index)].size());
                return id;
            }

            // The principal variation first, then the moves in the order they were tried.
            std::vector<int> order = children_[static_cast<size_t>(index)];
            const size_t ply = static_cast<size_t>(t.ply);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const bool pa = ply < pv_.size() &&
                                trace_[static_cast<size_t>(a)].move == pv_[ply] &&
                                !trace_[static_cast<size_t>(a)].null_move;
                const bool pb = ply < pv_.size() &&
                                trace_[static_cast<size_t>(b)].move == pv_[ply] &&
                                !trace_[static_cast<size_t>(b)].null_move;
                return pa && !pb;
            });
            size_t drawn = 0;

            for (int child : order) {
                if (drawn >= top_) {
                    break;
                }
                if (!positions_[static_cast<size_t>(child)].has_value()) {
                    continue;
                }

                const uint64_t key = keys_[static_cast<size_t>(child)];
                ++drawn;
                auto seen = by_key_.find(key);

                if (seen != by_key_.end() && seen->second != id) {
                    Edge edge;
                    edge.from = id;
                    edge.to = seen->second;
                    edge.transposition = true;

                    const TraceNode &c = trace_[static_cast<size_t>(child)];
                    edge.label = c.null_move ? "null" : move_to_san(card.position, c.move);
                    edge.nodes = subtree_[static_cast<size_t>(child)];
                    edges_.push_back(edge);
                    continue;
                }

                const int kid = place(child, id, depth + 1);
                cards_[static_cast<size_t>(id)].kids.push_back(kid);
                Edge edge;
                edge.from = id;
                edge.to = kid;
                edge.label = cards_[static_cast<size_t>(kid)].label;
                edge.nodes = cards_[static_cast<size_t>(kid)].subtree;
                edges_.push_back(edge);
            }

            const int all = static_cast<int>(children_[static_cast<size_t>(index)].size());
            cards_[static_cast<size_t>(id)].hidden = std::max(0, all - static_cast<int>(drawn));

            // The child the parent would play: the lowest value for the side that moves next.
            int best = -1;
            int best_score = 0;

            for (int kid : cards_[static_cast<size_t>(id)].kids) {
                const TraceNode &c =
                    trace_[static_cast<size_t>(cards_[static_cast<size_t>(kid)].trace)];
                if (best < 0 || c.score < best_score) {
                    best = kid;
                    best_score = c.score;
                }
            }

            if (best >= 0) {
                cards_[static_cast<size_t>(best)].main = true;
            }
            return id;
        }

        void mark_best_line() {
            int current = 0;
            cards_[0].best_line = true;

            for (size_t i = 0; i < pv_.size(); ++i) {
                int next = -1;

                for (int kid : cards_[static_cast<size_t>(current)].kids) {
                    const Card &k = cards_[static_cast<size_t>(kid)];

                    if (!k.null_move && k.move == pv_[i]) {
                        next = kid;
                        break;
                    }
                }

                if (next < 0) {
                    break;
                }

                cards_[static_cast<size_t>(next)].best_line = true;
                current = next;
            }

            for (Edge &edge : edges_) {
                edge.best = !edge.transposition &&
                            cards_[static_cast<size_t>(edge.from)].best_line &&
                            cards_[static_cast<size_t>(edge.to)].best_line;
                edge.main = !edge.transposition && cards_[static_cast<size_t>(edge.to)].main;
            }
        }

        double span(int id) {
            Card &c = cards_[static_cast<size_t>(id)];
            const double leaf = kCardWidth + kSiblingGap;

            if (c.kids.empty()) {
                return leaf;
            }

            double total = 0;

            for (int kid : c.kids) {
                total += span(kid);
            }
            return std::max(total, leaf);
        }

        void assign(int id, double begin, int level) {
            Card &c = cards_[static_cast<size_t>(id)];
            const double width = span(id);
            const double center = begin + width / 2;

            c.x = center;
            c.y = kTitleHeight + kMargin + kCardHeight / 2 + level * kLevelGap;

            double cursor = begin;

            for (int kid : c.kids) {
                assign(kid, cursor, level + 1);
                cursor += span(kid);
            }
        }

        void layout() {
            assign(0, kMargin, 0);
            double max_x = 0;
            double max_y = 0;

            for (const Card &c : cards_) {
                max_x = std::max(max_x, c.x + kCardWidth / 2);
                max_y = std::max(max_y, c.y + kCardHeight / 2);
            }

            width_ = std::max(max_x + kMargin, 900.0);
            height_ = max_y + kMargin;
        }

        Position root_;
        const std::vector<TraceNode> &trace_;
        std::vector<Move> pv_;
        int max_depth_;
        size_t top_;
        double scale_;
        std::vector<std::vector<int>> children_;
        std::vector<int> subtree_;
        std::vector<std::optional<Position>> positions_;
        std::vector<uint64_t> keys_;
        std::unordered_map<uint64_t, int> searches_;
        std::unordered_map<uint64_t, int> by_key_;
        std::vector<Card> cards_;
        std::vector<Edge> edges_;
        int max_subtree_ = 1;
        double width_ = 0;
        double height_ = 0;
    };

    // ---- drawing -------------------------------------------------------------------------

    /**
     * The chess symbol of a piece type.
     */
    const char *glyph(PieceType type) {
        static const char *kGlyphs[6] = {"♟︎", "♞︎", "♝︎", "♜︎", "♛︎", "♚︎"};
        return kGlyphs[type];
    }

    /**
     * A board of 8 by 8 squares with pieces, White at the bottom, at (x, y).
     */
    std::string board_svg(const Position &pos, const Move *last, double x, double y, double size) {
        std::ostringstream out;
        const double cell = size / 8;

        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const Square sq = make_square(col, 7 - row);
                const bool light = (row + col) % 2 == 0;
                out << "<rect x=\"" << fixed(x + col * cell, 2) << "\" y=\""
                    << fixed(y + row * cell, 2) << "\" width=\"" << fixed(cell, 2) << "\" height=\""
                    << fixed(cell, 2) << "\" fill=\"" << (light ? kLightSquare : kDarkSquare)
                    << "\"/>";
                if (last != nullptr && (last->from() == sq || last->to() == sq)) {
                    out << "<rect x=\"" << fixed(x + col * cell, 2) << "\" y=\""
                        << fixed(y + row * cell, 2) << "\" width=\"" << fixed(cell, 2)
                        << "\" height=\"" << fixed(cell, 2)
                        << "\" fill=\"#f6c945\" fill-opacity=\"0.7\"/>";
                }
            }
        }

        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const Piece p = pos.piece_on(make_square(col, 7 - row));

                if (p == kNoPiece) {
                    continue;
                }

                const bool white = piece_color(p) == kWhite;
                out << "<text x=\"" << fixed(x + (col + 0.5) * cell, 2) << "\" y=\""
                    << fixed(y + (row + 0.5) * cell + cell * 0.34, 2) << "\" font-size=\""
                    << fixed(cell * 0.98, 2) << "\" text-anchor=\"middle\" fill=\""
                    << (white ? "#ffffff" : "#1c1c1c") << "\" stroke=\""
                    << (white ? "#1c1c1c" : "#ffffff") << "\" stroke-width=\""
                    << fixed(cell * 0.055, 2) << "\" paint-order=\"stroke\" class=\"glyph\">"
                    << glyph(piece_type(p)) << "</text>";
            }
        }

        return out.str();
    }

    /**
     * A rounded label centered on a point.
     */
    std::string pill(double cx, double cy, const std::string &text, bool best) {
        const double width = std::max(24.0, static_cast<double>(text.size()) * 6.4 + 14);
        std::ostringstream out;
        out << "<rect x=\"" << fixed(cx - width / 2) << "\" y=\"" << fixed(cy - 9) << "\" width=\""
            << fixed(width) << "\" height=\"18\" rx=\"9\" fill=\"" << (best ? kBestLine : "#ffffff")
            << "\" stroke=\"" << (best ? kBestLine : "#cbd5e0") << "\"/>" << "<text x=\""
            << fixed(cx) << "\" y=\"" << fixed(cy + 4) << "\" text-anchor=\"middle\" "
            << "font-size=\"11\" fill=\"" << (best ? "#ffffff" : "#2d3748") << "\">" << escape(text)
            << "</text>";
        return out.str();
    }

    /**
     * The value with a comma between groups of three digits.
     */
    std::string number(int value) {
        std::string digits = std::to_string(std::abs(value));
        std::string out;
        int count = 0;

        for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
            if (count > 0 && count % 3 == 0) {
                out.insert(out.begin(), ',');
            }

            out.insert(out.begin(), *it);
            ++count;
        }

        return out;
    }

    /**
     * The curve between two cards, with its move label and node count.
     */
    std::string edge_svg(const Tree &tree, const Edge &edge, bool with_label = true) {
        const auto &cards = tree.cards();
        const Card &a = cards[static_cast<size_t>(edge.from)];
        const Card &b = cards[static_cast<size_t>(edge.to)];

        const double x0 = a.x;
        const double x1 = b.x;
        double y0 = a.y + kCardHeight / 2;
        double y1 = b.y - kCardHeight / 2;

        // A transposition edge may run upwards, to a card already drawn.
        if (y1 <= y0) {
            y0 = a.y - kCardHeight / 2;
            y1 = b.y + kCardHeight / 2;
        }

        const double mid = (y0 + y1) / 2;

        const double c0x = x0;
        const double c0y = mid;
        const double c1x = x1;
        const double c1y = mid;

        const double mx = (x0 + 3 * c0x + 3 * c1x + x1) / 8;
        const double my = (y0 + 3 * c0y + 3 * c1y + y1) / 8;

        const double weight =
            1.6 + 3.4 * std::log(1.0 + edge.nodes) / std::log(1.0 + tree.maximum_nodes());
        const char *color = edge.transposition ? kTransposition
                            : edge.best        ? kBestLine
                            : edge.main        ? kMainEdge
                                               : kSideEdge;
        std::ostringstream out;
        out << "<g class=\"edge" << (edge.best ? " best" : "") << "\" data-from=\"" << edge.from
            << "\" data-to=\"" << edge.to << "\"><path d=\"M" << fixed(x0) << ' ' << fixed(y0)
            << " C" << fixed(c0x) << ' ' << fixed(c0y) << ' ' << fixed(c1x) << ' ' << fixed(c1y)
            << ' ' << fixed(x1) << ' ' << fixed(y1) << "\" fill=\"none\" stroke=\"" << color
            << "\" stroke-width=\"" << fixed(edge.best ? std::max(weight, 4.0) : weight) << "\""
            << (edge.transposition ? " stroke-dasharray=\"7 5\"" : "")
            << " stroke-linecap=\"round\" stroke-opacity=\""
            << (edge.best || edge.main ? "1" : "0.85") << "\"/>";
        if (with_label) {
            std::string text = edge.label;

            if (!edge.transposition) {
                text += " " + number(edge.nodes);
            }

            out << "<g class=\"pill\">" << pill(mx, my, text, edge.best) << "</g>";
        }

        out << "</g>";
        return out.str();
    }

    /**
     * One card: the board, the score, the depth and the badges.
     */
    std::string card_svg(const Tree &tree, const Card &c) {
        const double left = c.x - kCardWidth / 2;
        const double top = c.y - kCardHeight / 2;
        const std::string border = evaluation_color(c.white, tree.scale());
        const bool white_to_move = c.position.side_to_move() == kWhite;
        std::ostringstream out;
        out << "<g class=\"node\" id=\"n" << c.id << "\" data-id=\"" << c.id << "\">";
        out << "<rect class=\"card\" x=\"" << fixed(left) << "\" y=\"" << fixed(top)
            << "\" width=\"" << fixed(kCardWidth) << "\" height=\"" << fixed(kCardHeight)
            << "\" rx=\"8\" fill=\"" << (c.quiescence ? "#f4f4f5" : "#ffffff") << "\" stroke=\""
            << border << "\" stroke-width=\"3\"/>";
        // Header: who is to move, and the move that led here.
        out << "<circle cx=\"" << fixed(left + 12) << "\" cy=\"" << fixed(top + 13)
            << "\" r=\"5\" fill=\"" << (white_to_move ? "#ffffff" : "#1c1c1c")
            << "\" stroke=\"#1c1c1c\" stroke-width=\"1.2\"/>";
        out << "<text x=\"" << fixed(left + 22) << "\" y=\"" << fixed(top + 17)
            << "\" font-size=\"11\" fill=\"" << kInk << "\" font-weight=\"600\">" << escape(c.label)
            << "</text>";
        const Move *last = (c.parent >= 0 && !c.null_move) ? &c.move : nullptr;
        out << board_svg(c.position, last, left + (kCardWidth - kBoardSize) / 2,
                         top + kHeaderHeight, kBoardSize);
        // Footer: the evaluation, the depth and the moves searched, and the badges.
        const double base = top + kHeaderHeight + kBoardSize;
        out << "<text x=\"" << fixed(c.x) << "\" y=\"" << fixed(base + 15)
            << "\" text-anchor=\"middle\" font-size=\"12\" font-weight=\"700\" fill=\"" << kInk
            << "\">" << evaluation_text(c.white) << "</text>";
        std::string detail = c.quiescence ? "quiescence" : "d" + std::to_string(c.depth);

        if (c.total > 0) {
            detail += "  " + std::to_string(c.searched) + "/" + std::to_string(c.total);
        }

        out << "<text x=\"" << fixed(c.x) << "\" y=\"" << fixed(base + 28)
            << "\" text-anchor=\"middle\" font-size=\"9.5\" fill=\"" << kMuted << "\">"
            << escape(detail) << "</text>";
        std::string badges;
        auto add = [&badges](const char *text) {
            badges += (badges.empty() ? "" : " · ");
            badges += text;
        };
        if (c.terminal) {
            add(c.position.in_check() ? "mate" : "stalemate");
        }
        if (c.cutoff) {
            add("cutoff");
        }
        if (c.from_tt) {
            add("hash");
        }
        if (c.null_move) {
            add("null move");
        }
        if (c.hidden > 0) {
            badges =
                "+" + std::to_string(c.hidden) + " more" + (badges.empty() ? "" : " · " + badges);
        }

        out << "<text x=\"" << fixed(c.x) << "\" y=\"" << fixed(base + 40)
            << "\" text-anchor=\"middle\" font-size=\"8.5\" fill=\"" << kMuted << "\">"
            << escape(badges) << "</text>";
        out << "</g>";
        return out.str();
    }

    /**
     * The legend of the colors, with its top left corner at (x, y).
     */
    std::string legend_svg(double scale, double x, double y) {
        std::ostringstream out;
        out << "<rect x=\"" << fixed(x - 14) << "\" y=\"" << fixed(y - 14)
            << "\" width=\"580\" height=\"116\" rx=\"8\" fill=\"#ffffff\" stroke=\"#cbd5e0\"/>";
        out << "<g font-size=\"12\" fill=\"" << kInk << "\">";
        out << "<text x=\"" << fixed(x) << "\" y=\"" << fixed(y + 4)
            << "\" font-weight=\"600\">Card border and score: evaluation from White's side</text>";

        const int steps = 9;

        for (int i = 0; i < steps; ++i) {
            const double t = -1 + 2.0 * i / (steps - 1);
            const std::string color =
                t >= 0 ? hex_color(kNeutral, kWhiteAhead, t) : hex_color(kNeutral, kBlackAhead, -t);
            out << "<rect x=\"" << fixed(x + i * 36) << "\" y=\"" << fixed(y + 14)
                << "\" width=\"36\" height=\"12\" fill=\"" << color << "\"/>";
        }

        out << "<text x=\"" << fixed(x) << "\" y=\"" << fixed(y + 44) << "\" fill=\"" << kMuted
            << "\">-" << fixed(scale, 1) << " Black ahead</text>";
        out << "<text x=\"" << fixed(x + 162) << "\" y=\"" << fixed(y + 44) << "\" fill=\""
            << kMuted << "\">0</text>";
        out << "<text x=\"" << fixed(x + 252) << "\" y=\"" << fixed(y + 44) << "\" fill=\""
            << kMuted << "\">+" << fixed(scale, 1) << " White ahead</text>";

        out << "<line x1=\"" << fixed(x) << "\" y1=\"" << fixed(y + 70) << "\" x2=\""
            << fixed(x + 40) << "\" y2=\"" << fixed(y + 70) << "\" stroke=\"" << kBestLine
            << "\" stroke-width=\"5\" stroke-linecap=\"round\"/><text x=\"" << fixed(x + 50)
            << "\" y=\"" << fixed(y + 74) << "\">best line (principal variation)</text>";

        out << "<line x1=\"" << fixed(x) << "\" y1=\"" << fixed(y + 92) << "\" x2=\""
            << fixed(x + 40) << "\" y2=\"" << fixed(y + 92) << "\" stroke=\"" << kTransposition
            << "\" stroke-width=\"2\" stroke-dasharray=\"7 5\"/><text x=\"" << fixed(x + 50)
            << "\" y=\"" << fixed(y + 96) << "\">position first reached by another path</text>";

        out << "<rect x=\"" << fixed(x + 360) << "\" y=\"" << fixed(y + 60)
            << "\" width=\"14\" height=\"14\" "
            << "fill=\"#f6c945\" fill-opacity=\"0.7\" stroke=\"#c9a227\"/><text x=\""
            << fixed(x + 382) << "\" y=\"" << fixed(y + 72) << "\">last move</text>";

        out << "<text x=\"" << fixed(x + 360) << "\" y=\"" << fixed(y + 96) << "\" fill=\""
            << kMuted << "\">cutoff: a move reached beta</text>";
        out << "</g>";

        return out.str();
    }

    /**
     * The tree as SVG, with or without the legend and the grid.
     */
    std::string svg_markup(const Tree &tree, bool header, bool grid) {
        std::ostringstream out;
        const double height = tree.height();

        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << fixed(tree.width())
            << ' ' << fixed(height) << "\" width=\"" << fixed(tree.width()) << "\" height=\""
            << fixed(height)
            << "\" font-family=\"system-ui, -apple-system, 'Segoe UI', 'DejaVu Sans', "
               "sans-serif\">\n";

        // No page background, so that the image takes the color of the page it is shown on. The
        // legend has its own white panel and stays readable on both light and dark pages.
        if (grid) {
            out << "<g id=\"grid\" stroke=\"#edf2f7\" stroke-width=\"1\">";

            for (double x = 0; x < tree.width(); x += 60) {
                out << "<line x1=\"" << fixed(x, 0) << "\" y1=\"0\" x2=\"" << fixed(x, 0)
                    << "\" y2=\"" << fixed(height, 0) << "\"/>";
            }

            for (double y = 0; y < height; y += 60) {
                out << "<line x1=\"0\" y1=\"" << fixed(y, 0) << "\" x2=\"" << fixed(tree.width(), 0)
                    << "\" y2=\"" << fixed(y, 0) << "\"/>";
            }

            out << "</g>\n";
        }

        if (header) {
            out << legend_svg(tree.scale(), 46, 22);
        }

        for (const Edge &edge : tree.edges()) {
            out << edge_svg(tree, edge) << "\n";
        }

        for (const Card &c : tree.cards()) {
            out << card_svg(tree, c) << "\n";
        }

        out << "</svg>\n";
        return out.str();
    }

    /**
     * The text with every occurrence of `from` replaced by `to`.
     */
    std::string replace_all(std::string text, const std::string &from, const std::string &to) {
        size_t at = 0;

        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, from.size(), to);
            at += to.size();
        }
        return text;
    }

    /**
     * The moves that lead to the root position, shortened when they are many.
     */
    std::string describe(const std::vector<std::string> &moves, const Position &start) {
        if (moves.empty()) {
            return start.to_fen() == Position().to_fen() ? "the initial position"
                                                         : "a given position";
        }
        if (moves.size() <= 8) {
            std::string text;

            for (const auto &m : moves) {
                text += (text.empty() ? "" : " ") + m;
            }
            return text;
        }

        return std::to_string(moves.size()) + " moves, ending " + moves[moves.size() - 3] + " " +
               moves[moves.size() - 2] + " " + moves.back();
    }

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2 || std::string(argv[1]).rfind("--", 0) == 0) {
        std::cerr
            << "usage: visualize_tree OUTPUT_BASE [--fen FEN] [--moves LIST] [--config NAME]\n"
               "                      [--depth N] [--max-depth N] [--top-children N]\n"
               "                      [--scale PAWNS]\n";
        return 1;
    }

    const std::string base = argv[1];
    const bench::Args args(argc, argv);

    auto start = Position::from_fen(args.get("fen", Position().to_fen()));

    if (!start) {
        std::cerr << "invalid FEN\n";
        return 1;
    }

    Position root = *start;
    std::vector<uint64_t> history;
    std::vector<std::string> played;

    for (const std::string &uci : tree::split(args.get("moves", ""), ',')) {
        const Move move = move_from_uci(root, uci);

        if (move.is_null()) {
            std::cerr << "illegal move " << uci << "\n";
            return 1;
        }

        played.push_back(move_to_san(root, move));
        history.push_back(root.key());
        root.make_move(move);
    }

    const std::string config = args.get("config", "full");
    SearchOptions options = SearchOptions::preset(config, args.get_int("depth", 5));
    Searcher searcher(options);
    std::vector<TraceNode> trace;
    const int max_depth = args.get_int("max-depth", 3);

    // The recording keeps one ply more than is drawn, so that the cards at the last drawn
    // level can say how many moves they leave out.
    searcher.set_trace(&trace, max_depth + 1, 400000);
    const SearchResult result = searcher.search(root, history);

    if (trace.empty()) {
        std::cerr << "nothing was recorded; the position may have no legal moves\n";
        return 1;
    }

    const size_t top = static_cast<size_t>(args.get_int("top-children", 4));
    const double scale = std::stod(args.get("scale", "2"));
    Tree built(root, trace, result.pv, max_depth, top, scale);

    // The principal variation in algebraic notation.
    std::string line;
    {
        Position walk = root;

        for (const Move move : result.pv) {
            line += (line.empty() ? "" : " ") + move_to_san(walk, move);
            walk.make_move(move);
        }
    }

    const std::string title = "Alpha-beta tree after " + describe(played, *start);
    std::ostringstream subtitle;
    subtitle << result.stats.nodes << " nodes at depth " << options.max_depth << " (" << config
             << "), " << (root.side_to_move() == kWhite ? "White" : "Black")
             << " to move, chosen move " << move_to_san(root, result.best_move) << ' '
             << evaluation_text(root.side_to_move() == kWhite ? result.score : -result.score)
             << "\n"
             << "Best line: " << (line.empty() ? "none" : line) << "\n"
             << "First " << top << " moves tried at each position, down to depth " << max_depth;

    const std::string svg = svg_markup(built, true, false);
    std::ofstream(base + ".svg") << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" << svg;

    // Data for the page: one entry per card.
    json nodes = json::array();

    for (const Card &c : built.cards()) {
        json n;
        n["id"] = c.id;
        n["parent"] = c.parent >= 0 ? json(c.parent) : json(nullptr);
        n["label"] = c.label;
        n["x"] = c.x;
        n["y"] = c.y;
        std::string board;

        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const Piece p = c.position.piece_on(make_square(col, 7 - row));
                board += p == kNoPiece ? '.' : "PNBRQKpnbrqk"[p];
            }
        }

        n["board"] = board;

        if (c.parent >= 0 && !c.null_move) {
            auto index = [](Square s) { return (7 - rank_of(s)) * 8 + file_of(s); };
            n["last"] = json::array({index(c.move.from()), index(c.move.to())});
        }

        n["mover"] = c.position.side_to_move() == kWhite ? "w" : "b";
        n["score"] = evaluation_text(c.white);
        n["depth"] = c.depth;
        n["alpha"] = c.alpha;
        n["beta"] = c.beta;
        n["searched"] = c.searched;
        n["total"] = c.total;
        n["nodes"] = c.subtree;
        n["cutoff"] = c.cutoff;
        n["tt"] = c.from_tt;
        n["quiescence"] = c.quiescence;
        n["terminal"] = c.terminal;
        n["moves"] = built.moves_of(c);
        nodes.push_back(n);
    }

    json data;
    data["nodes"] = nodes;
    std::string payload = replace_all(data.dump(), "</", "<\\/");

    const std::string page_svg = svg_markup(built, false, true);

    std::ostringstream legend;
    legend
        << "<div class=\"legend\"><div class=\"ramp\"><div class=\"caption\">Card border and "
           "score: "
           "evaluation from White's side</div><div class=\"bar\"></div><div class=\"ticks\">"
           "<span>-"
        << fixed(scale, 1) << " Black ahead</span><span>0</span><span>+" << fixed(scale, 1)
        << " White ahead</span></div></div><div class=\"keys\">"
           "<div class=\"key\"><i class=\"best\"></i>best line (principal variation)</div>"
           "<div class=\"key\"><i class=\"dash\"></i>position first reached by another path</div>"
           "<div class=\"key\"><i class=\"last\"></i>last move</div></div></div>";

    std::string subtitle_html;
    {
        std::istringstream lines(subtitle.str());
        std::string text;

        while (std::getline(lines, text)) {
            subtitle_html += (subtitle_html.empty() ? "" : "<br>") + escape(text);
        }
    }

    std::string page = kTreePage;

    page = replace_all(page, "__TITLE__", escape(title));
    page = replace_all(page, "__SUBTITLE__", subtitle_html);
    page = replace_all(page, "__LEGEND__", legend.str());
    page = replace_all(page, "__SVG__", page_svg);
    page = replace_all(page, "__DATA__", payload);

    std::ofstream(base + ".html") << page;

    std::cout << "wrote " << base << ".svg and " << base << ".html (" << built.cards().size()
              << " cards drawn, " << trace.size() << " nodes recorded of " << result.stats.nodes
              << " searched)\n";
    return 0;
}
