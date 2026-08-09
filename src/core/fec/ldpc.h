// ldpc.h - LDPC (Low-Density Parity-Check) codec implementation
#ifndef AUDIOT_LDPC_H
#define AUDIOT_LDPC_H

#include <vector>
#include <cstdint>

namespace audiot {

// LDPC codec using Min-Sum algorithm for decoding
// Supports configurable code length and information length
// Default: n=256, k=128 (rate 1/2 code suitable for real-time processing)
class LDPCCodec {
public:
    // Construct with code parameters
    // n: code length (total bits after encoding)
    // k: information length (input data bits)
    explicit LDPCCodec(int n = 256, int k = 128);
    ~LDPCCodec();

    // Encode data bits to codeword (systematic: info bits + parity bits)
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const;

    // Decode result structure
    struct DecodeResult {
        std::vector<uint8_t> data;
        int iterations_used;
        bool success;
    };

    // Decode received codeword using belief propagation (Min-Sum)
    // Input: soft bits (0.0 = bit 0, 1.0 = bit 1, 0.5 = uncertain)
    DecodeResult decode(const std::vector<uint8_t>& received, int maxIterations = 50) const;

    // Decode from soft values (LLR format)
    // Input: log-likelihood ratios (positive = bit 0, negative = bit 1)
    DecodeResult decodeSoft(const std::vector<float>& llr, int maxIterations = 50) const;

    int codeLength() const;    // n
    int infoLength() const;    // k
    int parityLength() const;  // n - k

private:
    int n_, k_;

    // Sparse parity-check matrix representation
    struct SparseMatrix {
        // Row-major: rows[i] contains column indices of 1s in row i
        std::vector<std::vector<int>> rows;
        // Column-major: cols[j] contains row indices of 1s in column j
        std::vector<std::vector<int>> cols;
        int numRows;
        int numCols;
    };

    SparseMatrix* H_;  // Parity-check matrix

    // Generate parity-check matrix using simple construction
    void generateParityCheckMatrix();

    // Systematic encoding: codeword = [info | parity]
    void systematicEncode(const std::vector<uint8_t>& info, std::vector<uint8_t>& codeword) const;

    // Belief propagation decoding (Min-Sum algorithm)
    std::vector<uint8_t> beliefPropagation(const std::vector<float>& llr, int maxIter, int& iterationsUsed) const;

    // Convert hard bits to LLR
    static std::vector<float> bitsToLLR(const std::vector<uint8_t>& bits, float confidence = 0.9f);
};

} // namespace audiot
#endif // AUDIOT_LDPC_H
