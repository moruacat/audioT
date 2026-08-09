// reed_solomon.cpp - Reed-Solomon FEC implementation over GF(2^8)
#include "reed_solomon.h"
#include <algorithm>
#include <cstring>

namespace audiot {

// Static member initialization
uint8_t ReedSolomon::gf_exp_table_[512] = {0};
uint8_t ReedSolomon::gf_log_table_[256] = {0};
bool ReedSolomon::tables_initialized_ = false;

// Initialize GF(2^8) lookup tables
// Primitive polynomial: x^8 + x^4 + x^3 + x^2 + 1 = 0x11D
void ReedSolomon::initTables() {
    if (tables_initialized_) return;

    const uint8_t prim_poly = 0x1D; // Lower 8 bits of 0x11D (x^8 is implicit)
    uint8_t x = 1;
    for (int i = 0; i < 255; ++i) {
        gf_exp_table_[i] = x;
        gf_log_table_[x] = static_cast<uint8_t>(i);
        // Multiply by alpha (= 2) in GF(2^8): shift left one bit and,
        // if the top bit overflows the byte, reduce modulo the
        // primitive polynomial. The overflow flag must be read from x
        // BEFORE the shift: after uint8 truncation (e.g. 128<<1 = 0)
        // the high bit is gone and the reduction would never fire.
        const bool overflow = (x & 0x80) != 0;
        x = static_cast<uint8_t>(x << 1);
        if (overflow) {
            x ^= prim_poly;
        }
    }
    // alpha^255 = 1 (field has 255 non-zero elements)
    gf_exp_table_[255] = gf_exp_table_[0]; // = 1

    // Extend exp table for easy modular access (up to index 511)
    for (int i = 256; i < 512; ++i) {
        gf_exp_table_[i] = gf_exp_table_[i - 255];
    }

    gf_log_table_[0] = 0; // Convention: log(0) is undefined, set to 0

    tables_initialized_ = true;
}

ReedSolomon::ReedSolomon(int t) : t_(t) {
    initTables();
}

ReedSolomon::~ReedSolomon() {}

// Multiply two elements in GF(2^8)
uint8_t ReedSolomon::gf_mul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    int log_a = gf_log_table_[a];
    int log_b = gf_log_table_[b];
    int log_result = log_a + log_b;
    if (log_result >= 255) log_result -= 255;
    return gf_exp_table_[log_result];
}

// Multiplicative inverse in GF(2^8)
uint8_t ReedSolomon::gf_inv(uint8_t a) {
    if (a == 0) return 0; // Undefined, but return 0 for safety
    int log_a = gf_log_table_[a];
    int log_inv = 255 - log_a;
    if (log_inv >= 255) log_inv -= 255;
    return gf_exp_table_[log_inv];
}

// Power in GF(2^8)
uint8_t ReedSolomon::gf_pow(uint8_t base, int exp) {
    if (base == 0) return 0;
    if (exp == 0) return 1;
    int log_base = gf_log_table_[base];
    long long log_result = (static_cast<long long>(log_base) * exp) % 255;
    if (log_result < 0) log_result += 255;
    return gf_exp_table_[static_cast<int>(log_result)];
}

// Generate the RS generator polynomial of degree 2*t
// g(x) = (x - alpha^0)(x - alpha^1)...(x - alpha^(2t-1))
std::vector<uint8_t> ReedSolomon::generatorPoly() const {
    int npar = 2 * t_;
    // Start with g(x) = 1
    std::vector<uint8_t> gen;
    gen.push_back(1);

    for (int i = 0; i < npar; ++i) {
        // Multiply gen by (x - alpha^i)
        std::vector<uint8_t> new_gen(gen.size() + 1, 0);
        uint8_t root = gf_exp_table_[i]; // alpha^i

        for (size_t j = 0; j < gen.size(); ++j) {
            // gen[j] * x
            new_gen[j] ^= gen[j];
            // gen[j] * (-alpha^i) = gen[j] * alpha^i (in GF(2^8), -1 = 1)
            new_gen[j + 1] ^= gf_mul(gen[j], root);
        }
        gen = new_gen;
    }

    return gen;
}

