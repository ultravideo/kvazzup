#include "sipregistration.h"


#include "initiation/transaction/sipdialogstate.h"
#include "initiation/transaction/sipclient.h"

#include "common.h"
#include "serverstatusview.h"
#include "global.h"


#include <QDebug>

const int REGISTER_SEND_PERIOD = (REGISTER_INTERVAL - 5)*1000;


SIPRegistration::SIPRegistration():
  retryTimer_(nullptr)
{}


SIPRegistration::~SIPRegistration()
{}


void SIPRegistration::init(ServerStatusView *statusView)
{
  printNormal(this, "Initiating Registration");
  statusView_ = statusView;

  QObject::connect(&retryTimer_, &QTimer::timeout,
                   this, &SIPRegistration::refreshRegistration);

  retryTimer_.setInterval(REGISTER_SEND_PERIOD); // have 5 seconds extra to reach registrar
  retryTimer_.setSingleShot(false);
}


void SIPRegistration::uninit()
{
  if (status_ == REG_ACTIVE)
  {
    sendREGISTERRequest(0, DEREGISTERING);
  }

  printNormal(this, "Finished uniniating registration");

  // we don't wait for the OK reply so we can quit faster.
  return;
}


void SIPRegistration::bindToServer(NameAddr& addressRecord, QString localAddress,
                                    uint16_t port)
{
  printNormal(this, "Binding to server", {"Server"},
              {addressRecord.uri.hostport.host});

  status_ = INACTIVE;
  contactAddress_ = localAddress;
  contactPort_ = port;

  serverAddress_ = addressRecord.uri.hostport.host;
  statusView_->updateServerStatus("Request sent. Waiting response...");

  sendREGISTERRequest(REGISTER_INTERVAL, FIRST_REGISTRATION);
}


void SIPRegistration::processIncomingResponse(SIPResponse& response, QVariant& content)
{
  Q_UNUSED(content);
  // REGISTER response must not create route. In other words ignore all record-routes

  if (response.message->cSeq.method == SIP_REGISTER)
  {
    bool foundRegistration = false;

    if (serverAddress_ == response.message->to.address.uri.hostport.host)
    {
      if (response.type == SIP_OK)
      {
        foundRegistration = true;

        if (status_ != RE_REGISTRATION &&
            response.message->vias.at(0).receivedAddress != "" &&
            response.message->vias.at(0).rportValue != 0 &&
            (contactAddress_ != response.message->vias.at(0).receivedAddress ||
             contactPort_ != response.message->vias.at(0).rportValue))
        {
          printNormal(this, "Detected that we are behind NAT!");

          // we want to remove the previous registration so it doesn't cause problems
          if (status_ == FIRST_REGISTRATION)
          {
            printNormal(this, "Resetting previous registration");
            sendREGISTERRequest(0, DEREGISTERING);
            return;
          }
          else if (status_ == DEREGISTERING)// the actual NAT registration
          {
            printNormal(this, "Sending the final NAT REGISTER");
            contactAddress_ = response.message->contact.first().address.uri.hostport.host;
            contactPort_ = response.message->contact.first().address.uri.hostport.port;
            // makes sure we don't end up in infinite loop if the address doesn't match

            statusView_->updateServerStatus("Behind NAT, updating address...");

             // re-REGISTER with NAT address and port
            sendREGISTERRequest(REGISTER_INTERVAL, RE_REGISTRATION);
            return;
          }
          else
          {
            printError(this, "The Registration response does not match internal state");
          }
        }
        else
        {
          statusView_->updateServerStatus("Registered");
        }

        status_ = REG_ACTIVE;

        if (!retryTimer_.isActive())
        {
          retryTimer_.start(REGISTER_SEND_PERIOD);
        }

        printNormal(this, "Registration was successful.");
      }
      else
      {
        printDebug(DEBUG_ERROR, this, "REGISTER-request failed");
        statusView_->updateServerStatus(response.text);
      }
    }

    if (!foundRegistration)
    {
      qDebug() << "PEER ERROR: Got a resonse to REGISTRATION we didn't send";
    }
  }
  else
  {
    printUnimplemented(this, "Processing of Non-REGISTER requests");
  }
}


void SIPRegistration::refreshRegistration()
{
  // no need to continue refreshing if we have no active registrations
  if (!haveWeRegistered())
  {
    printWarning(this, "Not refreshing our registrations, because we have none!");
    retryTimer_.stop();
    return;
  }

  if (status_ == REG_ACTIVE)
  {
    statusView_->updateServerStatus("Second request sent. Waiting response...");
    sendREGISTERRequest(REGISTER_INTERVAL, REG_ACTIVE);
  }
}


bool SIPRegistration::haveWeRegistered()
{
  return status_ == REG_ACTIVE;
}


void SIPRegistration::sendREGISTERRequest(uint32_t expires, RegistrationStatus newStatus)
{
  SIPRequest request;
  request.method = SIP_REGISTER;
  request.message = std::shared_ptr<SIPMessageHeader> (new SIPMessageHeader);
  request.message->expires = std::shared_ptr<uint32_t> (new uint32_t{expires});

  QVariant content;

  status_ = newStatus;
  emit outgoingRequest(request, content);
}
