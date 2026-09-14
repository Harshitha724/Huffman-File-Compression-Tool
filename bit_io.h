#ifndef BIT_IO_H
#define BIT_IO_H

#include <fstream>
#include <vector>
#include <cstdint>

// Buffers individual bits and flushes a full byte to the output
// stream once 8 bits have accumulated. Call flush() at the end to
// write out any leftover partial byte (padded with 0s).
class BitWriter {
public:
    explicit BitWriter(std::ostream& out) : out_(out), buffer_(0), bitCount_(0) {}

    void writeBit(int bit) {
        buffer_ = (buffer_ << 1) | (bit & 1);
        bitCount_++;
        if (bitCount_ == 8) {
            out_.put(static_cast<char>(buffer_));
            buffer_ = 0;
            bitCount_ = 0;
        }
    }

    // Writes a full byte's worth of raw bits (used for header fields,
    // not for Huffman codes), MSB first.
    void writeByte(uint8_t byte) {
        for (int i = 7; i >= 0; i--) {
            writeBit((byte >> i) & 1);
        }
    }

    // Returns how many padding 0-bits were added to fill the last byte.
    // Must be called exactly once, after all real bits are written.
    int flush() {
        int padding = 0;
        if (bitCount_ > 0) {
            padding = 8 - bitCount_;
            buffer_ <<= padding;
            out_.put(static_cast<char>(buffer_));
            buffer_ = 0;
            bitCount_ = 0;
        }
        return padding;
    }

private:
    std::ostream& out_;
    uint8_t buffer_;
    int bitCount_;
};

// Reads individual bits back out of a byte stream, MSB first,
// mirroring BitWriter's packing order.
class BitReader {
public:
    explicit BitReader(std::istream& in) : in_(in), buffer_(0), bitPos_(8) {}

    // Returns -1 if the underlying stream is exhausted.
    int readBit() {
        if (bitPos_ == 8) {
            int c = in_.get();
            if (c == -1) return -1;
            buffer_ = static_cast<uint8_t>(c);
            bitPos_ = 0;
        }
        int bit = (buffer_ >> (7 - bitPos_)) & 1;
        bitPos_++;
        return bit;
    }

    uint8_t readByte() {
        uint8_t byte = 0;
        for (int i = 0; i < 8; i++) {
            byte = (byte << 1) | readBit();
        }
        return byte;
    }

private:
    std::istream& in_;
    uint8_t buffer_;
    int bitPos_;
};

#endif