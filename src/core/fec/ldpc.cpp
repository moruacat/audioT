// ldpc.cpp - LDPC (Low-Density Parity-Check) codec implementation
#include "ldpc.h"

#include <algorithm>

namespace audiot {

// Number of information-bit connections per parity-check row
static const int kRowWeight = 3;

// Deterministic tap column for row i / tap index t (spread across info block)
static int tapColumn(int row, int tap, int k) {
    // Small primes keep taps well distributed and mostly distinct
    static const int strides[kRowWeight] = { 3, 5, 7 };
    static const int offsets[kRowWeight] = { 1, 7, 13 };
    return (row * strides[tap] + offsets[tap]) % k;
}

LDPCCodec::LDPCCodec(int n, int k)
    : n_(n > 0 ? n : 256)
    , k_(k > 0 ? k : 128)
    , H_(nullptr)
{
    if (k_ >= n_) k_ = n_ / 2;
    H_ = new SparseMatrix();
    generateParityCheckMatrix();
}

LDPCCodec::~LDPCCodec() {
    delete H_;
}

int LDPCCodec::codeLength() const { return n_; }
int LDPCCodec::infoLength() const { return k_; }
int LDPCCodec::parityLength() const { return n_ - k_; }

void LDPCCodec::generateParityCheckMatrix() {
    const int m = n_ - k_; // number of parity-check rows

    H_->numRows = m;
    H_->numCols = n_;
    H_->rows.assign(m, std::vector<int>());
    H_->cols.assign(n_, std::vector<int>());

    // H = [P | I_m]: each row i connects to parity column (k+i)
    // plus kRowWeight deterministic information columns.
    for (int i = 0; i < m; ++i) {
        std::vector<int>& row = H_->rows[i];

        for (int t = 0; t < kRowWeight; ++t) {
            const int col = tapColumn(i, t, k_);
            // Avoid duplicate columns within a row
            bool dup = false;
            for (size_t s = 0; s < row.size(); ++s) {
                if (row[s] == col) { dup = true; break; }
            }
            if (!dup) {
                row.push_back(col);
                H_->cols[col].push_back(i);
            }
        }

        row.push_back(k_ + i);
        H_->cols[k_ + i].push_back(i);

        std::sort(row.begin(), row.end());
    }
}

void LDPCCodec::systematicEncode(const std::vector<uint8_t>& info,
                                 std::vector<uint8_t>& codeword) const {
    codeword.assign(n_, 0);
    for (int j = 0; j < k_; ++j) {
        codeword[j] = info[j] & 0x01;
    }
    // With H = [P | I_m], parity bit i = XOR of the selected info bits
    const int m = n_ - k_;
    for (int i = 0; i < m; ++i) {
        int acc = 0;
        const std::vector<int>& row = H_->rows[i];
        for (size_t t = 0; t < row.size(); ++t) {
            const int col = row[t];
            if (col < k_) acc ^= codeword[col];
        }
        codeword[k_ + i] = static_cast<uint8_t>(acc);
    }
}

std::vector<uint8_t> LDPCCodec::encode(const std::vector<uint8_t>& data) const {
    std::vector<uint8_t> info(k_, 0);
    const size_t copyLen = std::min<size_t>(data.size(), k_);
    for (size_t i = 0; i < copyLen; ++i) {
        info[i] = data[i] & 0x01;
    }
    std::vector<uint8_t> codeword;
    systematicEncode(info, codeword);
    return codeword;
}

std::vector<float> LDPCCodec::bitsToLLR(const std::vector<uint8_t>& bits,
                                        float confidence) {
    // LLR convention: positive -> bit 0, negative -> bit 1
    const float mag = 2.0f + confidence * 4.0f; // ~2.0 .. 6.0
    std::vector<float> llr(bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        llr[i] = (bits[i] & 0x01) ? -mag : mag;
    }
    return llr;
}

std::vector<uint8_t> LDPCCodec::beliefPropagation(
    const std::vector<float>& llr, int maxIter, int& iterationsUsed) const {

    const int m = n_ - k_;
    iterationsUsed = 0;

    // Edge-aligned message storage:
    //   c2v[i][t] parallel to H_->rows[i] (check i -> variable rows[i][t])
    //   v2c[j][u] parallel to H_->cols[j] (variable j -> check cols[j][u])
    std::vector<std::vector<float>> c2v(m);
    std::vector<std::vector<float>> v2c(n_);

    // Precompute cross-index maps so edge lookups are O(1):
    //   rowPos[i][t] = position of check i inside H_->cols[rows[i][t]]
    //   colPos[j][u] = position of variable j inside H_->rows[cols[j][u]]
    std::vector<std::vector<int>> rowPos(m);
    std::vector<std::vector<int>> colPos(n_);
    for (int i = 0; i < m; ++i) {
        const std::vector<int>& row = H_->rows[i];
        rowPos[i].resize(row.size());
        for (size_t t = 0; t < row.size(); ++t) {
            const std::vector<int>& colRows = H_->cols[row[t]];
            for (size_t u = 0; u < colRows.size(); ++u) {
                if (colRows[u] == i) { rowPos[i][t] = static_cast<int>(u); break; }
            }
        }
    }
    for (int j = 0; j < n_; ++j) {
        const std::vector<int>& colRows = H_->cols[j];
        colPos[j].resize(colRows.size());
        for (size_t u = 0; u < colRows.size(); ++u) {
            const std::vector<int>& row = H_->rows[colRows[u]];
            for (size_t t = 0; t < row.size(); ++t) {
                if (row[t] == j) { colPos[j][u] = static_cast<int>(t); break; }
            }
        }
    }

    for (int i = 0; i < m; ++i) {
        c2v[i].assign(H_->rows[i].size(), 0.0f);
    }
    for (int j = 0; j < n_; ++j) {
        v2c[j].assign(H_->cols[j].size(), llr[j]);
    }

    std::vector<uint8_t> hard(n_, 0);

    for (int iter = 0; iter < maxIter; ++iter) {
        iterationsUsed = iter + 1;

        // ---- Check-to-variable update (Min-Sum) ----
        for (int i = 0; i < m; ++i) {
            const std::vector<int>& row = H_->rows[i];
            const size_t deg = row.size();

            for (size_t t = 0; t < deg; ++t) {
                int sign = 1;
                float mag = 1e30f;
                for (size_t s = 0; s < deg; ++s) {
                    if (s == t) continue;
                    const float msg = v2c[row[s]][rowPos[i][s]];
                    if (msg < 0.0f) sign = -sign;
                    const float a = msg < 0.0f ? -msg : msg;
                    if (a < mag) mag = a;
                }
                c2v[i][t] = static_cast<float>(sign) * mag;
            }
        }

        // ---- Variable-to-check update ----
        for (int j = 0; j < n_; ++j) {
            const std::vector<int>& colRows = H_->cols[j];
            const size_t deg = colRows.size();

            float total = llr[j];
            for (size_t u = 0; u < deg; ++u) {
                total += c2v[colRows[u]][colPos[j][u]];
            }
            for (size_t u = 0; u < deg; ++u) {
                v2c[j][u] = total - c2v[colRows[u]][colPos[j][u]];
            }
        }

        // ---- Posterior + hard decision + syndrome check ----
        bool syndromeOk = true;
        for (int j = 0; j < n_; ++j) {
            float post = llr[j];
            const std::vector<int>& colRows = H_->cols[j];
            for (size_t u = 0; u < colRows.size(); ++u) {
                post += c2v[colRows[u]][colPos[j][u]];
            }
            hard[j] = (post < 0.0f) ? 1 : 0;
        }
        for (int i = 0; i < m && syndromeOk; ++i) {
            int acc = 0;
            const std::vector<int>& row = H_->rows[i];
            for (size_t t = 0; t < row.size(); ++t) {
                acc ^= hard[row[t]];
            }
            if (acc != 0) syndromeOk = false;
        }
        if (syndromeOk) break;
    }

    return hard;
}

LDPCCodec::DecodeResult LDPCCodec::decodeSoft(const std::vector<float>& llr,
                                              int maxIterations) const {
    DecodeResult result;
    result.iterations_used = 0;
    result.success = false;

    if (static_cast<int>(llr.size()) < n_) return result;

    std::vector<float> prior(llr.begin(), llr.begin() + n_);
    int used = 0;
    std::vector<uint8_t> hard = beliefPropagation(prior, maxIterations, used);
    result.iterations_used = used;

    // Verify syndrome of the final hard decision
    const int m = n_ - k_;
    bool ok = true;
    for (int i = 0; i < m && ok; ++i) {
        int acc = 0;
        const std::vector<int>& row = H_->rows[i];
        for (size_t t = 0; t < row.size(); ++t) {
            acc ^= hard[row[t]];
        }
        if (acc != 0) ok = false;
    }

    result.success = ok;
    result.data.assign(hard.begin(), hard.begin() + k_);
    return result;
}

LDPCCodec::DecodeResult LDPCCodec::decode(const std::vector<uint8_t>& received,
                                          int maxIterations) const {
    DecodeResult result;
    result.iterations_used = 0;
    result.success = false;

    if (static_cast<int>(received.size()) < n_) return result;

    std::vector<uint8_t> bits(received.begin(), received.begin() + n_);
    return decodeSoft(bitsToLLR(bits), maxIterations);
}

} // namespace audiot
