// audio_monitor.h - Real-time audio waveform/spectrum display widget
#ifndef AUDIOT_AUDIO_MONITOR_H
#define AUDIOT_AUDIO_MONITOR_H

#include <QWidget>
#include <vector>

namespace audiot {

class AudioMonitor : public QWidget {
    Q_OBJECT
public:
    explicit AudioMonitor(QWidget* parent = nullptr);

    QSize sizeHint() const override;

public slots:
    void updateWaveform(const float* samples, int count);
    void updateSpectrum(const float* magnitudes, int count);
    void setDisplayMode(int mode); // 0=waveform, 1=spectrum

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> waveform_;
    std::vector<float> spectrum_;
    int displayMode_; // 0=waveform, 1=spectrum
};

} // namespace audiot
#endif // AUDIOT_AUDIO_MONITOR_H
