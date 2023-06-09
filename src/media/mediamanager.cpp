#include "mediamanager.h"

#include "media/processing/filtergraph.h"
#include "media/processing/filter.h"
#include "media/delivery/delivery.h"
#include "initiation/negotiation/sdptypes.h"
#include "statisticsinterface.h"
#include "videoviewfactory.h"

#include "resourceallocator.h"

#include "logger.h"
#include "common.h"

#include <QHostAddress>
#include <QtEndian>
#include <QSettings>


MediaManager::MediaManager():
  stats_(nullptr),
  fg_(new FilterGraph()),
  streamer_(nullptr)
{}


MediaManager::~MediaManager()
{
  fg_->running(false);
  fg_->uninit();
}


void MediaManager::init(std::shared_ptr<VideoviewFactory> viewFactory,
                        StatisticsInterface *stats)
{
  Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Initiating");
  stats_ = stats;
  streamer_ = std::unique_ptr<Delivery> (new Delivery());
  viewFactory_ = viewFactory;

  connect(
    streamer_.get(),
    &Delivery::handleZRTPFailure,
    this,
    &MediaManager::handleZRTPFailure);

  connect(
    streamer_.get(),
    &Delivery::handleNoEncryption,
    this,
    &MediaManager::handleNoEncryption);

  std::shared_ptr<ResourceAllocator> hwResources =
      std::shared_ptr<ResourceAllocator>(new ResourceAllocator());

  fg_->init(viewFactory_->getSelfVideos(), stats, hwResources);
  streamer_->init(stats_, hwResources);

  QObject::connect(this, &MediaManager::updateVideoSettings,
                   fg_.get(), &FilterGraph::updateVideoSettings);

  QObject::connect(this, &MediaManager::updateAudioSettings,
                   fg_.get(), &FilterGraph::updateAudioSettings);

  QObject::connect(this, &MediaManager::updateAutomaticSettings,
                   fg_.get(), &FilterGraph::updateAutomaticSettings);
}


void MediaManager::uninit()
{
  Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Closing");

  // first filter graph, then streamer because of the rtpfilters
  fg_->running(false);
  fg_->uninit();

  stats_ = nullptr;
  if (streamer_ != nullptr)
  {
    streamer_->uninit();
    streamer_ = nullptr;
  }
}


void MediaManager::addParticipant(uint32_t sessionID,
                                  std::shared_ptr<SDPMessageInfo> peerInfo,
                                  const std::shared_ptr<SDPMessageInfo> localInfo,
                                  const QList<MediaID> &allIDs,
                                  bool iceController, bool followOurSDP)
{
  // TODO: support stop-time and start-time as recommended by RFC 4566 section 5.9
  if (!sessionChecks(peerInfo, localInfo))
  {
    return;
  }

  if (getMediaNettype(peerInfo, 0) != "IN")
  {
    Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, this,
                                    "What are we using if not the internet!?");
    return;
  }

  if (stats_ != nullptr)
  {
    // TODO: Update SDP in stats, currently it just shows the first
    sdpToStats(sessionID, peerInfo, false);
    sdpToStats(sessionID, localInfo, true);
  }

  Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Start creating media");

  if (participants_.find(sessionID) == participants_.end())
  {
    participants_[sessionID].ice = std::unique_ptr<ICE>(new ICE(sessionID, stats_));

    // connect signals so we get information when ice is ready
    QObject::connect(participants_[sessionID].ice.get(), &ICE::mediaNominationSucceeded,
                     this, &MediaManager::iceSucceeded);

    QObject::connect(participants_[sessionID].ice.get(), &ICE::mediaNominationFailed,
                     this, &MediaManager::iceFailed);
  }

  return modifyParticipant(sessionID, peerInfo, localInfo, allIDs, iceController, followOurSDP);
}


