// hamming.cpp - Hamming(7,4) FEC codec implementation
#include "hamming.h"

namespace audiot {

// Encoding table: 4-bit data -> 7-bit codeword
// Codeword layout: [p1, p2, d1, p3, d2, d3, d4]
// p1 = d1^d2^d4, p2 = d1^d3^d4, p3 = d2^d3^d4
const uint8_t HammingCodec::encode_table[16] = {
    0x00, // 0000 -> 0000000
    0x69, // 0001 -> 1101001
    0x5A, // 0010 -> 1011010
    0x33, // 0011 -> 0110011
    0x3C, // 0100 -> 0111100
    0x55, // 0101 -> 1010101
    0x66, // 0110 -> 1100110
    0x0F, // 0111 -> 0001111
    0x78, // 1000 -> 1111000
    0x11, // 1001 -> 0010001
    0x22, // 1010 -> 0100010
    0x4B, // 1011 -> 1001011
    0x44, // 1100 -> 1000100
    0x2D, // 1101 -> 0101101
    0x1E, // 1110 -> 0011110
    0x77  // 1111 -> 1110111
};

// Parity check column values for each bit position (0-indexed)
// Position 0 (p1): covers bits 0,2,4,6 -> syndrome bit 0
// Position 1 (p2): covers bits 1,2,5,6 -> syndrome bit 1
// Position 2 (d1): syndrome bits 0,1 -> 3
// Position 3 (p3): covers bits 3,4,5,6 -> syndrome bit 2
// Position 4 (d2): syndrome bits 0,2 -> 5
// Position 5 (d3): syndrome bits 1,2 -> 6
// Position 6 (d4): syndrome bits 0,1,2 -> 7
const uint8_t HammingCodec::parity_check[7] = { 1, 2, 3, 4, 5, 6, 7 };

// Syndrome table: syndrome value -> bit position to flip (1-indexed, 0=no error)
// Since syndrome directly gives the 1-indexed bit position for single errors:
const uint8_t HammingCodec::syndrome_table[8] = {
    0, // syndrome 0: no error
    1, // syndrome 1: error at bit 0 (p1)
    2, // syndrome 2: error at bit 1 (p2)
    3, // syndrome 3: error at bit 2 (d1)
    4, // syndrome 4: error at bit 3 (p3)
    5, // syndrome 5: error at bit 4 (d2)
    6, // syndrome 6: error at bit 5 (d3)
    7  // syndrome 7: error at bit 6 (d4)
};

uint8_t HammingCodec::computeSyndrome(const uint8_t codeword[7]) {
    uint8_t s = 0;
    // s0 = c0 ^ c2 ^ c4 ^ c6
    s |= (codeword[0] ^ codeword[2] ^ codeword[4] ^ codeword[6]) ? 1 : 0;
    // s1 = c1 ^ c2 ^ c5 ^ c6
    s |= (codeword[1] ^ codeword[2] ^ codeword[5] ^ codeword[6]) ? 2 : 0;
    // s2 = c3 ^ c4 ^ c5 ^ c6
    s |= (codeword[3] ^ codeword[4] ^ codeword[5] ^ codeword[6]) ? 4 : 0;
    return s;
}

std::vector<uint8_t> HammingCodec::encode(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> output;
    size_t groups = data.size() / 4;
    output.reserve(groups * 7);

    for (size_t g = 0; g < groups; ++g) {
        uint8_t d1 = data[g * 4 + 0] & 1;
        uint8_t d2 = data[g * 4 + 1] & 1;
        uint8_t d3 = data[g * 4 + 2] & 1;
        uint8_t d4 = data[g * 4 + 3] & 1;

        // Compute parity bits to make syndrome zero
        uint8_t p1 = d1 ^ d2 ^ d4;  // c[0]
        uint8_t p2 = d1 ^ d3 ^ d4;  // c[1]
        uint8_t p3 = d2 ^ d3 ^ d4;  // c[3]

        // Codeword layout: [p1, p2, d1, p3, d2, d3, d4]
        output.push_back(p1);  // c[0]
        output.push_back(p2);  // c[1]
        output.push_back(d1);  // c[2]
        output.push_back(p3);  // c[3]
        output.push_back(d2);  // c[4]
        output.push_back(d3);  // c[5]
        output.push_back(d4);  // c[6]
    }

    return output;
}

HammingCodec::DecodeResult HammingCodec::decode(const std::vector<uint8_t>& encoded) {
    DecodeResult result;
    result.corrected_errors = 0;
    result.uncorrectable = 0;

    size_t groups = encoded.size() / 7;
    result.data.reserve(groups * 4);

    for (size_t g = 0; g < groups; ++g) {
        // Read 7-bit codeword
        uint8_t cw[7];
        for (int i = 0; i < 7; ++i) {
            cw[i] = encoded[g * 7 + i] & 1;
        }

        // Compute syndrome
        uint8_t syn = computeSyndrome(cw);

        if (syn != 0) {
            // Check if it's a correctable single-bit error
            // For Hamming(7,4), any non-zero syndrome from 1-7 indicates
            // a single-bit error at the corresponding position
            if (syn <= 7) {
                // Correct the error
                cw[syn - 1] ^= 1;
                ++result.corrected_errors;
            } else {
                // Should not happen for Hamming(7,4) since syndrome is 3 bits (0-7)
                ++result.uncorrectable;
            }
        }

        // Extract data bits: positions 2,4,5,6 (0-indexed) -> d1,d2,d3,d4
        result.data.push_back(cw[2]);
        result.data.push_back(cw[4]);
        result.data.push_back(cw[5]);
        result.data.push_back(cw[6]);
    }

    return result;
}

} // namespace audiot
