// qt_audio_stream.h - Real-time audio input/output using Qt Multimedia
#ifndef AUDIOT_QT_AUDIO_STREAM_H
#define AUDIOT_QT_AUDIO_STREAM_H

#include <QObject>
#include <QAudioInput>
#include <QAudioOutput>
#include <QIODevice>
#include <QAudioDeviceInfo>
#include <functional>

namespace audiot {

// Audio callback: receives float samples (-1.0 ~ 1.0)
using AudioInputCallback = std::function<void(const float* samples, int count)>;
using AudioRequestCallback = std::function<void(float* buffer, int count)>;

class QtAudioInputStream : public QObject {
    Q_OBJECT
public:
    explicit QtAudioInputStream(QObject* parent = nullptr);
    ~QtAudioInputStream();

    bool start(int sampleRate = 44100, int channels = 1, int bufferSize = 1024);
    void stop();
    bool isRunning() const;

    void setCallback(AudioInputCallback callback);

    // Set audio device (default device if not set)
    void setDevice(const QAudioDeviceInfo& deviceInfo);

private slots:
    void onAudioDataReady();

private:
    QAudioInput* audioInput_;
    QIODevice* audioDevice_;
    AudioInputCallback callback_;
    QAudioDeviceInfo deviceInfo_;
    int sampleRate_;
    int channels_;
    bool running_;
    void convertAndDeliver(const char* data, qint64 len);
};

class QtAudioOutputStream : public QObject {
    Q_OBJECT
public:
    explicit QtAudioOutputStream(QObject* parent = nullptr);
    ~QtAudioOutputStream();

    bool start(int sampleRate = 44100, int channels = 1, int bufferSize = 1024);
    void stop();
    bool isRunning() const;

    // Set callback: called when audio data is needed
    void setRequestCallback(AudioRequestCallback callback);

    // Directly write audio data (float samples)
    void write(const float* samples, int count);

    // Free space (bytes) in the underlying QAudioOutput buffer.
    // Lets the caller pace batched writes (push mode). 0 when not running.
    int bytesFree() const;

    // Total buffer size (bytes) requested via start(). 0 when not running.
    int bufferBytes() const;

    void setDevice(const QAudioDeviceInfo& deviceInfo);

private:
    QAudioOutput* audioOutput_;
    QIODevice* audioDevice_;
    AudioRequestCallback requestCallback_;
    QAudioDeviceInfo deviceInfo_;
    int sampleRate_;
    int channels_;
    bool running_;
};

} // namespace audiot
#endif // AUDIOT_QT_AUDIO_STREAM_H
