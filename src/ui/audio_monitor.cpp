// audio_monitor.cpp - Real-time audio waveform/spectrum display widget
#include "audio_monitor.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

namespace audiot {

AudioMonitor::AudioMonitor(QWidget* parent)
    : QWidget(parent)
    , displayMode_(0)
{
    setMinimumHeight(100);
    setMaximumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize AudioMonitor::sizeHint() const
{
    return QSize(400, 100);
}

void AudioMonitor::updateWaveform(const float* samples, int count)
{
    if (!samples || count <= 0) return;
    waveform_.assign(samples, samples + count);
    update();
}

void AudioMonitor::updateSpectrum(const float* magnitudes, int count)
{
    if (!magnitudes || count <= 0) return;
    spectrum_.assign(magnitudes, magnitudes + count);
    update();
}

void AudioMonitor::setDisplayMode(int mode)
{
    displayMode_ = (mode == 1) ? 1 : 0;
    update();
}

void AudioMonitor::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Black background
    painter.fillRect(rect(), Qt::black);

    const int w = width();
    const int h = height();
    const int midY = h / 2;

    if (displayMode_ == 0) {
        // Waveform mode: green line on black
        if (waveform_.empty()) {
            // Draw center line
            painter.setPen(QPen(QColor(0, 80, 0), 1));
            painter.drawLine(0, midY, w, midY);
            return;
        }

        painter.setPen(QPen(QColor(0, 255, 100), 1));

        const int n = static_cast<int>(waveform_.size());
        const float xStep = static_cast<float>(w) / static_cast<float>(n);

        int prevX = 0;
        int prevY = midY - static_cast<int>(waveform_[0] * midY);

        for (int i = 1; i < n; ++i) {
            int x = static_cast<int>(i * xStep);
            int y = midY - static_cast<int>(waveform_[i] * midY);
            // Clamp y
            y = std::max(0, std::min(h - 1, y));
            painter.drawLine(prevX, prevY, x, y);
            prevX = x;
            prevY = y;
        }
    } else {
        // Spectrum mode: vertical bars
        if (spectrum_.empty()) {
            return;
        }

        painter.setPen(QPen(QColor(0, 180, 255), 1));

        const int n = static_cast<int>(spectrum_.size());
        const int barWidth = std::max(1, w / n);
        int x = 0;

        for (int i = 0; i < n && x < w; ++i) {
            // Magnitude in dB-like scale (assume 0..1 linear)
            float mag = std::max(0.0f, std::min(1.0f, spectrum_[i]));
            int barH = static_cast<int>(mag * h);
            painter.fillRect(x, h - barH, barWidth - 1, barH, QColor(0, 180, 255));
            x += barWidth;
        }
    }
}

} // namespace audiot
