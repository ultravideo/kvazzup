#include "filter.h"

#include "statisticsinterface.h"
#include "yuvconversions.h"

#include "logger.h"

#include <QImage>
#include <QDebug>

#include <thread>


const std::map<DataType, QString> typeString = {
  {DT_NONE, "None"},
  {DT_YUV420VIDEO, "YUV 420"},
  {DT_YUV422VIDEO, "YUV 422"},
  {DT_NV12VIDEO, "NV 12"},
  {DT_NV21VIDEO, "NV 21"},
  {DT_YUYVVIDEO, "YUYV"},
  {DT_UYVYVIDEO, "UYVY"},
  {DT_ARGBVIDEO, "ARGB"},
  {DT_BGRAVIDEO, "BGRA"},
  {DT_ABGRVIDEO, "ABGR"},
  {DT_RGB32VIDEO, "RGB32"},
  {DT_RGB24VIDEO, "RGB24"},
  {DT_BGRXVIDEO, "BGRX"},
  {DT_MJPEGVIDEO, "MJPEG"},
  {DT_HEVCVIDEO, "HEVC"},
  {DT_RAWAUDIO, "RAW"},
  {DT_OPUSAUDIO, "OPUS"}
};


QString datatypeToString(const DataType type)
{
  if (typeString.find(type) == typeString.end())
  {
    Logger::getLogger()->printUnimplemented("Filter", "String conversion does not know this datatype!");
    return "Unknown";
  }
  return typeString.at(type);
}

Filter::Filter(QString id, QString name, StatisticsInterface *stats,
               std::shared_ptr<ResourceAllocator> hwResources,
               DataType input, DataType output, bool enforceFramerate):
  maxBufferSize_(10),
  input_(input),
  output_(output),
  name_(name),
  id_(id),
  stats_(stats),
  waitMutex_(new QMutex),
  hasInput_(),
  running_(true),
  inputTaken_(0),
  inputDiscarded_(0),
  hwResources_(hwResources),
  filterID_(0),
  enforceFramerate_(enforceFramerate),
  synchronizationPoint_(std::chrono::high_resolution_clock::now()),
  framesSinceSynchronization_(0),
  framerateNumerator_(0),
  framerateDenominator_(0)
{
  Q_ASSERT(hwResources != nullptr);
}

Filter::~Filter()
{
  delete waitMutex_;
}


void Filter::updateSettings()
{}


bool Filter::init()
{
  return true;
}


bool Filter::isVideo(DataType type) const
{
  return type == DT_YUV420VIDEO ||
    type == DT_YUV422VIDEO ||
    type == DT_NV12VIDEO ||
    type == DT_NV21VIDEO ||
    type == DT_YUYVVIDEO ||
    type == DT_UYVYVIDEO ||

    type == DT_ARGBVIDEO ||
    type == DT_BGRAVIDEO ||
    type == DT_ABGRVIDEO ||
    type == DT_RGB32VIDEO ||

    type == DT_RGB24VIDEO ||
    type == DT_BGRXVIDEO ||

    type == DT_MJPEGVIDEO ||
    type == DT_HEVCVIDEO;
}


bool Filter::isAudio(DataType type) const
{
  return type == DT_RAWAUDIO ||
      type == DT_OPUSAUDIO;
}


void Filter::addOutConnection(std::shared_ptr<Filter> out)
{
  outConnections_.push_back(out);
}

void Filter::removeOutConnection(std::shared_ptr<Filter> out)
{
  bool removed = false;
  connectionMutex_.lock();
  for(unsigned int i = 0; i < outConnections_.size(); ++i)
  {
    if(outConnections_[i].get() == out.get())
    {
      outConnections_.erase(outConnections_.begin() + i);
      removed = true;
      break;
    }
  }
  connectionMutex_.unlock();

  if(!removed)
  {
    Logger::getLogger()->printDebug(DEBUG_WARNING, this,
                                    "Did not succeed at removing outconnection.");
  }
}

void Filter::emptyBuffer()
{
  bufferMutex_.lock();
  //std::queue<std::unique_ptr<Data>> empty;
  std::deque<std::unique_ptr<Data>> empty;
  std::swap( inBuffer_, empty );
  bufferMutex_.unlock();
}

