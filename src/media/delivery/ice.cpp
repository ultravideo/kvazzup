#include "ice.h"

#include "icesessiontester.h"
#include "logger.h"
#include "common.h"
#include "statisticsinterface.h"

#include <QSettings>

#include <memory>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <math.h>       /* pow */

ICE::ICE(uint32_t sessionID, StatisticsInterface *stats):
  sessionID_(sessionID),
  mediaNominations_(),
  stats_(stats)
{
  qRegisterMetaType<uint32_t>("uint32_t");
  qRegisterMetaType<MediaInfo>("MediaInfo");
}


ICE::~ICE()
{
  uninit();
}


void ICE::startNomination(const MediaID &id, const MediaInfo &local, const MediaInfo &remote, bool controller)
{
  std::vector<std::shared_ptr<ICEPair>> newCandidates = makeCandidatePairs(local.candidates,
                                                                           remote.candidates, controller);
  int matchIndex = 0;
  if (matchNominationList(ICE_FINISHED, matchIndex, mediaNominations_, newCandidates))
  {
    Logger::getLogger()->printNormal(this, "Found existing ICE results, using those");
    printSuccessICEPairs(mediaNominations_[matchIndex].succeededPairs);

    updateMedia(mediaNominations_[matchIndex].localMedia, local);
    updateMedia(mediaNominations_[matchIndex].remoteMedia, remote);
    emit mediaNominationSucceeded(id, sessionID_,
                                  mediaNominations_[matchIndex].localMedia,
                                  mediaNominations_[matchIndex].remoteMedia);
  }
  else if (matchNominationList(ICE_RUNNING, matchIndex, mediaNominations_, newCandidates))
  {
    Logger::getLogger()->printImportant(this, "Already running ICE with these candidates, not doing anything");
  }
  else if (matchNominationList(ICE_FAILED, matchIndex, mediaNominations_, newCandidates))
  {
    Logger::getLogger()->printImportant(this, "These ICE candidates have failed before, no sense in running them again");
  }
  else
  {
    int components = 1;

    if (local.proto == "RTP/AVP" ||
        local.proto == "RTP/AVPF" ||
        local.proto == "RTP/SAVP" ||
        local.proto == "RTP/SAVPF")
    {
      components = 2; // RTP + RTCP
    }

    mediaNominations_.push_back({ICE_RUNNING,
                                 local,
                                 remote,
                                 false,
                                 id,
                                 newCandidates,
                                 {},
                                 std::unique_ptr<IceSessionTester> (new IceSessionTester(controller)),
                                 components});



    // perform connection testing and use those instead
    QObject::connect(mediaNominations_.back().iceTester.get(),
                     &IceSessionTester::iceSuccess,
                     this,
                     &ICE::handeICESuccess,
                     Qt::DirectConnection);
    QObject::connect(mediaNominations_.back().iceTester.get(),
                     &IceSessionTester::iceFailure,
                     this,
                     &ICE::handleICEFailure,
                     Qt::DirectConnection);

    QString role = "Controllee";
    if (controller)
    {
      role = "Controller";
    }

    Logger::getLogger()->printDebug(DEBUG_IMPORTANT, this,
                                    "No previous matching ICE results, performing nomination",
                                    {"Role", "Pairs", "Existing media nominations"}, {role,
                                    QString::number(mediaNominations_.back().candidatePairs.size()),
                                    QString::number(mediaNominations_.size())});

    if (mediaNominations_.back().candidatePairs.empty())
    {
      Logger::getLogger()->printProgramError(this, "No candidate pairs to start negotiation with");
      return;
    }

    /* Starts a SessionTester which is responsible for handling connectivity checks and nomination.
     * When testing is finished it is connected tonominationSucceeded/nominationFailed */
    mediaNominations_.back().iceTester.get()->init(mediaNominations_.back().candidatePairs, components);
    mediaNominations_.back().iceTester.get()->start();
  }
}


bool ICE:: matchNominationList(ICEState state, int& index, const std::vector<MediaNomination> &list,
                               const std::vector<std::shared_ptr<ICEPair> > pairs)
{
  for (int i = 0; i < list.size(); ++i)
  {
    if (list[i].state == state &&
        sameCandidates(pairs, list[i].candidatePairs))
    {
      index = i;
      return true;
    }
  }

  index = -1;
  return false;
}


void ICE::handeICESuccess(std::vector<std::shared_ptr<ICEPair>> &streams)
{
  // find the media these streams belong to
  for (auto& media : mediaNominations_)
  {
    // change state of media nomination and emit signal for ICE completion
    if (containCandidates(streams, media.candidatePairs))
    {
      Logger::getLogger()->printNormal(this, "Media ICE succeeded", "Components",
                                       QString::number(streams.size()));
      media.state = ICE_FINISHED;
      media.succeededPairs = streams;
      media.iceTester->quit();

      printSuccessICEPairs(streams);

      // TODO: Improve this (probably need to add RTCP to SDP message)

      // 0 is RTP, 1 is RTCP
      if (streams.at(0) != nullptr && streams.at(1) != nullptr)
      {
        setMediaPair(media.localMedia,  streams.at(0)->local, true);
        setMediaPair(media.remoteMedia, streams.at(0)->remote, false);
      }

      if (!media.addedToStats)
      {
        media.addedToStats = true;
        for (auto& stream : streams)
        {
          stats_->selectedICEPair(sessionID_, stream);
        }
      }

      emit mediaNominationSucceeded(media.id, sessionID_, media.localMedia, media.remoteMedia);

      return;
    }
  }

  Logger::getLogger()->printProgramError(this, "Did not find the media the successful ICE belongs to");
}


