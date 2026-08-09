// reed_solomon.h - Reed-Solomon forward error correction codec
#ifndef AUDIOT_REED_SOLOMON_H
#define AUDIOT_REED_SOLOMON_H

#include <vector>
#include <cstdint>

namespace audiot {

// Reed-Solomon codec over GF(2^8)
// Irreducible polynomial: x^8 + x^4 + x^3 + x^2 + 1 (0x11D)
// Can correct up to t symbol errors per block
class ReedSolomon {
public:
    // Construct with error-correction capability t
    // Encoded data length = original data + 2*t parity bytes
    explicit ReedSolomon(int t = 8);
    ~ReedSolomon();

    // Encode: append 2*t parity bytes to data
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const;

    // Decode result
    struct DecodeResult {
        std::vector<uint8_t> data;
        int corrected_errors;
        int uncorrectable;
        bool success;
    };

    // Decode: correct errors and return original data
    DecodeResult decode(const std::vector<uint8_t>& encoded) const;

    // Number of parity bytes added during encoding
    int parityBytes() const { return 2 * t_; }

private:
    int t_;  // Error-correction capability (number of correctable symbol errors)

    // GF(2^8) lookup tables
    static uint8_t gf_exp_table_[512];  // exponential (anti-log) table
    static uint8_t gf_log_table_[256];  // logarithm table
    static bool tables_initialized_;

    static void initTables();
    static uint8_t gf_mul(uint8_t a, uint8_t b);
    static uint8_t gf_inv(uint8_t a);
    static uint8_t gf_pow(uint8_t base, int exp);

    // Generate the RS generator polynomial of degree 2*t
    std::vector<uint8_t> generatorPoly() const;

    // Polynomial division: returns remainder of dividend / divisor
    static std::vector<uint8_t> polyMod(std::vector<uint8_t> dividend,
                                         const std::vector<uint8_t>& divisor);

    // Evaluate polynomial at point x in GF(2^8)
    static uint8_t polyEval(const std::vector<uint8_t>& poly, uint8_t x);

    // Berlekamp-Massey algorithm: find error locator polynomial
    static std::vector<uint8_t> berlekampMassey(const std::vector<uint8_t>& syndromes, int n);

    // Chien search: find error positions from error locator polynomial
    static std::vector<int> chienSearch(const std::vector<uint8_t>& errorLocator, int blockLen);

    // Forney algorithm: compute error magnitudes
    static std::vector<uint8_t> forneyAlgorithm(const std::vector<uint8_t>& syndromes,
                                                  const std::vector<uint8_t>& errorLocator,
                                                  const std::vector<int>& errorPositions,
                                                  int blockLen);
};

} // namespace audiot
#endif // AUDIOT_REED_SOLOMON_H