void Filter::putInput(std::unique_ptr<Data> data)
{
  Q_ASSERT(data);

#ifndef NDEBUG
  bool ok = true;
  data = validityCheck(std::move(data), ok);

  if(!ok)
  {
    Logger::getLogger()->printDebug(DEBUG_WARNING, this,  "Discarding bad data");
    return;
  }
#endif

  ++inputTaken_;

  bufferMutex_.lock();

  if(inputTaken_%30 == 0)
  {
    stats_->updateBufferStatus(filterID_, (uint16_t)inBuffer_.size(), maxBufferSize_);
  }

  inBuffer_.push_back(std::move(data));

  if(maxBufferSize_ != -1 && inBuffer_.size() >= (uint32_t)maxBufferSize_)
  {
    if(inBuffer_[0]->type == DT_HEVCVIDEO)
    {
      // Search for intra frames and discard everything up to it
      for(uint32_t i = 0; i < inBuffer_.size(); ++i)
      {
        const unsigned char *buff = inBuffer_.at(i)->data.get();
        if(!isHEVCIntra(buff))
        {
          Logger::getLogger()->printWarning(this, "Discarding HEVC frames from buffer. Finding next intra",
                                            "Frames discarded", QString::number(i));

          for(int j = i; j != 0; --j)
          {
            inBuffer_.pop_front();
          }
          break;
        }
      }
    }
    else
    {
      if(inBuffer_[0]->type == DT_OPUSAUDIO)
      {
        Logger::getLogger()->printDebug(DEBUG_WARNING, this,  
                                        "Should input Null pointer to opus decoder.");
      }
      inBuffer_.pop_front(); // discard the oldest
    }

    ++inputDiscarded_;
    stats_->packetDropped(filterID_);

    if (inputDiscarded_ == 1 || inputDiscarded_%10 == 0)
    {
      Logger::getLogger()->printDebug(DEBUG_WARNING, this, "Buffer too full",
                                      {"Name", "Discarded/total input"},
                                      {name_, QString::number(inputDiscarded_) + "/" +
                                              QString::number(inputTaken_)});
    }
  }
  wakeUp();

  bufferMutex_.unlock();
}


std::unique_ptr<Data> Filter::initializeData(DataType type, DataSource source) const
{
  std::unique_ptr<Data> data(new Data);
  data->type = type;
  data->source = source;
  data->data_size = 0;
  data->creationTimestamp = 0;
  data->presentationTimestamp = 0;

  if (isVideo(type))
  {
    data->vInfo = std::unique_ptr<VideoInfo> (new VideoInfo);
    data->vInfo->width = 0; // not known at this point. Decoder tells the correct resolution
    data->vInfo->height = 0;
    data->vInfo->framerateNumerator = 0;
    data->vInfo->framerateDenominator = 0;
    data->vInfo->flippedVertically = false;
    data->vInfo->flippedHorizontally = false;

    data->vInfo->roi.width = 0;
    data->vInfo->roi.height = 0;

    std::shared_ptr<uint8_t[]> roiArray = nullptr;
  }
  else if (isAudio(type))
  {
    data->aInfo = std::unique_ptr<AudioInfo> (new AudioInfo);
    data->aInfo->sampleRate = 0;
  }
  else
  {
    Logger::getLogger()->printProgramError(this, "Could not determine input data type!");
  }

  return data;
}


std::unique_ptr<Data> Filter::normalizeOrientation(std::unique_ptr<Data> video,
                                                   bool forceHorizontalFlip)
{
  if (video->type != DT_RGB32VIDEO)
  {
    Logger::getLogger()->printProgramError(this, "Not correct "
                                                 "data type for flipping");
    return video;
  }

  if (forceHorizontalFlip || video->vInfo->flippedHorizontally ||
      video->vInfo->flippedVertically)
  {

    uint32_t finalDataSize = video->vInfo->width*video->vInfo->height*4;
    std::unique_ptr<uchar[]> flipped_data(new uchar[finalDataSize]);

    flip_rgb(video->data.get(), flipped_data.get(), video->vInfo->width, video->vInfo->height,
             forceHorizontalFlip || video->vInfo->flippedHorizontally, video->vInfo->flippedVertically);


    if (forceHorizontalFlip || video->vInfo->flippedHorizontally)
    {
      video->vInfo->flippedHorizontally = !video->vInfo->flippedHorizontally;
    }
    video->vInfo->flippedVertically = false;

    video->data = std::move(flipped_data);
  }

  return video;
}


