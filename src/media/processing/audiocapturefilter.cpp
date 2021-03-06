#include "audiocapturefilter.h"

#include "statisticsinterface.h"
#include "audioframebuffer.h"

#include "common.h"
#include "settingskeys.h"
#include "global.h"
#include "logger.h"

#include <QAudioInput>
#include <QTime>
#include <QSettings>
#include <QRegularExpression>


AudioCaptureFilter::AudioCaptureFilter(QString id, QAudioFormat format,
                                       StatisticsInterface *stats):
  Filter(id, "Audio_Capture", stats, NONE, RAWAUDIO),
  deviceInfo_(),
  format_(format),
  audioInput_(nullptr),
  input_(nullptr),
  readBuffer_(nullptr),
  readBufferSize_(0),
  wantedState_(QAudio::StoppedState),
  buffer_(nullptr)
{}


AudioCaptureFilter::~AudioCaptureFilter()
{
  destroyReadBuffer();
}


bool AudioCaptureFilter::init()
{
  Logger::getLogger()->printNormal(this, "Initializing audio capture filter.");

  QList<QAudioDeviceInfo> microphones = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);

  if (!microphones.empty())
  {
    QSettings settings(settingsFile, settingsFileFormat);
    QString deviceName = settings.value(SettingsKey::audioDevice).toString();
    int deviceID = settings.value(SettingsKey::audioDeviceID).toInt();

    if (deviceID < microphones.size())
    {
      QString parsedName = microphones[deviceID].deviceName();
      // take only the device name from: "Microphone (device name)"
      QRegularExpression re_mic (".*\\((.+)\\).*");
      QRegularExpressionMatch mic_match = re_mic.match(microphones[deviceID].deviceName());

      if (mic_match.hasMatch() && mic_match.lastCapturedIndex() == 1)
      {
        // parsed extra text succesfully
        parsedName = mic_match.captured(1);
      }

      // if the device has changed between recording the settings and now.
      if (parsedName != deviceName)
      {
        // search for device with same name
        for(int i = 0; i < microphones.size(); ++i)
        {
          if(parsedName == deviceName)
          {
            deviceID = i;
            Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Found Mic.", {"Name", "ID"},
                        {microphones.at(i).deviceName(), QString::number(deviceID)});
            break;
          }
        }
        // previous camera could not be found, use first.
        Logger::getLogger()->printWarning(this, "Did not find selected microphone. Using first.",
                    {"Device name"}, {deviceName});
        deviceID = 0;
      }
    }
    deviceInfo_ = microphones.at(deviceID);
  }
  else
  {
    Logger::getLogger()->printWarning(this, "No available microphones found. Trying default.");
    deviceInfo_ = QAudioDeviceInfo::defaultInputDevice();
  }

  QAudioDeviceInfo info(deviceInfo_);

  Logger::getLogger()->printNormal(this, "A microphone chosen.", {"Device name"}, {info.deviceName()});

  if (!info.isFormatSupported(format_)) {
    Logger::getLogger()->printWarning(this, "Default audio format not supported - "
                                            "trying to use nearest");
    format_ = info.nearestFormat(format_);
  }

  if(format_.sampleRate() != -1)
    getStats()->audioInfo(format_.sampleRate(), format_.channelCount());
  else
    getStats()->audioInfo(0, 0);

  createAudioInput();
  Logger::getLogger()->printNormal(this, "Audio initializing completed.");
  return true;
}


void AudioCaptureFilter::createAudioInput()
{
  audioInput_ = new QAudioInput(deviceInfo_, format_, this);
  if (audioInput_)
  {
    // it is possible to reduce the buffer size here to reduce latency, but this
    // causes issues with audio reliability with Qt and is not recommended.

    input_ = audioInput_->start();

    int frameSize = format_.sampleRate()*format_.bytesPerFrame()/AUDIO_FRAMES_PER_SECOND;

    // here we input the samples to be made the right size for our application
    buffer_ = std::make_unique<AudioFrameBuffer>(frameSize);

    createReadBuffer(audioInput_->bufferSize());

    if (input_)
    {
      connect(input_, SIGNAL(readyRead()), SLOT(readMore()));
    }
  }
  wantedState_ = QAudio::ActiveState;

  connect(audioInput_, &QAudioInput::stateChanged,
          this,        &AudioCaptureFilter::stateChanged);

  Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Created audio input",
             {"Notify interval", "Buffer size", "Period Size"},
             {QString::number(audioInput_->notifyInterval()),
              QString::number(audioInput_->bufferSize()),
              QString::number(audioInput_->periodSize())});
}


