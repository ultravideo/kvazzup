#pragma once

#include "ui/settings/audiosettings.h"
#include "ui/settings/videosettings.h"
#include "ui/settings/sipsettings.h"

#include <QDialog>
#include <QSettings>

#include <memory>

#include "serverstatusview.h"

/* Settings in Kvazzup work as follows:
 * 1) The settings view holds the setting information in a way that the user
 * can modify it. 2) This Settings class monitors users modifications and
 * records them to a file using QSettings. The file is loaded when the user
 * opens the settings dialog. 3) The rest of the program may use these settings
 * through QSettings class to change its behavior based on users choices.
 *
 * In other words this class synchronizes the settings between UI,
 * QSettings and the settings file (through QSettings).
 *
 * Modifying the settings are done in these settings classes and reading can
 * be done anywhere in the program.
 */

namespace Ui {
class BasicSettings;
}

class CameraInfo;
class MicrophoneInfo;
class ScreenInfo;
class DeviceInfoInterface;
class QCheckBox;
class QComboBox;


class Settings : public QDialog, public ServerStatusView
{
  Q_OBJECT

public:
  explicit Settings(QWidget *parent = nullptr);
  ~Settings();

  void init();

  void updateDevices();

  void updateServerStatus(QString status);

  void setMicState(bool enabled);
  void setCameraState(bool enabled);
  void setScreenShareState(bool enabled);

signals:

  // announces to rest of the program that they should reload their settings
  void updateCallSettings();
  void updateVideoSettings();
  void updateAudioSettings();

public slots:

  virtual void show();

  // button slots, called automatically by Qt
  void on_save_clicked();
  void on_close_clicked();
  void on_sip_settings_button_clicked();
  void on_video_settings_button_clicked();
  void on_audio_settings_button_clicked();

  void changedSIPText(const QString &text);

  void uiChangedString(QString text);
  void uiChangedBool(bool state);

private:

  // Make sure the UI video devices are initialized before calling this.
  // This function tries to get the best guess at what is the current device
  // even in case devices have dissappeared/appeared since recording of information.
  int getDeviceID(QComboBox *deviceSelector, QString settingID, QString settingsDevice);

  void initDeviceSelector(QComboBox* deviceSelector, QString settingID,
                          QString settingsDevice, std::shared_ptr<DeviceInfoInterface> interface);

  void saveDevice(QComboBox* deviceSelector, QString settingsID, QString settingsDevice, bool video);

  // checks if user settings make sense
  // TODO: display errors to user on ok click

  // QSettings -> GUI
  void getSettings(bool changedDevice);

  // GUI -> QSettings
  // permanently records GUI settings
  void saveSettings();


  void resetFaultySettings();

  Ui::BasicSettings *basicUI_;

  std::shared_ptr<CameraInfo> cam_;
  std::shared_ptr<MicrophoneInfo> mic_;
  std::shared_ptr<ScreenInfo> screen_;

  SIPSettings sipSettings_;
  VideoSettings videoSettings_;
  AudioSettings audioSettings_;

  QSettings settings_;
};