// Polynomial division: returns remainder of dividend / divisor in GF(2^8)
std::vector<uint8_t> ReedSolomon::polyMod(std::vector<uint8_t> dividend,
                                            const std::vector<uint8_t>& divisor) {
    if (divisor.empty()) return dividend;

    // Work on a copy (dividend is passed by value)
    size_t dlen = dividend.size();
    size_t divlen = divisor.size();

    if (dlen < divlen) return dividend;

    uint8_t lead_inv = gf_inv(divisor[0]);

    for (size_t i = 0; i <= dlen - divlen; ++i) {
        if (dividend[i] == 0) continue;
        uint8_t coeff = gf_mul(dividend[i], lead_inv);
        for (size_t j = 0; j < divlen; ++j) {
            dividend[i + j] ^= gf_mul(divisor[j], coeff);
        }
    }

    // Extract remainder (last divlen-1 elements)
    size_t remainder_len = divlen - 1;
    std::vector<uint8_t> remainder(remainder_len);
    size_t start = dlen - remainder_len;
    for (size_t i = 0; i < remainder_len; ++i) {
        remainder[i] = dividend[start + i];
    }
    return remainder;
}

// Evaluate polynomial at point x in GF(2^8)
// poly[0] is the highest degree coefficient
uint8_t ReedSolomon::polyEval(const std::vector<uint8_t>& poly, uint8_t x) {
    uint8_t result = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        result = gf_mul(result, x) ^ poly[i];
    }
    return result;
}

// Encode: append 2*t parity bytes to data
std::vector<uint8_t> ReedSolomon::encode(const std::vector<uint8_t>& data) const {
    int npar = 2 * t_;
    std::vector<uint8_t> gen = generatorPoly();

    // Create message polynomial: data * x^npar
    std::vector<uint8_t> msg(data.size() + npar, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        msg[i] = data[i];
    }

    // Compute remainder = msg mod gen
    std::vector<uint8_t> remainder = polyMod(msg, gen);

    // Systematic encoding: [data | parity]
    std::vector<uint8_t> encoded(data.size() + npar);
    for (size_t i = 0; i < data.size(); ++i) {
        encoded[i] = data[i];
    }
    for (int i = 0; i < npar; ++i) {
        encoded[data.size() + i] = remainder[i];
    }

    return encoded;
}

// Berlekamp-Massey algorithm
// Input: syndromes S[0..2t-1], where S[i] = R(alpha^i)
// Output: error locator polynomial sigma(x)
std::vector<uint8_t> ReedSolomon::berlekampMassey(const std::vector<uint8_t>& syndromes, int n) {
    // sigma: current error locator polynomial
    std::vector<uint8_t> sigma(n + 1, 0);
    sigma[0] = 1;
    int L = 0; // Current number of assumed errors

    // B: previous sigma
    std::vector<uint8_t> B(n + 1, 0);
    B[0] = 1;
    int m = 1; // Shift counter

    for (int step = 0; step < n; ++step) {
        // Compute discrepancy
        uint8_t d = syndromes[step];
        for (int i = 1; i <= L; ++i) {
            d ^= gf_mul(sigma[i], syndromes[step - i]);
        }

        if (d == 0) {
            ++m;
        } else if (2 * L <= step) {
            // Update: new_sigma = sigma - d * B * x^m
            std::vector<uint8_t> T(sigma);
            for (int i = 0; i <= n - m; ++i) {
                T[i + m] ^= gf_mul(d, B[i]);
            }
            // B = sigma / d
            uint8_t d_inv = gf_inv(d);
            for (int i = 0; i <= n; ++i) {
                B[i] = gf_mul(sigma[i], d_inv);
            }
            sigma = T;
            L = step + 1 - L;
            m = 1;
        } else {
            // Update: sigma = sigma - d * B * x^m
            for (int i = 0; i <= n - m; ++i) {
                sigma[i + m] ^= gf_mul(d, B[i]);
            }
            ++m;
        }
    }

    // Trim sigma to actual degree
    sigma.resize(L + 1);
    return sigma;
}

