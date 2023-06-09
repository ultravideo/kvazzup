#pragma once

#include "mediaid.h"
#include <QWidget>
#include <QtMultimedia/QAudioFormat>
#include <QObject>

#include <vector>
#include <memory>

class VideoInterface;
class StatisticsInterface;

class Filter;
class ScreenShareFilter;
class DisplayFilter;
class AudioCaptureFilter;
class AudioOutputFilter;

class SpeexAEC;
class AudioMixer;

class ResourceAllocator;

typedef std::vector<std::shared_ptr<Filter>> GraphSegment;

class FilterGraph : public QObject
{
  Q_OBJECT
public:
  FilterGraph();

  void init(QList<VideoInterface*> selfViews, StatisticsInterface *stats,
            std::shared_ptr<ResourceAllocator> hwResources);
  void uninit();

  // These functions are used to manipulate filter graphs regarding a peer
  void sendVideoto(uint32_t sessionID, std::shared_ptr<Filter> videoFramedSource,
                   const MediaID &id);

  void receiveVideoFrom(uint32_t sessionID, std::shared_ptr<Filter> videoSink,
                        VideoInterface *view,
                        const MediaID &id);
  void sendAudioTo(uint32_t sessionID, std::shared_ptr<Filter> audioFramedSource,
                   const MediaID &id);
  void receiveAudioFrom(uint32_t sessionID, std::shared_ptr<Filter> audioSink,
                        const MediaID &id);

  // removes participant and all its associated filter from filter graph.
  void removeParticipant(uint32_t sessionID);

  void running(bool state);

public slots:

  void updateVideoSettings();
  void updateAudioSettings();
  void updateAutomaticSettings();

private:

  void selectVideoSource();

  void mic(bool state);
  void camera(bool state);
  void screenShare(bool shareState);

  // Adds fitler to graph and connects it to connectIndex unless this is
  // the first filter in graph. Adds format conversion if needed.
  bool addToGraph(std::shared_ptr<Filter> filter,
                  GraphSegment& graph,
                  size_t connectIndex = 0);

  // connects the two filters and checks for any problems
  bool connectFilters(std::shared_ptr<Filter> previous, std::shared_ptr<Filter> filter);

  // makes sure the participant exists and adds if necessary
  void checkParticipant(uint32_t sessionID);

  // iniates camera and attaches a self view to it.
  void initCameraSelfView();

  // iniates encoder and attaches it
  void initVideoSend();

  // iniates encoder and attaches it
  void initializeAudioInput(bool opus);
  void initializeAudioOutput(bool opus);

  QAudioFormat createAudioFormat(uint8_t channels, uint32_t sampleRate);

  void removeAllParticipants();

  struct Peer
  {
    // keep track of existing connections, so we don't duplicate them
    std::vector<MediaID> sendingStreams;
    std::vector<MediaID> receivingStreams;

    // Arrays of filters which send media, but are not connected to each other.
    std::vector<std::shared_ptr<Filter>> audioSenders; // sends audio
    std::vector<std::shared_ptr<Filter>> videoSenders; // sends video

    // Arrays of filters which receive media.
    // Each graphsegment receives one mediastream.
    std::vector<std::shared_ptr<GraphSegment>> videoReceivers;
    std::vector<std::shared_ptr<GraphSegment>> audioReceivers;
  };

  // destroy all filters associated with this peer.
  void destroyPeer(Peer* peer);

  void destroyFilters(std::vector<std::shared_ptr<Filter>>& filters);

  bool existingConnection(std::vector<MediaID>& connections, MediaID id);

  // --------------- General stuff ----------------
  bool quitting_;

  // key is sessionID
  std::map<uint32_t, Peer*> peers_;
  std::shared_ptr<ResourceAllocator> hwResources_;
  StatisticsInterface* stats_;

  // --------------- Video stuff   --------------------
  GraphSegment cameraGraph_;
  GraphSegment screenShareGraph_;

  std::shared_ptr<DisplayFilter> selfviewFilter_;
  VideoInterface* roiInterface_; // this is the roi surface from settings

  QString videoFormat_;
  bool videoSendIniated_;

  // --------------- Audio stuff   ----------------
  GraphSegment audioInputGraph_;  // mic and stuff after it
  GraphSegment audioOutputGraph_; // stuff before speakers and speakers

  // these are shared between filters
  std::shared_ptr<SpeexAEC> aec_;
  std::shared_ptr<AudioMixer> mixer_;

  bool audioInputInitialized_;
  bool audioOutputInitialized_;

  std::shared_ptr<AudioCaptureFilter> audioCapture_;
  std::shared_ptr<AudioOutputFilter>  audioOutput_;

  // audio configs
  QAudioFormat format_;
};
