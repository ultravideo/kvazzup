#include "filtergraph.h"


#include "camerafilter.h"
#include "displayfilter.h"
#include "kvazaarfilter.h"
#include "rgb32toyuv.h"
#include "openhevcfilter.h"
#include "yuvtorgb32.h"
#include "framedsourcefilter.h"
#include "displayfilter.h"


FilterGraph::FilterGraph(StatisticsInterface* stats):filters_(), stats_(stats),
  streamer_(stats)
{
  Q_ASSERT(stats);
}

void FilterGraph::init(VideoWidget* selfView, QSize resolution)
{
  streamer_.setPorts(15555,18888);
  streamer_.start();

  initSender(selfView, resolution);
}

void FilterGraph::initSender(VideoWidget *selfView, QSize resolution)
{
  Q_ASSERT(stats_);
  // Sending video graph
  filters_.push_back(new CameraFilter(stats_, resolution));

  // connect selfview to camera
  DisplayFilter* selfviewFilter = new DisplayFilter(stats_, selfView);
  selfviewFilter->setProperties(true);
  filters_.push_back(selfviewFilter);
  filters_.at(0)->addOutConnection(filters_.back());
  filters_.back()->start();

  filters_.push_back(new RGB32toYUV(stats_));
  filters_.at(0)->addOutConnection(filters_.back());
  filters_.back()->start();

  KvazaarFilter* kvz = new KvazaarFilter(stats_);
  kvz->init(resolution, 30, 1, 0);
  filters_.push_back(kvz);
  filters_.at(filters_.size() - 2)->addOutConnection(filters_.back());
  filters_.back()->start();

  encoderFilter_ = filters_.size() - 1;


}

ParticipantID FilterGraph::addParticipant(in_addr ip, uint16_t port, VideoWidget* view,
                                          bool wantsAudio, bool sendsAudio,
                                          bool wantsVideo, bool sendsVideo)
{
  Q_ASSERT(stats_);

  if(port != 0)
    streamer_.setPorts(15555, port);

  PeerID peer = streamer_.addPeer(ip, 30, true, true);
  if(wantsVideo)
  {
    Filter *framedSource = NULL;
    framedSource = streamer_.getSourceFilter(peer);

    filters_.push_back(framedSource);
    filters_.at(encoderFilter_)->addOutConnection(filters_.back());
    filters_.back()->start();
  }

  if(sendsVideo && view != NULL)
  {
    // Receiving video graph
    Filter* rtpSink = NULL;
    rtpSink = streamer_.getSinkFilter(peer);

    filters_.push_back(rtpSink);
    filters_.back()->start();

    OpenHEVCFilter* decoder =  new OpenHEVCFilter(stats_);
    decoder->init();
    filters_.push_back(decoder);
    filters_.at(filters_.size() - 2)->addOutConnection(filters_.back());
    filters_.back()->start();

    filters_.push_back(new YUVtoRGB32(stats_));
    filters_.at(filters_.size() - 2)->addOutConnection(filters_.back());
    filters_.back()->start();

    filters_.push_back(new DisplayFilter(stats_, view));
    filters_.at(filters_.size() - 2)->addOutConnection(filters_.back());
    filters_.back()->start();
  }
  else if(view == NULL)
    qWarning() << "Warn: wanted to receive video, but no view available";
}


void FilterGraph::uninit()
{
  deconstruct();
}


void FilterGraph::deconstruct()
{
  for( Filter *f : filters_ )
    delete f;

  filters_.clear();
}

void FilterGraph::restart()
{
  for(Filter* f : filters_)
    f->start();

  streamer_.start();
}

void FilterGraph::stop()
{
  for(Filter* f : filters_)
  {
    f->stop();
    f->emptyBuffer();
  }
  streamer_.stop();
}
