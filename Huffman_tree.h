#ifndef HUFFMAN_TREE_H
#define HUFFMAN_TREE_H

#include <cstdint>

// A node in the Huffman tree.
// Leaf nodes hold a byte value (0-255) and have no children.
// Internal nodes have both children set and their `byte` field is unused.
struct HuffmanNode {
    uint8_t byte;
    uint64_t freq;
    HuffmanNode* left;
    HuffmanNode* right;

    HuffmanNode(uint8_t b, uint64_t f)
        : byte(b), freq(f), left(nullptr), right(nullptr) {}

    HuffmanNode(uint64_t f, HuffmanNode* l, HuffmanNode* r)
        : byte(0), freq(f), left(l), right(r) {}

    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

// Comparator for the min-heap (priority_queue defaults to max-heap,
// so we flip the comparison to get the smallest frequency at the top).
struct CompareNodes {
    bool operator()(const HuffmanNode* a, const HuffmanNode* b) const {
        return a->freq > b->freq;
    }
};

#endif