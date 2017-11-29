#pragma once

#include "globalsdpstate.h"
#include "sipstringcomposer.h"
#include "connection.h"
#include "connectionserver.h"

#include "common.h"

struct Contact
{
  QString username;
  QString realName;
  QString remoteAddress;
};

class SIPRouting;
class SIPSession;

class SIPManager : public QObject
{
  Q_OBJECT
public:
  SIPManager();

  void init();
  void uninit();

  QList<QString> startCall(QList<Contact> addresses);
  void acceptCall(QString callID);
  void rejectCall(QString callID);
  void endCall(QString callID);

  void endAllCalls();

signals:

  // caller wants to establish a call.
  // Ask use if it is and call accept or reject call
  void incomingINVITE(QString CallID, QString caller);

  // we are calling ourselves.
  // TODO: Current implementation ceases the negotiation and just starts the call.
  void callingOurselves(QString callID, std::shared_ptr<SDPMessageInfo> info);

  // their call which we have accepted has finished negotiating. Call can now start.
  void callNegotiated(QString callID, std::shared_ptr<SDPMessageInfo> peerInfo,
                      std::shared_ptr<SDPMessageInfo> localInfo);

  // Local call is waiting for user input at remote end
  void ringing(QString callID);

  // Call iniated locally has been accepted by peer. Call can now start.
  void ourCallAccepted(QString callID, std::shared_ptr<SDPMessageInfo> peerInfo,
                       std::shared_ptr<SDPMessageInfo> localInfo);

  // Remote rejected local INVITE
  void ourCallRejected(QString CallID);

  // Received call ending signal (BYE)
  void callEnded(QString callID, QString ip);

private slots:
  // connection has been established. This enables for us to get the needed info
  // to form a SIP message
  void connectionEstablished(quint32 sessionID);
  void receiveTCPConnection(Connection* con);
  void processSIPMessage(QString header, QString content, quint32 sessionID);

  void sendRequest(uint32_t dialogID_, RequestType request, const SIPSessionInfo& session);

private:

  struct SIPDialogData
  {
    QString callID;
    Connection* con;
    SIPSession* session;
    SIPRouting* routing;
    // has local invite sdp or o response sdp
    std::shared_ptr<SDPMessageInfo> localSdp_;
    // empty until final ok 200
    std::shared_ptr<SDPMessageInfo> remoteSdp_;
    bool hostedSession;
  };

  SIPSession* createSIPSession();


  void destroySession(SIPDialogData *dialog);

  // tmp function to convert new structs to old
  void toSIPMessageInfo(SIPRoutingInfo info);

  QMutex dialogMutex_;

  QList<SIPDialogData*> dialogs_;

  GlobalSDPState sdp_;
  SIPStringComposer messageComposer_;

  ConnectionServer server_;
  uint16_t sipPort_;

  QString localName_;
  QString localUsername_;
};