std::unique_ptr<Data> Filter::getInput()
{
  bufferMutex_.lock();
  std::unique_ptr<Data> r;
  if(!inBuffer_.empty())
  {
    r = std::move(inBuffer_.front());
    inBuffer_.pop_front();
  }
  bufferMutex_.unlock();

  // optional enforcement of smooth frame rate, only done if there was input
  // TODO: Does not work at the moment
  if (enforceFramerate_ && r && r->vInfo)
  {
    std::chrono::time_point<std::chrono::high_resolution_clock> timeSlot =
        getFrameTimepoint();
    std::chrono::time_point<std::chrono::high_resolution_clock> now =
        std::chrono::high_resolution_clock::now();

    // is this frame late or has frame rate changed
    if (r->vInfo->framerateNumerator != framerateNumerator_ ||
        r->vInfo->framerateDenominator != framerateDenominator_ || now > timeSlot)
    {
      /*
      Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Updating frame rate synchronization point.",
                                      {"Arrival time"},
                                      {QString::number(std::chrono::duration_cast<
                                       std::chrono::milliseconds>
                                       (timeSlot - now).count()) + " ms"});
                                       */
      resetSynchronizationPoint(r->vInfo->framerateNumerator, r->vInfo->framerateDenominator);
    }
    else
    {
      // wait until timeslot
      std::this_thread::sleep_for(timeSlot - now);
    }

    ++framesSinceSynchronization_;
  }

  return r;
}


std::chrono::time_point<std::chrono::high_resolution_clock> Filter::getFrameTimepoint()
{
  int flexibility1ms = 1000000;

  double framerate = (double)framerateNumerator_/framerateDenominator_;

  return synchronizationPoint_ +
      std::chrono::nanoseconds((uint64_t)(1000000000*framesSinceSynchronization_/framerate + flexibility1ms));
}


void Filter::resetSynchronizationPoint(int32_t framerateNumerator,
                                       int32_t framerateDenominator)
{
  framerateNumerator_ = framerateNumerator;
  framerateDenominator_ = framerateDenominator;
  framesSinceSynchronization_ = 0;
  synchronizationPoint_ = std::chrono::high_resolution_clock::now();
}


void Filter::sendOutput(std::unique_ptr<Data> output)
{
  Q_ASSERT(output);

  if(outDataCallbacks_.size() == 0 && outConnections_.size() == 0)
  {
    Logger::getLogger()->printDebug(DEBUG_WARNING, this, 
                                    "Trying to send output data without outconnections.");
    return;
  }

  // TODO: If data is HEVC, I think we can safely use shallowcopy

  connectionMutex_.lock();
  // copy data to callbacks expect the last one is moved
  // in either callbacks or outconnections(default).
  if(outDataCallbacks_.size() != 0)
  {
    // all expect the last
    for(unsigned int i = 0; i < outDataCallbacks_.size() - 1; ++i)
    {
      Data* copy = deepDataCopy(output.get());
      std::unique_ptr<Data> u_copy(copy);
      outDataCallbacks_[i](std::move(u_copy));
    }

    // copy last callback and move last connection
    if(outConnections_.size() != 0)
    {
      Data* copy = deepDataCopy(output.get());
      std::unique_ptr<Data> u_copy(copy);
      outDataCallbacks_.back()(std::move(u_copy));
    }
    else // move last callback
    {
      outDataCallbacks_.back()(std::move(output));
    }
  }

  // handle all connected filters.
  if(outConnections_.size() != 0)
  {
    // all expect the last
    for(unsigned int i = 0; i < outConnections_.size() - 1; ++i)
    {
      Data* copy = deepDataCopy(output.get());
      std::unique_ptr<Data> u_copy(copy);
      outConnections_[i]->putInput(std::move(u_copy));
    }
    // always move the last outconnection
    outConnections_.back()->putInput(std::move(output));
  }
  connectionMutex_.unlock();
}

void Filter::stop()
{
  running_ = false;
  hasInput_.wakeAll();
}

