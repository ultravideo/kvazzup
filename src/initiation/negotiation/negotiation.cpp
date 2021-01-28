#include "negotiation.h"

#include <QObject>

#include "common.h"
#include "global.h"

const uint16_t MIN_ICE_PORT   = 23000;
const uint16_t MAX_ICE_PORT   = 24000;

Negotiation::Negotiation():
  nCandidates_(),
  ice_(std::make_unique<ICE>())
{}


void Negotiation::init()
{
  QObject::connect(ice_.get(), &ICE::nominationSucceeded,
                   this,       &Negotiation::nominationSucceeded);

  QObject::connect(ice_.get(), &ICE::nominationFailed,
                   this,       &Negotiation::iceNominationFailed);

  nCandidates_.setPortRange(MIN_ICE_PORT, MAX_ICE_PORT);
}


void Negotiation::processOutgoingRequest(SIPRequest& request, QVariant& content, uint32_t sessionID)
{
  if(request.method == SIP_ACK && getState(sessionID)
     == NEG_ANSWER_GENERATED)
  {
    request.message->contentLength = 0;
    printNormal(this, "Adding SDP content to request");

    request.message->contentType = MT_APPLICATION_SDP;

    if (!SDPAnswerToContent(content, sessionID))
    {
      printError(this, "Failed to get SDP answer to request");
      return;
    }
  }
}


void Negotiation::processOutgoingResponse(SIPResponse& response, QVariant& content,
                                          uint32_t sessionID, QString localAddress)
{
  if (response.type == SIP_OK
      && response.message->cSeq.method == SIP_INVITE
      && getState(sessionID) == NEG_NO_STATE)
  {
    printNormal(this, "Adding SDP to an OK response");
    response.message->contentLength = 0;
    response.message->contentType = MT_APPLICATION_SDP;
    if (!SDPOfferToContent(content, localAddress, sessionID))
    {
      return;
    }
  }
  // if they sent an offer in their INVITE
  else if (getState(sessionID) == NEG_ANSWER_GENERATED)
  {
    printNormal(this, "Adding SDP to response since INVITE had an SDP.");

    response.message->contentLength = 0;
    response.message->contentType = MT_APPLICATION_SDP;
    if (!SDPAnswerToContent(content, sessionID))
    {
      printError(this, "Failed to get SDP answer to response");
      return;
    }
  }

}

void Negotiation::processIncomingRequest(SIPRequest& request, QVariant& content,
                                         uint32_t sessionID, QString localAddress)
{
  if((request.method == SIP_INVITE || request.method == SIP_ACK)
     && request.message->contentType == MT_APPLICATION_SDP)
  {
    switch (getState(sessionID))
    {
    case NEG_NO_STATE:
    {
      printDebug(DEBUG_NORMAL, this,
                 "Got first SDP offer.");
      if(!processOfferSDP(sessionID, content, localAddress))
      {
         printDebug(DEBUG_PROGRAM_ERROR, this,
                    "Failure to process SDP offer not implemented.");

         //sendResponse(sessionID, SIP_DECLINE, request.type);
         return;
      }
      break;
    }
    case NEG_OFFER_GENERATED:
    {
      printDebug(DEBUG_NORMAL, this,
                 "Got an SDP answer.");
      processAnswerSDP(sessionID, content);
      break;
    }
    case NEG_ANSWER_GENERATED: // TODO: Not sure if these make any sense
    {
      printDebug(DEBUG_NORMAL, this,
                 "They sent us another SDP offer.");
      processOfferSDP(sessionID, content, localAddress);
      break;
    }
    case NEG_FINISHED:
    {
      printDebug(DEBUG_NORMAL, this,
                 "Got a new SDP offer in response.");
      processOfferSDP(sessionID, content, localAddress);
      break;
    }
    }
  }
}