void MediaManager::modifyParticipant(uint32_t sessionID,
                                     std::shared_ptr<SDPMessageInfo> peerInfo,
                                     const std::shared_ptr<SDPMessageInfo> localInfo,
                                     const QList<MediaID> &allIDs,
                                     bool iceController, bool followOurSDP)
{
  // TODO: support stop-time and start-time as recommended by RFC 4566 section 5.9
  if (!sessionChecks(peerInfo, localInfo))
  {
    return;
  }

  Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Start creating media");
  QList<std::shared_ptr<ICEInfo>> localCandidates;
  QList<std::shared_ptr<ICEInfo>> remoteCandidates;

  for (auto& media : localInfo->media)
  {
    localCandidates += media.candidates;
  }

  for (auto& media : peerInfo->media)
  {
    remoteCandidates += media.candidates;
  }

  // perform ICE
  if (!localCandidates.empty() && !remoteCandidates.empty())
  {
    participants_[sessionID].localInfo = localInfo;
    participants_[sessionID].peerInfo = peerInfo;
    participants_[sessionID].allIDs = allIDs;
    participants_[sessionID].followOurSDP = followOurSDP;

     // in mesh conference host, we also have media meant for others, so we don't have and id for those
    unsigned int idIndex = 0;

    // each media has its own separate ICE
    for (unsigned int i = 0; i < localInfo->media.size(); ++i)
    {
      // only test if this is a local candidate
      if (isLocalCandidate(localInfo->media.at(i).candidates.first()))
      {
          participants_[sessionID].ice->startNomination(allIDs.at(idIndex),
                                                        localInfo->media.at(i),
                                                        peerInfo->media.at(i),
                                                        iceController);
        ++idIndex;
      }
    }
  }
  else
  {
    /* Not really used or tested branch, but its not much of a hassle to
     * attempt to support non-ICE implementations */
    Logger::getLogger()->printWarning(this, "Did not find any ICE candidates, not performing ICE");

    unsigned int medias = localInfo->media.size();

    if (peerInfo->media.size() < medias)
    {
      Logger::getLogger()->printProgramError(this, "Different amount of medias in local vs peer");
      medias = peerInfo->media.size();
    }

    unsigned int idIndex = 0;
    for (unsigned int i = 0; i < medias; ++i)
    {
      if (isLocalAddress(localInfo->media.at(i).connection_address))
      {
          createMediaPair(sessionID, allIDs.at(idIndex), localInfo->media.at(i), peerInfo->media.at(i),
                          viewFactory_->getVideo(allIDs.at(idIndex)));
          ++idIndex;
      }
    }
  }
}


void MediaManager::createMediaPair(uint32_t sessionID, const MediaID &id,
                                   const MediaInfo &localMedia,
                                   const MediaInfo &remoteMedia,
                                   VideoInterface *videoView)
{
  if(!streamer_->addSession(sessionID,
                         remoteMedia.connection_addrtype,
                         remoteMedia.connection_address,
                         localMedia.connection_addrtype,
                         localMedia.connection_address))
  {
    Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, this,
               "Error creating RTP peer");
    return;
  }

  createOutgoingMedia(sessionID, localMedia, remoteMedia, id, id.getSend());
  createIncomingMedia(sessionID, localMedia, remoteMedia, id, videoView, id.getReceive());
}


