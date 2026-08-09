// audio_device.cpp - Audio device enumeration and management implementation
#include "audio_device.h"
#include <QAudioDeviceInfo>
#include <algorithm>
#include <vector>

namespace audiot {

// WORKAROUND: on this machine destroying the last reference to a
// QAudioDeviceInfo crashes inside the Qt Multimedia Windows plugin
// (access violation reading 0xFFFFFFFFFFFFFFFF). QAudioDeviceInfo is
// ref-counted, so we keep every enumerated device alive forever in this
// intentionally-leaked container: as long as one reference survives,
// destroying other copies is safe (ref-count decrement only).
static std::vector<QAudioDeviceInfo>* deviceKeepAlive() {
    static std::vector<QAudioDeviceInfo>* keep = new std::vector<QAudioDeviceInfo>();
    return keep;
}

void keepQtDeviceHandleAlive(const QAudioDeviceInfo& device) {
    if (!device.isNull()) deviceKeepAlive()->push_back(device);
}

AudioDeviceManager::AudioDeviceManager(QObject* parent)
    : QObject(parent)
{
    // Defer device enumeration to avoid crash on startup
    // Will be called lazily when devices are first requested
}

void AudioDeviceManager::refreshDevices() {
    // Enumerate only ONCE per process. On this machine a second call to
    // availableDevices() trips over heap already corrupted by the audio
    // plugin (verified with gdb: RtlFreeHeap "invalid address" inside
    // QList<QAudioDeviceInfo>::operator=). Subsequent refreshes serve the
    // cached results instead.
    if (enumerated_) {
        emit devicesChanged();
        return;
    }

    inputDevices_.clear();
    outputDevices_.clear();

    // Enumerate input devices (keep raw handles alive: see deviceKeepAlive)
    const QList<QAudioDeviceInfo> inputs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (const QAudioDeviceInfo& info : inputs) {
        deviceKeepAlive()->push_back(info);
        inputDevices_.append(convertInfo(info, true));
    }
    cachedInputs_ = inputs;

    // Enumerate output devices
    const QList<QAudioDeviceInfo> outputs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo& info : outputs) {
        deviceKeepAlive()->push_back(info);
        outputDevices_.append(convertInfo(info, false));
    }
    cachedOutputs_ = outputs;

    // Cache default devices as well (safe to destroy afterwards because
    // the keep-alive container holds a reference)
    cachedDefaultInput_ = QAudioDeviceInfo::defaultInputDevice();
    if (!cachedDefaultInput_.isNull()) deviceKeepAlive()->push_back(cachedDefaultInput_);
    cachedDefaultOutput_ = QAudioDeviceInfo::defaultOutputDevice();
    if (!cachedDefaultOutput_.isNull()) deviceKeepAlive()->push_back(cachedDefaultOutput_);

    enumerated_ = true;
    emit devicesChanged();
}

QList<AudioDeviceInfo> AudioDeviceManager::inputDevices() const {
    return inputDevices_;
}

QList<AudioDeviceInfo> AudioDeviceManager::outputDevices() const {
    return outputDevices_;
}

AudioDeviceInfo AudioDeviceManager::defaultInputDevice() const {
    QAudioDeviceInfo defaultInfo = cachedDefaultInput_;
    if (defaultInfo.isNull()) {
        defaultInfo = QAudioDeviceInfo::defaultInputDevice();
        if (!defaultInfo.isNull()) deviceKeepAlive()->push_back(defaultInfo);
    }
    if (defaultInfo.isNull()) {
        AudioDeviceInfo empty;
        empty.deviceName = QString("No input device");
        empty.deviceId = QString();
        empty.isInput = true;
        empty.defaultSampleRate = 0;
        empty.maxChannels = 0;
        return empty;
    }
    return convertInfo(defaultInfo, true);
}

AudioDeviceInfo AudioDeviceManager::defaultOutputDevice() const {
    QAudioDeviceInfo defaultInfo = cachedDefaultOutput_;
    if (defaultInfo.isNull()) {
        defaultInfo = QAudioDeviceInfo::defaultOutputDevice();
        if (!defaultInfo.isNull()) deviceKeepAlive()->push_back(defaultInfo);
    }
    if (defaultInfo.isNull()) {
        AudioDeviceInfo empty;
        empty.deviceName = QString("No output device");
        empty.deviceId = QString();
        empty.isInput = false;
        empty.defaultSampleRate = 0;
        empty.maxChannels = 0;
        return empty;
    }
    return convertInfo(defaultInfo, false);
}

QAudioDeviceInfo AudioDeviceManager::qtInputDeviceByName(const QString& name) const {
    for (const QAudioDeviceInfo& d : cachedInputs_) {
        if (d.deviceName() == name) return d;
    }
    if (!cachedDefaultInput_.isNull()) return cachedDefaultInput_;
    QAudioDeviceInfo def = QAudioDeviceInfo::defaultInputDevice();
    if (!def.isNull()) deviceKeepAlive()->push_back(def);
    return def;
}

QAudioDeviceInfo AudioDeviceManager::qtOutputDeviceByName(const QString& name) const {
    for (const QAudioDeviceInfo& d : cachedOutputs_) {
        if (d.deviceName() == name) return d;
    }
    if (!cachedDefaultOutput_.isNull()) return cachedDefaultOutput_;
    QAudioDeviceInfo def = QAudioDeviceInfo::defaultOutputDevice();
    if (!def.isNull()) deviceKeepAlive()->push_back(def);
    return def;
}

AudioDeviceInfo AudioDeviceManager::convertInfo(const QAudioDeviceInfo& info, bool isInput) const {
    AudioDeviceInfo result;
    result.deviceName = info.deviceName();
    result.deviceId = info.deviceName(); // Qt5 uses device name as identifier
    result.isInput = isInput;

    // Get supported sample rates and pick the preferred one
    QList<int> rates = info.supportedSampleRates();
    result.supportedSampleRates = rates;
    if (!rates.isEmpty()) {
        // Prefer 44100 or 48000, otherwise use the highest available
        if (rates.contains(44100)) {
            result.defaultSampleRate = 44100;
        } else if (rates.contains(48000)) {
            result.defaultSampleRate = 48000;
        } else {
            result.defaultSampleRate = rates.last();
        }
    } else {
        result.defaultSampleRate = 0;
    }

    // Get maximum channel count from supported channel counts
    QList<int> channels = info.supportedChannelCounts();
    result.maxChannels = channels.isEmpty() ? 1 : *std::max_element(channels.begin(), channels.end());
    return result;
}

} // namespace audiot
