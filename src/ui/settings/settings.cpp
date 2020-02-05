#include "settings.h"

#include "ui_settings.h"

#include <ui/settings/camerainfo.h>
#include <ui/settings/microphoneinfo.h>
#include "settingshelper.h"

#include <QDebug>

Settings::Settings(QWidget *parent) :
  QDialog(parent),
  basicUI_(new Ui::BasicSettings),
  cam_(std::shared_ptr<CameraInfo> (new CameraInfo())),
  mic_(std::shared_ptr<MicrophoneInfo> (new MicrophoneInfo())),
  advanced_(this),
  custom_(this, cam_),
  settings_("kvazzup.ini", QSettings::IniFormat)
{}


Settings::~Settings()
{
  // I believe the UI:s are destroyed when parents are destroyed
}


void Settings::init()
{
  basicUI_->setupUi(this);

  // Checks that settings values are correct for the program to start. Also sets GUI.
  getSettings(false);

  custom_.init(getDeviceID(basicUI_->videoDevice, "video/DeviceID", "video/Device"));
  advanced_.init();

  //QObject::connect(basicUI_->save, &QPushButton::clicked, this, &Settings::on_ok_clicked);
  //QObject::connect(basicUI_->close, &QPushButton::clicked, this, &Settings::on_cancel_clicked);

  QObject::connect(&custom_, &CustomSettings::customSettingsChanged,
                   this, &Settings::settingsChanged);
  QObject::connect(&custom_, &CustomSettings::hidden, this, &Settings::show);

  QObject::connect(&advanced_, &AdvancedSettings::advancedSettingsChanged,
                   this, &Settings::settingsChanged);
  QObject::connect(&advanced_, &AdvancedSettings::hidden,
                   this, &Settings::show);

  QObject::connect(basicUI_->serverAddress, &QLineEdit::textChanged,
                   this, &Settings::changedSIPText);
  QObject::connect(basicUI_->username, &QLineEdit::textChanged,
                   this, &Settings::changedSIPText);
}


void Settings::show()
{
  initDeviceSelector(basicUI_->videoDevice, "video/DeviceID", "video/Device", cam_); // initialize everytime in case they have changed
  initDeviceSelector(basicUI_->audioDevice, "audio/DeviceID", "audio/Device", mic_); // initialize everytime in case they have changed
  QWidget::show();
}


void Settings::on_save_clicked()
{
  qDebug() << "Settings," << metaObject()->className() << ": Saving basic settings";
  // The UI values are saved to settings.
  saveSettings();
  emit settingsChanged(); // TODO: check have the settings actually been changed
}


void Settings::on_close_clicked()
{
  qDebug() << "Settings," << metaObject()->className()
           << ": Cancel clicked. Getting settings from system";

  // discard UI values and restore the settings from file
  getSettings(false);
  hide();
}


void Settings::on_advanced_settings_button_clicked()
{
  saveSettings();
  hide();
  advanced_.show();
}


void Settings::on_custom_settings_button_clicked()
{
  saveSettings();
  hide();
  custom_.show();
}



// records the settings
void Settings::saveSettings()
{
  qDebug() << "Settings," << metaObject()->className() << ": Saving basic Settings";

  // Local settings
  saveTextValue("local/Name", basicUI_->name_edit->text(), settings_);
  saveTextValue("local/Username", basicUI_->username->text(), settings_);
  saveTextValue("sip/ServerAddress", basicUI_->serverAddress->text(), settings_);

  saveCheckBox("sip/AutoConnect", basicUI_->autoConnect, settings_);

  saveCheckBox("sip/kvzRTP", basicUI_->kvzRTP, settings_);

  saveDevice(basicUI_->videoDevice, "video/DeviceID", "video/Device", true);
  saveDevice(basicUI_->audioDevice, "audio/DeviceID", "audio/Device", false);
}


// restores recorded settings
void Settings::getSettings(bool changedDevice)
{
  initDeviceSelector(basicUI_->videoDevice, "video/DeviceID", "video/Device", cam_);
  initDeviceSelector(basicUI_->videoDevice, "audio/DeviceID", "audio/Device", mic_);

  //get values from QSettings
  if(checkMissingValues() && checkUserSettings())
  {
    qDebug() << "Settings," << metaObject()->className()
             << ": Restoring user settings from file:" << settings_.fileName();
    basicUI_->name_edit->setText      (settings_.value("local/Name").toString());
    basicUI_->username->setText  (settings_.value("local/Username").toString());

    basicUI_->serverAddress->setText(settings_.value("sip/ServerAddress").toString());

    restoreCheckBox("sip/AutoConnect", basicUI_->autoConnect, settings_);

    // updates the sip text label
    changedSIPText("");

    restoreCheckBox("sip/kvzrtp", basicUI_->kvzRTP, settings_);

    int videoIndex = getDeviceID(basicUI_->videoDevice, "video/DeviceID", "video/Device");
    if(changedDevice)
    {
      custom_.changedDevice(videoIndex);
    }
    basicUI_->videoDevice->setCurrentIndex(videoIndex);

    int audioIndex = getDeviceID(basicUI_->audioDevice, "audio/DeviceID", "audio/Device");
    basicUI_->audioDevice->setCurrentIndex(audioIndex);
  }
  else
  {
    resetFaultySettings();
  }
}