void MediaManager::createOutgoingMedia(uint32_t sessionID,
                                       const MediaInfo& localMedia,
                                       const MediaInfo& remoteMedia,
                                       const MediaID& id,
                                       bool active)
{
  if (localMedia.connection_address == "" || remoteMedia.connection_address == "")
  {
    Logger::getLogger()->printProgramError(this, "Address was empty when creating outgoing media");
    return;
  }

  QString codec = rtpNumberToCodec(remoteMedia);

  std::shared_ptr<Filter> senderFilter = nullptr;

  if(remoteMedia.proto == "RTP/AVP" ||
     remoteMedia.proto == "RTP/AVPF" ||
     remoteMedia.proto == "RTP/SAVP" ||
     remoteMedia.proto == "RTP/SAVPF")
  {
    uint32_t localSSRC = findSSRC(localMedia);
    uint32_t remoteSSRC = findSSRC(remoteMedia);

    senderFilter = streamer_->addSendStream(sessionID,
                                            localMedia.connection_address,
                                            remoteMedia.connection_address,
                                            localMedia.receivePort,
                                            remoteMedia.receivePort,
                                            codec, remoteMedia.rtpNums.at(0),
                                            id, localSSRC, remoteSSRC);
  }
  else
  {
    Logger::getLogger()->printUnimplemented(this, "Remote has unknown proto");
    return;
  }

  // if we want to send
  if(active && remoteMedia.receivePort != 0)
  {
    Q_ASSERT(remoteMedia.receivePort);
    Q_ASSERT(!remoteMedia.rtpNums.empty());

    Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Creating send stream", {"Destination", "Type"},
                                     {remoteMedia.connection_address + ":" + QString::number(remoteMedia.receivePort), remoteMedia.type});

    Q_ASSERT(senderFilter != nullptr);

    if(remoteMedia.type == "audio")
    {
      fg_->sendAudioTo(sessionID, senderFilter, id);
    }
    else if(remoteMedia.type == "video")
    {
      fg_->sendVideoto(sessionID, senderFilter, id);
    }
    else
    {
      Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, this, "Unsupported media type!",
                {"type"}, QStringList() << remoteMedia.type);
    }
  }
  else
  {
    Logger::getLogger()->printDebug(DEBUG_NORMAL, this,
               "Not sending media according to attribute", {"Type"}, {localMedia.type});

    // TODO: Spec says we should still send RTCP if the port is not 0
  }
}


void MediaManager::createIncomingMedia(uint32_t sessionID,
                                       const MediaInfo &localMedia,
                                       const MediaInfo &remoteMedia,
                                       const MediaID& id,
                                       VideoInterface* videoView, bool active)
{
  if (localMedia.connection_address == "" || remoteMedia.connection_address == "")
  {
    Logger::getLogger()->printProgramError(this, "Address was empty when creating incoming media");
    return;
  }

  QString codec = rtpNumberToCodec(localMedia);
  std::shared_ptr<Filter> receiverFilter = nullptr;

  uint32_t localSSRC = findSSRC(localMedia);
  uint32_t remoteSSRC = findSSRC(remoteMedia);

  if(localMedia.proto == "RTP/AVP" ||
     localMedia.proto == "RTP/AVPF" ||
     localMedia.proto == "RTP/SAVP" ||
     localMedia.proto == "RTP/SAVPF")
  {
    receiverFilter = streamer_->addReceiveStream(sessionID,
                                                 localMedia.connection_address,
                                                 remoteMedia.connection_address,
                                                 localMedia.receivePort,
                                                 remoteMedia.receivePort,
                                                 codec, localMedia.rtpNums.at(0), id, localSSRC, remoteSSRC);
  }
  else
  {
    Logger::getLogger()->printUnimplemented(this, "Our media has unkown proto");
    return;
  }

  if(active)
  {
    Q_ASSERT(localMedia.receivePort);
    Q_ASSERT(!localMedia.rtpNums.empty());

    Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "Creating receive stream", {"Interface", "codec"},
                                     {localMedia.connection_address + ":" + QString::number(localMedia.receivePort), codec});


    Q_ASSERT(receiverFilter != nullptr);
    if(localMedia.type == "audio")
    {
      fg_->receiveAudioFrom(sessionID, receiverFilter, id);
    }
    else if(localMedia.type == "video")
    {
      Q_ASSERT(videoView);
      if (videoView != nullptr)
      {
        fg_->receiveVideoFrom(sessionID, receiverFilter, videoView, id);
      }
      else
      {
        Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, this, "Failed to get view from viewFactory");
      }
    }
    else
    {
      Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, this, "Unsupported incoming media type!",
                {"type"}, QStringList() << localMedia.type);
    }
  }
  else
  {
    Logger::getLogger()->printDebug(DEBUG_NORMAL, this,
                                    "Not receiving media according to attribute", {"Type"}, {localMedia.type});
  }
}


void MediaManager::removeParticipant(uint32_t sessionID)
{
  if (participants_.find(sessionID) != participants_.end())
  {
    participants_[sessionID].ice->uninit();
    participants_.erase(sessionID);
  }

  fg_->removeParticipant(sessionID);
  streamer_->removePeer(sessionID);

  Logger::getLogger()->printDebug(DEBUG_NORMAL, "Media Manager", "Session media removed",
            {"SessionID"}, {QString::number(sessionID)});
}


