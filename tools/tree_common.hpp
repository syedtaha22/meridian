#pragma once

// Builds a navigable tree from the nodes a search recorded.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "chess/movegen.hpp"
#include "chess/position.hpp"
#include "chess/search.hpp"

namespace tree {

    /**
     * A recorded node together with the position it stands for.
     */
    struct Node {
        chess::TraceNode trace;
        chess::Position position;  ///< Position after `trace.move` is played.
        std::string label;         ///< Move in algebraic notation, "null", or "root".
        std::vector<Node *> children;
        size_t hidden_children = 0;  ///< Children not shown because of the width limit.
        int white_score = 0;         ///< Node score from White's point of view.
    };

    /**
     * The recorded tree; owns its nodes.
     */
    struct Tree {
        std::vector<std::unique_ptr<Node>> nodes;
        Node *root = nullptr;
    };

    /**
     * @brief Links the nodes recorded in `trace` and attaches positions and labels.
     *
     * @param root_position The position the search started from.
     * @param trace Nodes in the order they were entered.
     * @param max_children Most children kept per node; extra ones are counted as hidden.
     * @return The tree; empty when `trace` is empty.
     */
    inline Tree build(const chess::Position &root_position,
                      const std::vector<chess::TraceNode> &trace, size_t max_children) {
        Tree tree;
        tree.nodes.reserve(trace.size());

        for (const chess::TraceNode &t : trace) {
            auto node = std::make_unique<Node>();
            node->trace = t;
            tree.nodes.push_back(std::move(node));
        }

        for (size_t i = 0; i < tree.nodes.size(); ++i) {
            Node &node = *tree.nodes[i];

            if (node.trace.parent < 0) {
                node.position = root_position;
                node.label = "root";

                if (tree.root == nullptr) {
                    tree.root = &node;
                }

                continue;
            }

            Node &parent = *tree.nodes[static_cast<size_t>(node.trace.parent)];

            if (parent.children.size() >= max_children) {
                ++parent.hidden_children;
                continue;
            }

            parent.children.push_back(&node);
        }

        // Positions need the parent's, so fill them in from the root outwards.
        std::vector<Node *> stack;

        if (tree.root != nullptr) {
            stack.push_back(tree.root);
        }

        while (!stack.empty()) {
            Node *node = stack.back();
            stack.pop_back();

            node->white_score = node->position.side_to_move() == chess::kWhite ? node->trace.score
                                                                               : -node->trace.score;

            for (Node *child : node->children) {
                child->position = node->position;

                if (child->trace.null_move) {
                    child->position.make_null_move();
                    child->label = "null";
                } else {
                    child->label = chess::move_to_san(node->position, child->trace.move);
                    child->position.make_move(child->trace.move);
                }

                stack.push_back(child);
            }
        }

        return tree;
    }

    /**
     * A score as a person writes it: "+0.35", "-1.20" or "M3".
     */
    inline std::string score_text(int score) {
        const chess::Score s = chess::to_display_score(score);
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
     * Splits "a,b,c" into its parts.
     */
    inline std::vector<std::string> split(const std::string &text, char separator) {
        std::vector<std::string> parts;
        std::string part;
        std::istringstream in(text);

        while (std::getline(in, part, separator)) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }

        return parts;
    }

}  // namespace tree