void AudioCaptureFilter::readMore()
{
  if (!audioInput_ || !input_)
  {
    Logger::getLogger()->printProgramWarning(this,  "No audio input in readMore");
    return;
  }

  int micBufferSize = audioInput_->bufferSize();

  if (micBufferSize != readBufferSize_)
  {
    Logger::getLogger()->printWarning(this, "Mic changed buffer size");
    createReadBuffer(audioInput_->bufferSize());
  }

  while (audioInput_->bytesReady() >= audioInput_->periodSize())
  {
    qint64 len = audioInput_->bytesReady();

    if (len >= 3*micBufferSize/2)
    {
      Logger::getLogger()->printWarning(this, "Microphone buffer is 75 % full. "
                                              "Possibly losing audio soon", {"Amount"},
                   QString::number(len) + "/" + QString::number(audioInput_->bufferSize()));
    }

    if (len > readBufferSize_)
    {
      Logger::getLogger()->printWarning(this, "Mic has too much input to read all at once");
      len = readBufferSize_;
    }

    qint64 readData = input_->read(readBuffer_, len);

    if (readData == 0)
    {
      Logger::getLogger()->printWarning(this, "Failed to read any data",
      {"Bytes attempted"}, {QString::number(len)});
      break;
    }
    else if (readData == -1)
    {
      Logger::getLogger()->printWarning(this, "Error reading data from mic IODevice!",
      {"Amount"}, {QString::number(len)});
      break;
    }
    else if (readData > 0)
    {
      buffer_->inputData((uint8_t*)readBuffer_, readData);
    }

    uint8_t* frame = buffer_->readFrame();

    while (frame != nullptr)
    {
      Data* audioFrame = new Data;

      // create audio data packet to be sent to filter graph
      audioFrame->presentationTime = QDateTime::currentMSecsSinceEpoch();
      audioFrame->type = RAWAUDIO;

      audioFrame->data_size = buffer_->getDesiredSize();
      audioFrame->data = std::unique_ptr<uint8_t[]>(frame);

      audioFrame->width = 0;
      audioFrame->height = 0;
      audioFrame->source = LOCAL;
      audioFrame->framerate = format_.sampleRate();

      std::unique_ptr<Data> u_newSample( audioFrame );
      sendOutput(std::move(u_newSample));

      //Logger::getLogger()->printNormal(this, "sent forward audio sample", {"Size"},
      //            {QString::number(audioFrame->data_size)});

      frame = buffer_->readFrame();
    }
  }
}


void AudioCaptureFilter::start()
{
  Logger::getLogger()->printNormal(this, "Resuming audio input.");

  wantedState_ = QAudio::ActiveState;
  if (audioInput_ && (audioInput_->state() == QAudio::SuspendedState
      || audioInput_->state() == QAudio::StoppedState))
  {
    audioInput_->resume();
  }

}


void AudioCaptureFilter::stop()
{
  Logger::getLogger()->printNormal(this, "Suspending input.");

  wantedState_ = QAudio::SuspendedState;
  if (audioInput_ && audioInput_->state() == QAudio::ActiveState)
  {
#ifdef __linux__
    // suspend hangs on linux with pulseaudio for some reason
    audioInput_->stop();
#else
    audioInput_->suspend();
#endif
  }

  // just in case the filter part was running
  Filter::stop();
  Logger::getLogger()->printNormal(this, "Input suspended.");
}


// changing of audio device mid stream.
void AudioCaptureFilter::updateSettings()
{
  Logger::getLogger()->printNormal(this, "Updating audio capture settings");

  if (audioInput_)
  {
    audioInput_->stop();
    audioInput_->disconnect(this);
    delete audioInput_;
    input_ = nullptr;
  }

  init();
}


void AudioCaptureFilter::volumeChanged(int value)
{
  if(audioInput_)
  {
    audioInput_->setVolume(qreal(value) / 100);
  }
}

void AudioCaptureFilter::stateChanged()
{
  Logger::getLogger()->printNormal(this, "Audio Input State changed", {"States:"}, 
                                   {"Current: " + QString::number(audioInput_->state()) + 
                                    ", Wanted: " + QString::number(wantedState_)});

  if (audioInput_ && audioInput_->state() != wantedState_)
  {
    if (wantedState_ == QAudio::SuspendedState)
    {
#ifdef __linux__
      audioInput_->stop();
#else
      audioInput_->suspend();
#endif
    }
    else if (wantedState_ == QAudio::ActiveState)
    {
      if (audioInput_->state() == QAudio::StoppedState)
      {
        createAudioInput();
      }
      else if (audioInput_->state() == QAudio::SuspendedState)
      {
        audioInput_->resume();
      }
      else if (audioInput_->state() == QAudio::IdleState)
      {

      }
    }
  }
}

void AudioCaptureFilter::createReadBuffer(int size)
{
  destroyReadBuffer();

  // Qt recommends reading samples size of period size
  readBuffer_ = new char [size];

  readBufferSize_ = size;
}


void AudioCaptureFilter::destroyReadBuffer()
{
  if (readBuffer_ != nullptr)
  {
    delete [] readBuffer_;
    readBuffer_ = nullptr;
  }

  readBufferSize_ = 0;
}


void AudioCaptureFilter::process()
{}
