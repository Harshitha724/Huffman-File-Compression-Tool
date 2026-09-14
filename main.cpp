#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <array>
#include <queue>
#include <string>
#include <cstdint>
#include <cstring>

#include "huffman_tree.h"
#include "bit_io.h"

static const char MAGIC[4] = {'H', 'U', 'F', '1'};

// ---- small helpers for fixed-width header fields ----

static void writeUint32(std::ostream& out, uint32_t value) {
    for (int i = 3; i >= 0; i--) {
        out.put(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

static uint32_t readUint32(std::istream& in) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value = (value << 8) | static_cast<uint8_t>(in.get());
    }
    return value;
}

// ---- tree building ----

static HuffmanNode* buildTree(const std::array<uint64_t, 256>& freq, int& uniqueSymbols) {
    std::priority_queue<HuffmanNode*, std::vector<HuffmanNode*>, CompareNodes> heap;

    uniqueSymbols = 0;
    for (int b = 0; b < 256; b++) {
        if (freq[b] > 0) {
            heap.push(new HuffmanNode(static_cast<uint8_t>(b), freq[b]));
            uniqueSymbols++;
        }
    }

    if (heap.empty()) return nullptr; // empty input file

    // Edge case: exactly one distinct symbol. A single leaf can't produce
    // a traversable code on its own, so wrap it under an internal node.
    // Both children must be real leaves (even a duplicate) so the tree
    // stays fully binary — serializeTree/deserializeTree assume every
    // internal node has exactly two children, and an asymmetric tree
    // (one real child + a null) desyncs the two during decode.
    if (heap.size() == 1) {
        HuffmanNode* onlyLeaf = heap.top();
        HuffmanNode* dummy = new HuffmanNode(onlyLeaf->byte, 0);
        return new HuffmanNode(onlyLeaf->freq, onlyLeaf, dummy);
    }

    while (heap.size() > 1) {
        HuffmanNode* a = heap.top(); heap.pop();
        HuffmanNode* b = heap.top(); heap.pop();
        heap.push(new HuffmanNode(a->freq + b->freq, a, b));
    }
    return heap.top();
}

static void generateCodes(HuffmanNode* node, const std::string& prefix,
                           std::array<std::string, 256>& codes) {
    if (!node) return;
    if (node->isLeaf()) {
        // A root that is itself a leaf (single-symbol file) never reaches
        // here with an empty prefix because buildTree wraps it above.
        codes[node->byte] = prefix.empty() ? "0" : prefix;
        return;
    }
    generateCodes(node->left, prefix + "0", codes);
    generateCodes(node->right, prefix + "1", codes);
}

// ---- tree serialization: preorder, 1 bit marker + 8 bits for leaf value ----

static void serializeTree(HuffmanNode* node, BitWriter& writer) {
    if (node->isLeaf()) {
        writer.writeBit(1);
        writer.writeByte(node->byte);
        return;
    }
    writer.writeBit(0);
    serializeTree(node->left, writer);
    serializeTree(node->right, writer);
}

static HuffmanNode* deserializeTree(BitReader& reader) {
    int marker = reader.readBit();
    if (marker == 1) {
        uint8_t byte = reader.readByte();
        return new HuffmanNode(byte, 0);
    }
    HuffmanNode* left = deserializeTree(reader);
    HuffmanNode* right = deserializeTree(reader);
    return new HuffmanNode(0ULL, left, right);
}

static void freeTree(HuffmanNode* node) {
    if (!node) return;
    freeTree(node->left);
    freeTree(node->right);
    delete node;
}

// ---- compress ----

static bool compressFile(const std::string& inPath, const std::string& outPath) {
    std::ifstream in(inPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file: " << inPath << "\n";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string data = ss.str();

    std::array<uint64_t, 256> freq{};
    for (unsigned char c : data) freq[c]++;

    int uniqueSymbols = 0;
    HuffmanNode* root = buildTree(freq, uniqueSymbols);

    std::array<std::string, 256> codes;
    if (root) generateCodes(root, "", codes);

    // Encode tree + data into an in-memory bit buffer first, so we can
    // compute the final padding count before writing the real header.
    std::ostringstream bitBuffer;
    {
        BitWriter writer(bitBuffer);
        if (root) {
            serializeTree(root, writer);
            for (unsigned char c : data) {
                for (char bit : codes[c]) {
                    writer.writeBit(bit - '0');
                }
            }
        }
        int padding = writer.flush();

        std::ofstream out(outPath, std::ios::binary);
        out.write(MAGIC, 4);
        writeUint32(out, static_cast<uint32_t>(data.size()));
        out.put(static_cast<char>(padding));
        out << bitBuffer.str();
    }

    freeTree(root);

    // Report the compression ratio achieved on this file.
    std::ifstream check(outPath, std::ios::binary | std::ios::ate);
    std::streamsize compressedSize = check.tellg();
    std::streamsize originalSize = static_cast<std::streamsize>(data.size());
    if (originalSize > 0) {
        double ratio = 100.0 * (1.0 - static_cast<double>(compressedSize) / originalSize);
        std::cout << "Original: " << originalSize << " bytes, Compressed: "
                  << compressedSize << " bytes, Ratio: " << ratio << "%\n";
    } else {
        std::cout << "Empty input file compressed (header-only output).\n";
    }
    return true;
}

// ---- decompress ----

static bool decompressFile(const std::string& inPath, const std::string& outPath) {
    std::ifstream in(inPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file: " << inPath << "\n";
        return false;
    }

    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, MAGIC, 4) != 0) {
        std::cerr << "Not a valid .huf file (bad magic number)\n";
        return false;
    }

    uint32_t originalSize = readUint32(in);
    in.get(); // padding count byte — not needed for decode logic itself,
              // since we stop once we've emitted originalSize bytes.

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string bitData = ss.str();

    std::ofstream out(outPath, std::ios::binary);

    if (originalSize == 0) {
        return true; // empty original file, nothing further to do
    }

    std::istringstream bitStream(bitData);
    BitReader reader(bitStream);
    HuffmanNode* root = deserializeTree(reader);

    HuffmanNode* current = root;
    uint32_t decoded = 0;
    while (decoded < originalSize) {
        int bit = reader.readBit();
        if (bit == -1) break; // shouldn't happen on a well-formed file
        current = (bit == 0) ? current->left : current->right;
        if (!current->left && !current->right) {
            out.put(static_cast<char>(current->byte));
            decoded++;
            current = root;
        }
    }

    freeTree(root);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4 || (std::string(argv[1]) != "compress" && std::string(argv[1]) != "decompress")) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " compress   <input.txt> <output.huf>\n"
                  << "  " << argv[0] << " decompress <input.huf> <output.txt>\n";
        return 1;
    }

    std::string mode = argv[1];
    std::string inPath = argv[2];
    std::string outPath = argv[3];

    bool ok = (mode == "compress") ? compressFile(inPath, outPath)
                                    : decompressFile(inPath, outPath);
    return ok ? 0 : 1;
}