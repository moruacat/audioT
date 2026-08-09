// noise_profile_dialog.h - Dialog for importing noise WAV and showing profile info
#ifndef AUDIOT_NOISE_PROFILE_DIALOG_H
#define AUDIOT_NOISE_PROFILE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>

#include "core/noise/noise_profile.h"

namespace audiot {

// Lets the user import a noise-only WAV recording, builds a NoiseProfile
// from it and displays the profile statistics (RMS, peak, spectral centroid).
class NoiseProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit NoiseProfileDialog(QWidget* parent = nullptr);

    // Access the currently loaded profile (valid() indicates success)
    const NoiseProfile& profile() const;

private slots:
    void onBrowseWav();
    void onImport();
    void onSaveProfile();

private:
    void setupUI();
    void refreshInfo();

    QLineEdit* wavPath_;
    QPushButton* btnBrowse_;
    QPushButton* btnImport_;
    QPushButton* btnSave_;
    QPlainTextEdit* infoView_;

    NoiseProfile profile_;
};

} // namespace audiot
#endif // AUDIOT_NOISE_PROFILE_DIALOG_H