void Filter::run()
{
  if (stats_ != nullptr)
  {
    filterID_ = stats_->addFilter(name_, id_, (uint64_t)currentThreadId());
    while(running_)
    {
      waitForInput();
      if(!running_) break;

      process();
    }
    if (filterID_ != 0)
    {
      stats_->removeFilter(filterID_);
      filterID_ = 0;
    }
  }
}

Data* Filter::shallowDataCopy(Data* original) const
{
  if(original != nullptr)
  {
    Data* copy = new Data;
    copy->type = original->type;

    copy->source = original->source;

    copy->creationTimestamp = original->creationTimestamp;
    copy->presentationTimestamp = original->presentationTimestamp;

    copy->data_size = 0; // no data in shallow copy

    if (original->vInfo != nullptr)
    {
      copy->vInfo = std::unique_ptr<VideoInfo> (new VideoInfo);

      copy->vInfo->width               = original->vInfo->width;
      copy->vInfo->height              = original->vInfo->height;
      copy->vInfo->framerateNumerator  = original->vInfo->framerateNumerator;
      copy->vInfo->framerateDenominator  = original->vInfo->framerateDenominator;
      copy->vInfo->flippedHorizontally = original->vInfo->flippedHorizontally;
      copy->vInfo->flippedVertically   = original->vInfo->flippedVertically;
    }

    if (original->aInfo != nullptr)
    {
      copy->aInfo = std::unique_ptr<AudioInfo> (new AudioInfo);

      copy->aInfo->sampleRate = original->aInfo->sampleRate;
    }

    return copy;
  }
  Logger::getLogger()->printDebug(DEBUG_WARNING, this, 
                                  "Trying to copy nullptr Data pointer.");
  return nullptr;
}

Data* Filter::deepDataCopy(Data* original) const
{
  if(original != nullptr)
  {
    Data* copy = shallowDataCopy(original);
    copy->data = std::unique_ptr<uchar[]>(new uchar[original->data_size]);
    memcpy(copy->data.get(), original->data.get(), original->data_size);
    copy->data_size = original->data_size;

    return copy;
  }
  Logger::getLogger()->printDebug(DEBUG_WARNING, this, 
                                  "Trying to copy nullptr Data pointer.");
  return nullptr;
}


QString Filter::printOutputs()
{
  QString outs = "";

  for(auto& out : outConnections_)
  {
    outs += "   \"" + name_ + "\" -> \"" + out->name_ + "\";" + "\r\n";
  }

  outs += "plus " + QString::number(outDataCallbacks_.size()) + " callbacks";
  return outs;
}


bool Filter::isHEVCIntra(const unsigned char *buff) const
{
  return (buff[0] == 0 &&
      buff[1] == 0 &&
      buff[2] == 0 &&
      buff[3] == 1 &&
      (buff[4] >> 1) == 19);
}

bool Filter::isHEVCInter(const unsigned char *buff) const
{
  return (buff[0] == 0 &&
      buff[1] == 0 &&
      buff[2] == 0 &&
      buff[3] == 1 &&
      (buff[4] >> 1) == 1);
}


std::unique_ptr<Data> Filter::validityCheck(std::unique_ptr<Data> data, bool& ok)
{
  ok = true;

  if(data->source == DS_UNKNOWN
     || data->type == DT_NONE
     || data->data_size == 0)
  {
    Logger::getLogger()->printWarning(this,  "Invalid data detected");
    ok = false;
  }

  if(isAudio(data->type) &&
     data->aInfo == nullptr)
  {
    Logger::getLogger()->printWarning(this,  "No audio info for audio");
    ok = false;
  }

  if(isVideo(data->type) &&
     data->vInfo == nullptr)
  {
    Logger::getLogger()->printWarning(this,  "No video info for video");
    ok = false;
  }

  return data;
}

void Filter::printDataBytes(QString type, const uint8_t *payload, size_t size, int bytes, int shift)
{
  QString bytesString = "";

  if (bytes > size)
  {
    bytes = size;
  }

  for (int i = 0; i < bytes; ++i)
  {
    bytesString += QString::number(payload[i] << shift) + " ";
  }

  Logger::getLogger()->printNormal(this, type + ": " + bytesString, "size", QString::number(size));
}