// Chien search: find error positions from error locator polynomial
// Returns positions (array indices into the codeword) where errors
// occurred.
std::vector<int> ReedSolomon::chienSearch(const std::vector<uint8_t>& errorLocator, int blockLen) {
    std::vector<int> positions;

    // Berlekamp-Massey builds sigma(x) = prod (1 - X_k x) in ascending
    // order (sigma[0] = 1 is the constant term), whose roots are
    // X_k^(-1) = alpha^(j-(n-1)). polyEval expects the highest-degree
    // coefficient first, so flip the polynomial before evaluating.
    // Testing sigma(alpha^(-i)) == 0 then yields j = n-1-i (mod n),
    // which is the array index of the corrupted byte.
    std::vector<uint8_t> sigmaDesc(errorLocator.rbegin(), errorLocator.rend());
    for (int i = 0; i < blockLen; ++i) {
        uint8_t x = gf_exp_table_[(255 - (i % 255)) % 255]; // alpha^(-i)
        uint8_t val = polyEval(sigmaDesc, x);
        if (val == 0) {
            positions.push_back(blockLen - 1 - i);
        }
    }

    return positions;
}

// Forney algorithm: compute error magnitudes
std::vector<uint8_t> ReedSolomon::forneyAlgorithm(const std::vector<uint8_t>& syndromes,
                                                    const std::vector<uint8_t>& errorLocator,
                                                    const std::vector<int>& errorPositions,
                                                    int blockLen) {
    int npar = static_cast<int>(syndromes.size());
    std::vector<uint8_t> errors;

    // Compute the error evaluator polynomial omega(x) = S(x)*sigma(x) mod x^(2t)
    // S(x) = syndromes[0] + syndromes[1]*x + ... + syndromes[2t-1]*x^(2t-1)
    // sigma(x) = errorLocator (coefficients from highest to lowest degree)
    // But we need sigma in ascending order for this computation

    // sigma from Berlekamp-Massey is already in ascending order
    // (constant term first); use it directly for the omega convolution.
    const std::vector<uint8_t>& sigma_asc = errorLocator;

    // Compute omega = S * sigma mod x^(npar)
    std::vector<uint8_t> omega(npar, 0);
    for (int i = 0; i < npar; ++i) {
        for (int j = 0; j <= i && j < static_cast<int>(sigma_asc.size()); ++j) {
            omega[i] ^= gf_mul(syndromes[i - j], sigma_asc[j]);
        }
    }

    // Compute formal derivative of sigma: sigma'(x)
    // sigma'(x) = sum of sigma_asc[i] * i * x^(i-1) for odd i (in GF(2), even terms vanish)
    std::vector<uint8_t> sigma_deriv(sigma_asc.size(), 0);
    for (size_t i = 1; i < sigma_asc.size(); i += 2) {
        // In GF(2^8), multiplying by odd i: we just add sigma_asc[i] (since i is odd, i mod 2 = 1)
        // Actually in GF(2^8), i*x means adding x copies. For odd i, the coefficient survives.
        // In characteristic 2, d/dx(x^i) = i*x^(i-1), and i mod 2 determines if term survives
        sigma_deriv[i - 1] = sigma_asc[i];
    }

    // For each error position, compute the error value
    for (size_t k = 0; k < errorPositions.size(); ++k) {
        int pos = errorPositions[k];
        // The error location X_k = alpha^(n-1-pos): pos is an array
        // index into the codeword (see chienSearch), and the byte at
        // index j is the coefficient of x^(n-1-j).
        uint8_t Xk = gf_exp_table_[blockLen - 1 - pos];

        // Error value = -Xk * omega(Xk^(-1)) / sigma'(Xk^(-1))
        // In GF(2^8), negation is identity
        uint8_t Xk_inv = gf_inv(Xk);

        uint8_t omega_val = polyEval(omega, Xk_inv);
        // Need omega in descending order for polyEval
        // Actually polyEval expects descending order. Let me re-evaluate.
        // Let's just evaluate omega_asc at Xk_inv directly
        uint8_t omega_eval = 0;
        uint8_t xpow = 1;
        for (int i = 0; i < npar; ++i) {
            omega_eval ^= gf_mul(omega[i], xpow);
            xpow = gf_mul(xpow, Xk_inv);
        }

        uint8_t sigma_deriv_eval = 0;
        xpow = 1;
        for (size_t i = 0; i < sigma_deriv.size(); ++i) {
            sigma_deriv_eval ^= gf_mul(sigma_deriv[i], xpow);
            xpow = gf_mul(xpow, Xk_inv);
        }

        if (sigma_deriv_eval == 0) {
            errors.push_back(0);
            continue;
        }

        // e_k = Xk * omega_eval / sigma_deriv_eval
        uint8_t ek = gf_mul(Xk, gf_mul(omega_eval, gf_inv(sigma_deriv_eval)));
        errors.push_back(ek);
    }

    return errors;
}

