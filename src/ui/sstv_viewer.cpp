// sstv_viewer.cpp - SSTV decoded image viewer implementation
#include "sstv_viewer.h"

#include <QPainter>
#include <QPalette>

#include <cstring>

namespace audiot {

SSTVViewer::SSTVViewer(QWidget* parent)
    : QWidget(parent)
    , hasImage_(false)
{
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(16, 18, 22));
    setPalette(pal);
}

void SSTVViewer::beginImage(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    image_ = QImage(width, height, QImage::Format_RGB888);
    image_.fill(Qt::black);
    hasImage_ = true;
    update();
}

void SSTVViewer::updateLine(int lineIndex, const uint8_t* rgbLine)
{
    if (!hasImage_ || rgbLine == nullptr) return;
    if (lineIndex < 0 || lineIndex >= image_.height()) return;

    uint8_t* dest = image_.scanLine(lineIndex);
    std::memcpy(dest, rgbLine, static_cast<size_t>(image_.width()) * 3);
    update();
}

void SSTVViewer::setImage(const uint8_t* rgbPixels, int width, int height)
{
    if (rgbPixels == nullptr || width <= 0 || height <= 0) return;

    QImage tmp(width, height, QImage::Format_RGB888);
    std::memcpy(tmp.bits(), rgbPixels,
                static_cast<size_t>(width) * height * 3);
    image_ = tmp.copy(); // detach from stack buffer
    hasImage_ = true;
    update();
}

void SSTVViewer::clearImage()
{
    image_ = QImage();
    hasImage_ = false;
    update();
}

bool SSTVViewer::hasImage() const { return hasImage_; }
int SSTVViewer::imageWidth() const { return hasImage_ ? image_.width() : 0; }
int SSTVViewer::imageHeight() const { return hasImage_ ? image_.height() : 0; }

bool SSTVViewer::saveImage(const QString& filePath) const
{
    if (!hasImage_) return false;
    return image_.save(filePath);
}

void SSTVViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(16, 18, 22));

    if (!hasImage_ || image_.isNull()) {
        painter.setPen(QColor(90, 96, 108));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("等待 SSTV 图像..."));
        return;
    }

    // Scale keeping aspect ratio, centered
    QImage scaled = image_.scaled(size(), Qt::KeepAspectRatio,
                                  Qt::FastTransformation);
    const int x = (width() - scaled.width()) / 2;
    const int y = (height() - scaled.height()) / 2;

    painter.setPen(QColor(52, 58, 68));
    painter.drawRect(x - 1, y - 1, scaled.width() + 1, scaled.height() + 1);
    painter.drawImage(x, y, scaled);
}

} // namespace audiot
