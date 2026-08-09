// sstv_viewer.h - Widget for displaying SSTV decoded images
#ifndef AUDIOT_SSTV_VIEWER_H
#define AUDIOT_SSTV_VIEWER_H

#include <QWidget>
#include <QImage>
#include <cstdint>

namespace audiot {

// Displays a progressively decoded SSTV image.
// Call updateLine() as scan lines arrive; the widget repaints itself.
class SSTVViewer : public QWidget {
    Q_OBJECT
public:
    explicit SSTVViewer(QWidget* parent = nullptr);

    // Reset to an empty image of the given dimensions
    void beginImage(int width, int height);

    // Write one decoded scan line (RGB, 3 bytes per pixel)
    void updateLine(int lineIndex, const uint8_t* rgbLine);

    // Set the complete image at once (RGB, row-major)
    void setImage(const uint8_t* rgbPixels, int width, int height);

    // Clear the display
    void clearImage();

    bool hasImage() const;
    int imageWidth() const;
    int imageHeight() const;

    // Save the current image to a file (PNG/BMP via QImage)
    bool saveImage(const QString& filePath) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    bool hasImage_;
};

} // namespace audiot
#endif // AUDIOT_SSTV_VIEWER_H