// Decode: correct errors and return original data
ReedSolomon::DecodeResult ReedSolomon::decode(const std::vector<uint8_t>& encoded) const {
    DecodeResult result;
    result.corrected_errors = 0;
    result.uncorrectable = 0;
    result.success = false;

    int npar = 2 * t_;
    int blockLen = static_cast<int>(encoded.size());

    if (blockLen < npar) {
        return result; // Too short
    }

    // Step 1: Compute syndromes S[i] = R(alpha^i) for i = 0..2t-1
    std::vector<uint8_t> syndromes(npar, 0);
    bool hasErrors = false;
    for (int i = 0; i < npar; ++i) {
        uint8_t alpha_i = gf_exp_table_[i];
        uint8_t val = 0;
        for (int j = 0; j < blockLen; ++j) {
            val = gf_mul(val, alpha_i) ^ encoded[j];
        }
        syndromes[i] = val;
        if (val != 0) hasErrors = true;
    }

    // If all syndromes are zero, no errors
    if (!hasErrors) {
        result.data.assign(encoded.begin(), encoded.end() - npar);
        result.success = true;
        return result;
    }

    // Step 2: Berlekamp-Massey to find error locator polynomial
    std::vector<uint8_t> errorLocator = berlekampMassey(syndromes, npar);
    int numErrors = static_cast<int>(errorLocator.size()) - 1; // Degree of sigma

    if (numErrors > t_) {
        result.uncorrectable = numErrors - t_;
        return result; // Too many errors
    }

    // Step 3: Chien search to find error positions
    std::vector<int> errorPositions = chienSearch(errorLocator, blockLen);

    if (static_cast<int>(errorPositions.size()) != numErrors) {
        result.uncorrectable = 1;
        return result; // Could not find all error positions
    }

    // Step 4: Forney algorithm to find error magnitudes
    std::vector<uint8_t> errorMagnitudes = forneyAlgorithm(syndromes, errorLocator, errorPositions, blockLen);

    // Step 5: Correct errors
    std::vector<uint8_t> corrected = encoded;
    for (size_t k = 0; k < errorPositions.size(); ++k) {
        int pos = errorPositions[k];
        if (pos < blockLen) {
            corrected[pos] ^= errorMagnitudes[k];
            result.corrected_errors++;
        }
    }

    // Verify correction by recomputing syndromes
    bool correctionOk = true;
    for (int i = 0; i < npar; ++i) {
        uint8_t alpha_i = gf_exp_table_[i];
        uint8_t val = 0;
        for (int j = 0; j < blockLen; ++j) {
            val = gf_mul(val, alpha_i) ^ corrected[j];
        }
        if (val != 0) {
            correctionOk = false;
            break;
        }
    }

    if (!correctionOk) {
        result.uncorrectable = 1;
        return result;
    }

    // Extract original data (remove parity bytes)
    result.data.assign(corrected.begin(), corrected.end() - npar);
    result.success = true;
    return result;
}

} // namespace audiot