void Negotiation::processIncomingResponse(SIPResponse& response, QVariant& content,
                                          uint32_t sessionID, QString localAddress)
{
  if(response.message->cSeq.method == SIP_INVITE && response.type == SIP_OK)
  {
    if(response.message->contentType == MT_APPLICATION_SDP)
    {
      switch (getState(sessionID))
      {
      case NEG_NO_STATE:
      {
        printDebug(DEBUG_NORMAL, this,
                   "Got first SDP offer.");
        if(!processOfferSDP(sessionID, content, localAddress))
        {
           printDebug(DEBUG_PROGRAM_ERROR, this,
                      "Failure to process SDP offer not implemented.");

           //sendResponse(sessionID, SIP_DECLINE, request.type);
           return;
        }
        break;
      }
      case NEG_OFFER_GENERATED:
      {
        printDebug(DEBUG_NORMAL, this,
                   "Got an SDP answer.");
        processAnswerSDP(sessionID, content);
        break;
      }
      case NEG_ANSWER_GENERATED: // TODO: Not sure if these make any sense
      {
        printDebug(DEBUG_NORMAL, this,
                   "They sent us another SDP offer.");
        processOfferSDP(sessionID, content, localAddress);
        break;
      }
      case NEG_FINISHED:
      {
        printDebug(DEBUG_NORMAL, this,
                   "Got a new SDP offer in response.");
        processOfferSDP(sessionID, content, localAddress);
        break;
      }
      }
    }
  }
}


bool Negotiation::generateOfferSDP(QString localAddress,
                                        uint32_t sessionID)
{
  Q_ASSERT(sessionID);

  qDebug() << "Getting local SDP suggestion";
  std::shared_ptr<SDPMessageInfo> localSDP = negotiator_.generateLocalSDP(localAddress);
  // TODO: Set also media sdp parameters.
  localSDP->candidates = ice_->generateICECandidates(nCandidates_.localCandidates(STREAM_COMPONENTS, sessionID),
                                                     nCandidates_.globalCandidates(STREAM_COMPONENTS, sessionID),
                                                     nCandidates_.stunCandidates(STREAM_COMPONENTS),
                                                     nCandidates_.stunBindings(STREAM_COMPONENTS, sessionID),
                                                     nCandidates_.turnCandidates(STREAM_COMPONENTS, sessionID));

  if(localSDP != nullptr)
  {
    sdps_[sessionID].localSDP = localSDP;
    sdps_[sessionID].remoteSDP = nullptr;

    negotiationStates_[sessionID] = NEG_OFFER_GENERATED;
  }
  return localSDP != nullptr;
}


bool Negotiation::generateAnswerSDP(SDPMessageInfo &remoteSDPOffer,
                                    QString localAddress,
                                    uint32_t sessionID)
{
  Q_ASSERT(sessionID);

  // check if suitable.
  if(!negotiator_.checkSDPOffer(remoteSDPOffer))
  {
    qDebug() << "Incoming SDP did not have Opus and H265 in their offer.";
    return false;
  }

  // TODO: check that we dont already have an SDP for them in which case we should deallocate those ports.

  // generate our SDP.
  std::shared_ptr<SDPMessageInfo> localSDP = negotiator_.negotiateSDP(remoteSDPOffer, localAddress);
  localSDP->candidates = ice_->generateICECandidates(nCandidates_.localCandidates(STREAM_COMPONENTS, sessionID),
                                                     nCandidates_.globalCandidates(STREAM_COMPONENTS, sessionID),
                                                     nCandidates_.stunCandidates(STREAM_COMPONENTS),
                                                     nCandidates_.stunBindings(STREAM_COMPONENTS, sessionID),
                                                     nCandidates_.turnCandidates(STREAM_COMPONENTS, sessionID));

  if (localSDP == nullptr)
  {
    printDebug(DEBUG_PROGRAM_ERROR, "Negotiation", 
               "Failed to generate our answer to their offer."
               "Suitability should be detected earlier in checkOffer.");
    return false;
  }

  std::shared_ptr<SDPMessageInfo> remoteSDP
      = std::shared_ptr<SDPMessageInfo>(new SDPMessageInfo);
  *remoteSDP = remoteSDPOffer;

  sdps_[sessionID].localSDP = localSDP;
  sdps_[sessionID].remoteSDP = remoteSDP;

  negotiationStates_[sessionID] = NEG_ANSWER_GENERATED;

  // Start candiate nomination. This function won't block,
  // negotiation happens in the background
  ice_->startNomination(localSDP->candidates, remoteSDP->candidates, sessionID, true);

  return true;
}


