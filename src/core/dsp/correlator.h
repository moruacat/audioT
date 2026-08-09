#ifndef AUDIOT_CORRELATOR_H
#define AUDIOT_CORRELATOR_H

#include <vector>
#include <cstddef>

namespace audiot {

// Correlator: searches for a known pattern in an input signal using cross-correlation.
// Used for synchronization detection in audio data communication.
class Correlator {
public:
    // Construct with the known pattern to search for
    explicit Correlator(const std::vector<float>& pattern);

    // Result of pattern search
    struct CorrelationResult {
        size_t offset;      // Position of best match in input
        float peak_value;   // Peak correlation value (normalized)
        bool found;         // Whether a match above threshold was found
    };

    // Search for the known pattern in input data.
    // Returns the best match position if correlation exceeds threshold.
    CorrelationResult find(const std::vector<float>& input, float threshold = 0.5f);

private:
    std::vector<float> pattern_;
    float pattern_energy_;  // Precomputed pattern energy for normalization
};

} // namespace audiot
#endif
