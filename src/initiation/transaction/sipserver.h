#pragma once

#include "initiation/sipmessageprocessor.h"
#include "initiation/siptypes.h"

/* This class implements the behavior defined in RFC3261 for component
 * User Agent Server (UAS). See section 8.2 for details.
 *
 * Server handles processing received requests and sending correct responses.
 */


class SIPTransactionUser;


class SIPServer : public SIPMessageProcessor
{
   Q_OBJECT
public:
  SIPServer();

  bool doesCANCELMatchRequest(SIPRequest& request) const;

public slots:

  virtual void processOutgoingResponse(SIPResponse& response, QVariant& content);

    // processes incoming request. Part of server transaction
  virtual void processIncomingRequest(SIPRequest& request, QVariant& content);


private:

    bool isCANCELYours(SIPRequest &cancel);

  // Copies the fields of to a response which are direct copies of the request.
  // includes at least via, to, from, CallID and cseq
  void copyResponseDetails(std::shared_ptr<SIPMessageHeader> &inMessage,
                          std::shared_ptr<SIPMessageHeader> &copy);

  bool equalURIs(SIP_URI& first, SIP_URI& second);

  bool equalToFrom(ToFrom& first, ToFrom& second);


  // used for copying data to response
  std::shared_ptr<SIPRequest> receivedRequest_;
};
