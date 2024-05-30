#pragma once

#include "deviceinfointerface.h"

#include <QStringList>
#include <QSize>
#include <QtMultimedia/QCamera>

#include <memory>

struct SettingsCameraFormat
{
  QString deviceName;
  int deviceID;

  QString format;
  QSize resolution;
  QString framerate;
};

// A helper class for settings.

class CameraInfo : public DeviceInfoInterface
{
public:
  CameraInfo();

  // returns a list of devices.
  virtual QStringList getDeviceList();

  // get formats for a device.
  void getVideoFormats(int deviceID, QStringList& formats);

  // get resolutions for a format.
  void getFormatResolutions(int deviceID, QString format, QStringList& resolutions);

  // get resolutions for a format.
  void getFramerates(int deviceID, QString format, QString resolution, QStringList& ranges);

  QString getFormat(int deviceID, QString format);

  // format must be verified by calling getFormat() before calling getResolution()
  QSize   getResolution(int deviceID, QString format, QString resolution);
  int     getFramerate(int deviceID, QString format, QString resolution, QString framerate);

  QCameraFormat getVideoFormat(int deviceID, QString format, QString resolution, QString framerate);

  void getCameraOptions(std::vector<SettingsCameraFormat>& options, int deviceID);

private:

  void getAllowedFormats(QList<QVideoFrameFormat::PixelFormat>& p_formats,
                         QStringList& allowedFormats);

  std::unique_ptr<QCamera> loadCamera(int deviceID);

  void printFormatOption(QCameraFormat& formatOption) const;

  bool goodResolution(QSize resolution);
};