bool Negotiation::processAnswerSDP(SDPMessageInfo &remoteSDPAnswer, uint32_t sessionID)
{
  printDebug(DEBUG_NORMAL, "Negotiation",  "Starting to process answer SDP.");
  if (!checkSessionValidity(sessionID, false))
  {
    return false;
  }

  if (getState(sessionID) == NEG_NO_STATE)
  {
    printDebug(DEBUG_WARNING, "Negotiation",  "Processing SDP answer without hacing sent an offer!");
    return false;
  }

  // this function blocks until a candidate is nominated or all candidates are considered
  // invalid in which case it returns false to indicate error
  if (negotiator_.checkSDPOffer(remoteSDPAnswer))
  {
    std::shared_ptr<SDPMessageInfo> remoteSDP
        = std::shared_ptr<SDPMessageInfo>(new SDPMessageInfo);
    *remoteSDP = remoteSDPAnswer;
    sdps_[sessionID].remoteSDP = remoteSDP;

    negotiationStates_[sessionID] = NEG_FINISHED;

    // spawn ICE controllee threads and start the candidate
    // exchange and nomination
    //
    // This will start the ICE nomination process. After it has finished,
    // it will send a signal which indicates its state and if successful, the call may start.
    ice_->startNomination(sdps_[sessionID].localSDP->candidates, remoteSDP->candidates, sessionID, false);

    return true;
  }

  return false;
}


std::shared_ptr<SDPMessageInfo> Negotiation::getLocalSDP(uint32_t sessionID) const
{
  if(!checkSessionValidity(sessionID, false))
  {
    return nullptr;
  }
  return sdps_.at(sessionID).localSDP;
}


std::shared_ptr<SDPMessageInfo> Negotiation::getRemoteSDP(uint32_t sessionID) const
{
  if(!checkSessionValidity(sessionID, true))
  {
    return nullptr;
  }

  return sdps_.at(sessionID).remoteSDP;
}


void Negotiation::endSession(uint32_t sessionID)
{
  if(sdps_.find(sessionID) != sdps_.end())
  {
    if (sdps_.at(sessionID).localSDP != nullptr)
    {
      std::shared_ptr<SDPMessageInfo> localSDP = sdps_.at(sessionID).localSDP;
      /*for(auto& mediaStream : localSDP->media)
      {
        // TODO: parameters_.makePortPairAvailable(mediaStream.receivePort);
      }*/
    }
    sdps_.erase(sessionID);
  }

  if (negotiationStates_.find(sessionID) != negotiationStates_.end())
  {
    negotiationStates_.erase(sessionID);
  }

  ice_->cleanupSession(sessionID);
  nCandidates_.cleanupSession(sessionID);
}


void Negotiation::endAllSessions()
{
  QList<uint32_t> sessions;

  for (auto& i : negotiationStates_)
  {
    sessions.push_back(i.first);
  }

  for (auto& i : sessions)
  {
    endSession(i);
  }
}