void Settings::resetFaultySettings()
{
  qDebug() << "WARNING," << metaObject()->className()
           << ": Could not restore settings because they were corrupted!";
  // record GUI settings in hope that they are correct ( is case by default )
  saveSettings();
  custom_.resetSettings(getDeviceID(basicUI_->videoDevice, "video/DeviceID", "video/Device"));
}


void Settings::initDeviceSelector(QComboBox* deviceSelector,
                                      QString settingID,
                                      QString settingsDevice,
                                      std::shared_ptr<DeviceInfoInterface> interface)
{
  qDebug() << "Settings," << metaObject()->className() << ": Initialize device list";
  deviceSelector->clear();
  QStringList devices = interface->getDeviceList();
  for(int i = 0; i < devices.size(); ++i)
  {
    deviceSelector->addItem( devices[i]);
  }
  int deviceIndex = getDeviceID(deviceSelector, settingID, settingsDevice);

  if(deviceIndex >= deviceSelector->count())
  {
    deviceSelector->setCurrentIndex(0);
  }
  else
  {
    deviceSelector->setCurrentIndex(deviceIndex);
  }
}


int Settings::getDeviceID(QComboBox* deviceSelector, QString settingID, QString settingsDevice)
{
  int deviceIndex = deviceSelector->findText(settings_.value(settingsDevice).toString());
  int deviceID = settings_.value(settingID).toInt();

  qDebug() << "Settings," << metaObject()->className()
           << "Get device id: Index:" << deviceIndex << "deviceID:"
           << deviceID << "Name:" << settings_.value(settingsDevice).toString();

  // if the device exists in list
  if(deviceIndex != -1 && deviceSelector->count() != 0)
  {
    // if we have multiple devices with same name we use id
    if(deviceID != deviceIndex
       && deviceSelector->itemText(deviceID) == settings_.value(settingsDevice).toString())
    {
      return deviceID;
    }
    else
    {
      // the recorded info was false and our found device is chosen
      settings_.setValue(settingID, deviceIndex);
      return deviceIndex;
    }
  } // if there are devices available, use first
  else if(deviceSelector->count() != 0)
  {
    // could not find the device. Choosing first one
    settings_.setValue(settingID, 0);
    return 0;
  }

  // no devices attached
  return -1;
}


void Settings::saveDevice(QComboBox* deviceSelector, QString settingsID, QString settingsDevice, bool video)
{
  int currentIndex = deviceSelector->currentIndex();
  if( currentIndex != -1)
  {
    if(deviceSelector->currentText() != settings_.value(settingsDevice))
    {
      settings_.setValue(settingsDevice,        deviceSelector->currentText());
      // set capability to first

      if (video)
      {
        custom_.changedDevice(currentIndex);
      }
    }
    else if(basicUI_->videoDevice->currentIndex() != settings_.value(settingsID))
    {
      if (video)
      {
        custom_.changedDevice(currentIndex);
      }
    }

    // record index in all cases
    settings_.setValue(settingsID,      currentIndex);
  }
}


void Settings::changedSIPText(const QString &text)
{
  Q_UNUSED(text);
  basicUI_->sipAddress->setText("sip:" + basicUI_->username->text()
                                + "@" + basicUI_->serverAddress->text());
}


void Settings::updateServerStatus(ServerStatus status)
{

  switch (status)
  {
    case DISCONNECTED:
    {
      basicUI_->status->setText("Disconnected");
      break;
    }
    case IN_PROCESS:
    {
      basicUI_->status->setText("Connecting...");
      break;
    }
    case BEHIND_NAT:
    {
      basicUI_->status->setText("Online, but behind NAT");
      break;
    }
    case REGISTERED:
    {
      basicUI_->status->setText("Online");
      break;
    }
    case SERVER_FAILED:
    {
      basicUI_->status->setText("Failed to register");
      break;
    }
  }
}


bool Settings::checkUserSettings()
{
  return settings_.contains("local/Name")
      && settings_.contains("local/Username");
}


bool Settings::checkMissingValues()
{
  QStringList list = settings_.allKeys();

  bool foundEverything = true;
  for(auto& key : list)
  {
    if(settings_.value(key).isNull() || settings_.value(key) == "")
    {
      qDebug() << "WARNING," << metaObject()->className() << ": MISSING SETTING FOR:" << key;
      foundEverything = false;
    }
  }
  return foundEverything;
}
