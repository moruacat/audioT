// qt_audio_stream.cpp - Real-time audio input/output implementation
#include "qt_audio_stream.h"
#include "audio_device.h"
#include <QAudioFormat>
#include <cstring>
#include <algorithm>
#include <vector>

namespace audiot {

// WORKAROUND: on this machine the Qt Multimedia Windows plugin
// (qtaudio_windows.dll) corrupts the heap when QAudioInput/QAudioOutput
// objects are destroyed (verified crash family: stop() then delete =>
// access violation / RtlFreeHeap invalid address). We therefore never
// delete them: stopped audio objects are parked in this intentionally-
// leaked container for the lifetime of the process.
//
// NOTE: leaked objects are created WITHOUT a QObject parent, otherwise
// Qt's object-tree cleanup would still destroy them when the owning
// stream (and ultimately the main window) is torn down.
static std::vector<QObject*>* audioObjectKeepAlive() {
    static std::vector<QObject*>* keep = new std::vector<QObject*>();
    return keep;
}

static void leakQtAudioObject(QObject* obj) {
    if (obj) audioObjectKeepAlive()->push_back(obj);
}

// ============================================================
// QtAudioInputStream
// ============================================================

QtAudioInputStream::QtAudioInputStream(QObject* parent)
    : QObject(parent)
    , audioInput_(nullptr)
    , audioDevice_(nullptr)
    , sampleRate_(44100)
    , channels_(1)
    , running_(false)
{
}

QtAudioInputStream::~QtAudioInputStream() {
    stop();
}

bool QtAudioInputStream::start(int sampleRate, int channels, int bufferSize) {
    if (running_) stop();

    sampleRate_ = sampleRate;
    channels_ = channels;

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setSampleType(QAudioFormat::SignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);

    QAudioDeviceInfo devInfo = deviceInfo_.isNull()
        ? QAudioDeviceInfo::defaultInputDevice()
        : deviceInfo_;
    // Keep the handle alive: destroying the last QAudioDeviceInfo reference
    // can crash the Qt Multimedia Windows plugin on some systems.
    keepQtDeviceHandleAlive(devInfo);

    if (devInfo.isNull()) return false;

    if (!devInfo.isFormatSupported(format)) {
        format = devInfo.nearestFormat(format);
    }

    // No QObject parent on purpose: see leakQtAudioObject() above.
    audioInput_ = new QAudioInput(devInfo, format, nullptr);
    audioInput_->setBufferSize(bufferSize * channels * 2); // 16-bit = 2 bytes per sample
    audioInput_->setNotifyInterval(23); // ~23ms, matching 1024 samples @44100Hz

    audioDevice_ = audioInput_->start();
    if (!audioDevice_) {
        // Never delete: park the (stopped) object in the leak container.
        leakQtAudioObject(audioInput_);
        audioInput_ = nullptr;
        return false;
    }

    connect(audioInput_, &QAudioInput::stateChanged, this, [](QAudio::State state) {
        // State change handling (for debugging)
        (void)state;
    });

    // Connect readyRead from the IO device
    connect(audioDevice_, &QIODevice::readyRead, this, &QtAudioInputStream::onAudioDataReady);

    running_ = true;
    return true;
}

void QtAudioInputStream::stop() {
    if (!running_) return;

    // Mark stopped FIRST so any already-queued readyRead events are ignored.
    running_ = false;

    if (audioInput_) {
        // Disconnect BEFORE QAudioInput::stop(): in pull mode the QIODevice
        // returned by start() is invalidated by stop(), so touching it
        // afterwards would be a use-after-free.
        if (audioDevice_) {
            disconnect(audioDevice_, &QIODevice::readyRead, this, &QtAudioInputStream::onAudioDataReady);
        }
        audioInput_->stop();
        audioDevice_ = nullptr;
        // Never delete the QAudioInput: its destructor runs the buggy
        // qtaudio_windows plugin cleanup path. Leak it instead.
        leakQtAudioObject(audioInput_);
        audioInput_ = nullptr;
    }
}

bool QtAudioInputStream::isRunning() const {
    return running_;
}

void QtAudioInputStream::setCallback(AudioInputCallback callback) {
    callback_ = callback;
}

void QtAudioInputStream::setDevice(const QAudioDeviceInfo& deviceInfo) {
    deviceInfo_ = deviceInfo;
}

void QtAudioInputStream::onAudioDataReady() {
    // Defensive: readyRead events queued before stop() must not read from
    // the invalidated device.
    if (!running_ || !audioDevice_ || !callback_) return;

    QByteArray data = audioDevice_->readAll();
    if (data.size() > 0) {
        convertAndDeliver(data.constData(), data.size());
    }
}

// Convert PCM 16-bit signed int to float [-1.0, 1.0] and deliver via callback
void QtAudioInputStream::convertAndDeliver(const char* data, qint64 len) {
    if (!callback_ || len <= 0) return;

    const int16_t* pcm = reinterpret_cast<const int16_t*>(data);
    int sampleCount = static_cast<int>(len / 2); // 16-bit = 2 bytes per sample

    std::vector<float> floatSamples(sampleCount);
    const float scale = 1.0f / 32768.0f;
    for (int i = 0; i < sampleCount; ++i) {
        floatSamples[i] = static_cast<float>(pcm[i]) * scale;
    }

    callback_(floatSamples.data(), sampleCount);
}

// ============================================================
// QtAudioOutputStream
// ============================================================

QtAudioOutputStream::QtAudioOutputStream(QObject* parent)
    : QObject(parent)
    , audioOutput_(nullptr)
    , audioDevice_(nullptr)
    , sampleRate_(44100)
    , channels_(1)
    , running_(false)
{
}

QtAudioOutputStream::~QtAudioOutputStream() {
    stop();
}

bool QtAudioOutputStream::start(int sampleRate, int channels, int bufferSize) {
    if (running_) stop();

    sampleRate_ = sampleRate;
    channels_ = channels;

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setSampleType(QAudioFormat::SignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);

    QAudioDeviceInfo devInfo = deviceInfo_.isNull()
        ? QAudioDeviceInfo::defaultOutputDevice()
        : deviceInfo_;
    // Keep the handle alive (see QtAudioInputStream::start)
    keepQtDeviceHandleAlive(devInfo);

    if (devInfo.isNull()) return false;

    if (!devInfo.isFormatSupported(format)) {
        format = devInfo.nearestFormat(format);
    }

    // No QObject parent on purpose: see leakQtAudioObject() above.
    audioOutput_ = new QAudioOutput(devInfo, format, nullptr);
    audioOutput_->setBufferSize(bufferSize * channels * 2);

    audioDevice_ = audioOutput_->start();
    if (!audioDevice_) {
        // Never delete: park the (stopped) object in the leak container.
        leakQtAudioObject(audioOutput_);
        audioOutput_ = nullptr;
        return false;
    }

    running_ = true;
    return true;
}

void QtAudioOutputStream::stop() {
    if (!running_) return;

    // Mark stopped FIRST so concurrent write() calls are rejected.
    running_ = false;

    if (audioOutput_) {
        // Drop the device reference before stopping: the pull-mode QIODevice
        // is invalidated by QAudioOutput::stop().
        audioDevice_ = nullptr;
        audioOutput_->stop();
        // Never delete the QAudioOutput (buggy plugin destructor path);
        // leak it instead.
        leakQtAudioObject(audioOutput_);
        audioOutput_ = nullptr;
    }
}

bool QtAudioOutputStream::isRunning() const {
    return running_;
}

void QtAudioOutputStream::setRequestCallback(AudioRequestCallback callback) {
    requestCallback_ = callback;
}

// Write float samples to the audio output (convert to PCM 16-bit)
void QtAudioOutputStream::write(const float* samples, int count) {
    if (!audioDevice_ || !running_ || count <= 0) return;

    // Convert float [-1.0, 1.0] to int16_t
    std::vector<int16_t> pcm(count);
    for (int i = 0; i < count; ++i) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcm[i] = static_cast<int16_t>(s * 32767.0f);
    }

    audioDevice_->write(reinterpret_cast<const char*>(pcm.data()),
                        count * sizeof(int16_t));
}

int QtAudioOutputStream::bytesFree() const {
    if (!audioOutput_ || !running_) return 0;
    const int freeBytes = audioOutput_->bytesFree();
    return freeBytes > 0 ? freeBytes : 0;
}

int QtAudioOutputStream::bufferBytes() const {
    if (!audioOutput_ || !running_) return 0;
    return audioOutput_->bufferSize();
}

void QtAudioOutputStream::setDevice(const QAudioDeviceInfo& deviceInfo) {
    deviceInfo_ = deviceInfo;
}

} // namespace audiot
