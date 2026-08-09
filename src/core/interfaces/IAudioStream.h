#ifndef AUDIOT_IAUDIOSTREAM_H
#define AUDIOT_IAUDIOSTREAM_H

#include <vector>
#include <functional>
#include <cstdint>

namespace audiot {

// Audio callback: receives a block of float samples (mono, range -1.0~1.0)
using AudioCallback = std::function<void(const std::vector<float>& samples)>;

// Audio stream abstract interface: manages real-time audio capture/playback
class IAudioStream {
public:
    virtual ~IAudioStream() = default;

    // Start the audio stream
    virtual bool start() = 0;

    // Stop the audio stream
    virtual void stop() = 0;

    // Check if the stream is currently running
    virtual bool isRunning() const = 0;

    // Set the callback for receiving audio input samples
    virtual void setInputCallback(AudioCallback callback) = 0;

    // Provide output samples to the audio stream (for playback)
    virtual void setOutputSamples(const std::vector<float>& samples) = 0;

    // Get current sample rate
    virtual int sampleRate() const = 0;

    // Get buffer size (samples per callback invocation)
    virtual int bufferSize() const = 0;
};

} // namespace audiot
#endif