void MediaManager::iceSucceeded(const MediaID& id, uint32_t sessionID,
                                MediaInfo local, MediaInfo remote)
{
  if (participants_.find(sessionID) == participants_.end())
  {
    Logger::getLogger()->printProgramError(this, "Could not find participant when ice finished");
    return;
  }

  Logger::getLogger()->printNormal(this, "ICE nomination has succeeded", {"SessionID"},
                                   {QString::number(sessionID)});

  VideoInterface* view = nullptr;

  if (local.type == "video")
  {
    for (auto& media : participants_[sessionID].allIDs)
    {
      if (media == id)
      {
        view = viewFactory_->getVideo(media);
        if (view == nullptr)
        {
            Logger::getLogger()->printProgramError(this, "Media view was not set correctly");
            return;
        }
        break;
      }
    }

    if (view == nullptr)
    {
      Logger::getLogger()->printProgramError(this, "Could not find a view for media");
      return;
    }
  }

  createMediaPair(sessionID, id, local, remote, view);
}


void MediaManager::iceFailed(const MediaID &id, uint32_t sessionID)
{
  Logger::getLogger()->printError(this, "ICE failed, removing participant");

  // the participant is removed later by receiver of this signal
  emit iceMediaFailed(sessionID);
}


QString MediaManager::rtpNumberToCodec(const MediaInfo& info)
{
  // If we are not using raw.
  // This is the place where all other preset audio video codec numbers should be set
  // but its unlikely that we will support any besides raw pcmu.
  if(info.rtpNums.at(0) != 0)
  {
    Q_ASSERT(!info.rtpMaps.empty());
    if(!info.rtpMaps.empty())
    {
      return info.rtpMaps.at(0).codec;
    }
  }
  return "PCMU";
}


void MediaManager::sdpToStats(uint32_t sessionID, std::shared_ptr<SDPMessageInfo> sdp, bool local)
{
  if (stats_)
  {
    if (local)
    {
      stats_->outgoingMedia(sessionID, sdp->originator_username);
    }
    else
    {
      stats_->incomingMedia(sessionID, sdp->originator_username);
    }
  }
}


QString MediaManager::getMediaNettype(std::shared_ptr<SDPMessageInfo> sdp, int mediaIndex)
{
  if (sdp->media.size() >= mediaIndex && sdp->media.at(mediaIndex).connection_nettype != "")
  {
    return sdp->media.at(mediaIndex).connection_nettype;
  }
  return sdp->connection_nettype;
}


QString MediaManager::getMediaAddrtype(std::shared_ptr<SDPMessageInfo> sdp, int mediaIndex)
{
  if (sdp->media.size() >= mediaIndex && sdp->media.at(mediaIndex).connection_addrtype != "")
  {
    return sdp->media.at(mediaIndex).connection_addrtype;
  }
  return sdp->connection_addrtype;
}


QString MediaManager::getMediaAddress(std::shared_ptr<SDPMessageInfo> sdp, int mediaIndex)
{
  if (sdp->media.size() >= mediaIndex && sdp->media.at(mediaIndex).connection_address != "")
  {
    return sdp->media.at(mediaIndex).connection_address;
  }
  return sdp->connection_address;
}



bool MediaManager::sessionChecks(std::shared_ptr<SDPMessageInfo> peerInfo,
                   const std::shared_ptr<SDPMessageInfo> localInfo) const
{

  Q_ASSERT(peerInfo->media.size() == localInfo->media.size());
  if (peerInfo->media.size() != localInfo->media.size() || peerInfo->media.empty())
  {
    Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, "Media manager",
               "addParticipant, invalid SDPs",
                {"LocalInfo", "PeerInfo"},
                {QString::number(localInfo->media.size()),
                 QString::number(peerInfo->media.size())});
    return false;
  }

  if(peerInfo->timeDescriptions.at(0).startTime != 0 ||
     localInfo->timeDescriptions.at(0).startTime != 0)
  {
    Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, this,
                                    "Nonzero start-time not supported!");
    return false;
  }

  return true;
}
