#pragma once
#include "filter.h"

#include <QByteArray>
#include <QAudioFormat>
#include <QAudioDevice>

//TODO: this class would not have to be a filter, just needs to send data to one

class QAudioSource;
class QIODevice;
class AudioFrameBuffer;

class AudioCaptureFilter : public Filter
{
  Q_OBJECT
public:
  AudioCaptureFilter(QString id, QAudioFormat format,
                     StatisticsInterface* stats,
                     std::shared_ptr<ResourceAllocator> hwResources);
  virtual ~AudioCaptureFilter();

  virtual bool init(); // setups audio device and parameters.
  virtual void updateSettings(); // changes the selected audio device
  virtual void start(); // resumes audio input
  virtual void stop(); // suspends audio input

public slots:

  void mute();

protected:

  // this does nothing. ReadMore does the sending of
  void process();

private slots:

  // reads audio sample data
  void readMore();

  void volumeChanged(int value);

  // handles a second state change if we suddenly changed our mind
  void stateChanged();

private:

  void createAudioInput();

  void createReadBuffer(int size);
  void destroyReadBuffer();

  QAudioFormat format_;
  QAudioSource *audioInput_;
  QIODevice *input_;
  QAudioDevice device_;

  // used in reading audio frames from mic
  char* readBuffer_;
  int readBufferSize_;

  QAudio::State wantedState_;

  std::unique_ptr<AudioFrameBuffer> buffer_;

  uint8_t muteSamples_;
  uint8_t mutingPeriod_;
};