void ICE::handleICEFailure(std::vector<std::shared_ptr<ICEPair> > &candidates)
{
  Logger::getLogger()->printDebug(DEBUG_ERROR, "ICE",
                                  "Failed to nominate RTP/RTCP candidates!");

  for (auto& media : mediaNominations_)
  {
    // change state of media nomination and emit signal for ICE completion
    if (sameCandidates(candidates, media.candidatePairs))
    {
      media.state = ICE_FAILED;
      media.iceTester->quit();

      emit mediaNominationFailed(media.id, sessionID_);
    }
  }

  Logger::getLogger()->printProgramError(this, "Did not find the media ICE failure belongs to");
}


void ICE::printSuccessICEPairs(std::vector<std::shared_ptr<ICEPair> > &streams) const
{
  QStringList names;
  QStringList values;
  for(auto& component : streams)
  {
    names.append("Component " + QString::number(component->local->component));
    values.append(component->local->address + ":" + QString::number(component->local->port)
                  + " <-> " +
                  component->remote->address + ":" + QString::number(component->remote->port));
  }

  Logger::getLogger()->printDebug(DEBUG_IMPORTANT, this, "Nominated media ICE candidates", names, values);
}


std::vector<std::shared_ptr<ICEPair> > ICE::makeCandidatePairs(
    const QList<std::shared_ptr<ICEInfo>>& local,
    const QList<std::shared_ptr<ICEInfo>>& remote,
    bool controller
)
{
  std::vector<std::shared_ptr<ICEPair>> pairs;

  // TODO: Check if local are actually local interfaces

  // match all host candidates with remote (remote does the same)
  for (int i = 0; i < local.size(); ++i)
  {
    if (isLocalCandidate(local[i]))
    {
      for (int k = 0; k < remote.size(); ++k)
      {
        // component has to match
        if (local[i]->component == remote[k]->component)
        {
          std::shared_ptr<ICEPair> pair = std::make_shared<ICEPair>();

          // we copy local because we modify it later with stun bindings and
          // we don't want to modify our sent candidates
          pair->local = std::shared_ptr<ICEInfo> (new ICEInfo);
          *(pair->local)    = *local[i];

          pair->remote   = remote[k];

          if (controller)
          {
            pair->priority = pairPriority(local[i]->priority, remote[k]->priority);
          }
          else
          {
            pair->priority = pairPriority(remote[k]->priority, local[i]->priority);
          }

          pair->state    = PAIR_FROZEN;

          pairs.push_back(pair);
        }
      }
    }
    else
    {
      Logger::getLogger()->printNormal(this, "Found ICE candidates that is not found on local machine,"
                                             " ignoring");
    }
  }

  Logger::getLogger()->printNormal(this, "Created " + QString::number(pairs.size()) +
                                         " candidate pairs");
  return pairs;
}


void ICE::uninit()
{
  for (auto& media : mediaNominations_)
  {
    if (media.state == ICE_RUNNING)
    {
      media.iceTester->exit(0);
      uint8_t waits = 0;
      while (media.iceTester->isRunning() && waits <= 50)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ++waits;
      }
    }
  }

  mediaNominations_.clear();
}


uint64_t ICE::pairPriority(int controllerCandidatePriority, int controlleeCandidatePriority) const
{
  // see RFC 8445 section 6.1.2.3
  return (uint64_t)((uint64_t)pow(2, 32) * qMin(controllerCandidatePriority, controlleeCandidatePriority)) +
         (uint64_t)((uint64_t)2 * qMax(controllerCandidatePriority, controlleeCandidatePriority)) +
         (uint64_t)(controllerCandidatePriority > controlleeCandidatePriority ? 1 : 0);
}


void ICE::setMediaPair(MediaInfo& media, std::shared_ptr<ICEInfo> mediaInfo, bool local)
{
  if (mediaInfo == nullptr)
  {
    Logger::getLogger()->printDebug(DEBUG_PROGRAM_ERROR, "SDPNegotiationHelper",
                                    "Null mediainfo in setMediaPair");
    return;
  }

  // for local address, we bind to our rel-address if using non-host connection type
  if (local &&
      mediaInfo->type != "host" &&
      mediaInfo->rel_address != "" && mediaInfo->rel_port != 0)
  {
    setSDPAddress(mediaInfo->rel_address, media.connection_address,
                  media.connection_nettype,
                  media.connection_addrtype);
    media.receivePort        = mediaInfo->rel_port;
  }
  else
  {
    setSDPAddress(mediaInfo->address, media.connection_address,
                  media.connection_nettype,
                  media.connection_addrtype);
    media.receivePort        = mediaInfo->port;
  }
}


void ICE::updateMedia(MediaInfo& oldMedia, const MediaInfo &newMedia)
{
  // basically update everything expect ICE connection
  oldMedia.type = newMedia.type;
  oldMedia.proto = newMedia.proto;

  oldMedia.rtpNums = newMedia.rtpNums;
  oldMedia.title = newMedia.title;
  oldMedia.bitrate = newMedia.bitrate;
  oldMedia.encryptionKey = newMedia.encryptionKey;

  oldMedia.rtpMaps = newMedia.rtpMaps;
  oldMedia.flagAttributes = newMedia.flagAttributes;
  oldMedia.valueAttributes = newMedia.valueAttributes;
  oldMedia.candidates = newMedia.candidates;
}
