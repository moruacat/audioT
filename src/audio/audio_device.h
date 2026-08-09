// audio_device.h - Audio device enumeration and management (Qt Multimedia)
#ifndef AUDIOT_AUDIO_DEVICE_H
#define AUDIOT_AUDIO_DEVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QAudioDeviceInfo>

namespace audiot {

// Keep a reference to the given device handle alive for the entire process
// lifetime. On some Windows systems destroying the LAST reference to a
// QAudioDeviceInfo crashes inside the Qt Multimedia audio plugin.
void keepQtDeviceHandleAlive(const QAudioDeviceInfo& device);

struct AudioDeviceInfo {
    QString deviceName;
    QString deviceId;
    bool isInput;          // true=microphone/input, false=speaker/output
    int defaultSampleRate;
    QList<int> supportedSampleRates;
    int maxChannels;
};

class AudioDeviceManager : public QObject {
    Q_OBJECT
public:
    explicit AudioDeviceManager(QObject* parent = nullptr);

    // Refresh the device list
    void refreshDevices();

    // Get all input devices
    QList<AudioDeviceInfo> inputDevices() const;

    // Get all output devices
    QList<AudioDeviceInfo> outputDevices() const;

    // Get default input device
    AudioDeviceInfo defaultInputDevice() const;

    // Get default output device
    AudioDeviceInfo defaultOutputDevice() const;

    // Look up the raw Qt device handle by name from the cached enumeration.
    // Falls back to the default device. The returned handle is kept alive
    // internally, so copies are safe to use and destroy.
    QAudioDeviceInfo qtInputDeviceByName(const QString& name) const;
    QAudioDeviceInfo qtOutputDeviceByName(const QString& name) const;

signals:
    void devicesChanged();

private:
    QList<AudioDeviceInfo> inputDevices_;
    QList<AudioDeviceInfo> outputDevices_;
    // Raw Qt device handles from the last enumeration (kept for lookup)
    QList<QAudioDeviceInfo> cachedInputs_;
    QList<QAudioDeviceInfo> cachedOutputs_;
    QAudioDeviceInfo cachedDefaultInput_;
    QAudioDeviceInfo cachedDefaultOutput_;
    bool enumerated_ = false;

    AudioDeviceInfo convertInfo(const QAudioDeviceInfo& info, bool isInput) const;
};

} // namespace audiot
#endif // AUDIOT_AUDIO_DEVICE_H