void Negotiation::nominationSucceeded(quint32 sessionID)
{
  if (!checkSessionValidity(sessionID, true))
  {
    return;
  }

  QList<std::shared_ptr<ICEPair>> streams = ice_->getNominated(sessionID);

  if (streams.size() != 4)
  {
    return;
  }

  printNormal(this, "ICE nomination has succeeded", {"SessionID"}, {QString::number(sessionID)});

  std::shared_ptr<SDPMessageInfo> localSDP = sdps_.at(sessionID).localSDP;
  std::shared_ptr<SDPMessageInfo> remoteSDP = sdps_.at(sessionID).remoteSDP;

  // Video. 0 is RTP, 1 is RTCP
  if (streams.at(0) != nullptr && streams.at(1) != nullptr)
  {
    negotiator_.setMediaPair(localSDP->media[1],  streams.at(0)->local, true);
    negotiator_.setMediaPair(remoteSDP->media[1], streams.at(0)->remote, false);
  }

  // Audio. 2 is RTP, 3 is RTCP
  if (streams.at(2) != nullptr && streams.at(3) != nullptr)
  {
    negotiator_.setMediaPair(localSDP->media[0],  streams.at(2)->local, true);
    negotiator_.setMediaPair(remoteSDP->media[0], streams.at(2)->remote, false);
  }

  emit iceNominationSucceeded(sessionID);
}


NegotiationState Negotiation::getState(uint32_t sessionID)
{
  if (negotiationStates_.find(sessionID) == negotiationStates_.end())
  {
    return NEG_NO_STATE;
  }

  return negotiationStates_[sessionID];
}


bool Negotiation::checkSessionValidity(uint32_t sessionID, bool checkRemote) const
{
  Q_ASSERT(sessionID);

  Q_ASSERT(sdps_.find(sessionID) != sdps_.end());
  Q_ASSERT(sdps_.at(sessionID).localSDP != nullptr);
  Q_ASSERT(sdps_.at(sessionID).remoteSDP != nullptr || !checkRemote);

  if(sessionID == 0 ||
     sdps_.find(sessionID) == sdps_.end() ||
     sdps_.at(sessionID).localSDP == nullptr ||
     (sdps_.at(sessionID).remoteSDP == nullptr && checkRemote))
  {
    printDebug(DEBUG_PROGRAM_ERROR, "GlobalSDPState",
               "Attempting to use GlobalSDPState without setting SessionID correctly",
              {"sessionID"}, {QString::number(sessionID)});
    return false;
  }
  return true;
}

bool Negotiation::SDPOfferToContent(QVariant& content, QString localAddress,
                                    uint32_t sessionID)
{
  std::shared_ptr<SDPMessageInfo> pointer;

  printDebug(DEBUG_NORMAL, this,  "Adding one-to-one SDP.");
  if(!generateOfferSDP(localAddress, sessionID))
  {
    printWarning(this, "Failed to generate local SDP when sending offer.");
    return false;
  }
   pointer = getLocalSDP(sessionID);

  Q_ASSERT(pointer != nullptr);

  SDPMessageInfo sdp = *pointer;
  content.setValue(sdp);
  return true;
}


bool Negotiation::processOfferSDP(uint32_t sessionID, QVariant& content,
                                  QString localAddress)
{
  if(!content.isValid())
  {
    printDebug(DEBUG_PROGRAM_ERROR, this,
                     "The SDP content is not valid at processing. "
                     "Should be detected earlier.");
    return false;
  }

  SDPMessageInfo retrieved = content.value<SDPMessageInfo>();
  if(!generateAnswerSDP(retrieved, localAddress, sessionID))
  {
    printWarning(this, "Remote SDP not suitable or we have no ports to assign");
    endSession(sessionID);
    return false;
  }

  return true;
}


bool Negotiation::SDPAnswerToContent(QVariant &content, uint32_t sessionID)
{
  SDPMessageInfo sdp;
  std::shared_ptr<SDPMessageInfo> pointer = getLocalSDP(sessionID);
  if (pointer == nullptr)
  {
    return false;
  }
  sdp = *pointer;
  content.setValue(sdp);
  return true;
}


bool Negotiation::processAnswerSDP(uint32_t sessionID, QVariant &content)
{
  SDPMessageInfo retrieved = content.value<SDPMessageInfo>();
  if (!content.isValid())
  {
    printDebug(DEBUG_PROGRAM_ERROR, this,
               "Content is not valid when processing SDP. "
               "Should be detected earlier.");
    return false;
  }

  if(!processAnswerSDP(retrieved, sessionID))
  {
    return false;
  }

  return true;
}
