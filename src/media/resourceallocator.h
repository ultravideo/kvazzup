#pragma once

#include "processing/filter.h"

#include <QObject>

/* The purpose of this class is the enable filters to easily query the
 * state of hardware in terms of possible optimizations and performance. */

struct StreamInfo
{
  uint32_t previousJitter;
  int32_t  previousLost;

  int bitrate;
};

class ResourceAllocator : public QObject
{
  Q_OBJECT
public:
  ResourceAllocator();

  void updateSettings();

  bool isAVX2Enabled();
  bool isSSE41Enabled();

  bool useManualROI();
  bool useAutoROI();

  uint16_t getRoiObject() const;

  void addRTCPReport(uint32_t sessionID, DataType type,
                     int32_t lost, uint32_t jitter);

  int getBitrate(DataType type);


  uint8_t getRoiQp() const;
  uint8_t getBackgroundQp() const;

private:

  void updateGlobalBitrate(int& bitrate,
                           std::map<uint32_t, std::shared_ptr<StreamInfo> > &streams);

  std::shared_ptr<StreamInfo> getStreamInfo(uint32_t sessionID, DataType type);

  void limitBitrate(int &bitrate, DataType type);

  bool avx2_ = false;
  bool sse41_ = false;

  bool manualROI_ = false;
  bool autoROI_ = false;

  // key is sessionID
  std::map<uint32_t, std::shared_ptr<StreamInfo>> audioStreams_;
  std::map<uint32_t, std::shared_ptr<StreamInfo>> videoStreams_;

  QMutex bitrateMutex_;
  int videoBitrate_;
  int audioBitrate_;

  uint8_t roiQp_;
  uint8_t backgroundQp_;

  uint16_t roiObject_;
};
