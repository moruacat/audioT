// hamming.h - Hamming(7,4) forward error correction codec
#ifndef AUDIOT_HAMMING_H
#define AUDIOT_HAMMING_H

#include <vector>
#include <cstdint>
#include <cstddef>

namespace audiot {

// Hamming(7,4) FEC codec
// Encodes 4 data bits into 7 coded bits (3 parity bits added)
// Can correct single-bit errors and detect double-bit errors
class HammingCodec {
public:
    // Result of decoding operation
    struct DecodeResult {
        std::vector<uint8_t> data;
        int corrected_errors;    // Number of corrected single-bit errors
        int uncorrectable;       // Number of uncorrectable (double-bit) errors
    };

    // Encode: each 4 bits of input -> 7 bits output
    // Input: vector of bits (0 or 1), length must be multiple of 4
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& data);

    // Decode: each 7 bits of input -> 4 bits output (with error correction)
    static DecodeResult decode(const std::vector<uint8_t>& encoded);

private:
    // Encode lookup table: index = 4-bit data, value = 7-bit codeword
    static const uint8_t encode_table[16];

    // Syndrome lookup: index = 7-bit received word syndrome,
    // value = bit position to flip (0=no error, 1-7=bit to correct, 8=uncorrectable)
    static const uint8_t syndrome_table[8];

    // Parity check matrix H (3x7), stored as column vectors
    // H columns: [1,2,3,4,5,6,7] mapped to binary representations
    static const uint8_t parity_check[7];

    // Compute syndrome for a 7-bit codeword
    static uint8_t computeSyndrome(const uint8_t codeword[7]);
};

} // namespace audiot
#endif // AUDIOT_HAMMING_H
