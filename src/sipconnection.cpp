#include "sipconnection.h"

#include "sipconversions.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QList>
#include <QHostInfo>

#include <iostream>
#include <sstream>
#include <string>

#include <functional>

SIPConnection::SIPConnection(quint32 sessionID):
  partialMessage_(""),
  connection_(sessionID, false),
  sessionID_(sessionID)
{}

SIPConnection::~SIPConnection()
{}

void SIPConnection::initConnection(ConnectionType type, QHostAddress target)
{
  qWarning() << "WARNING: SIPConnection not implemented yet.";


}


void SIPConnection::sendRequest(RequestType request,
                 std::shared_ptr<SIPRoutingInfo> routing,
                 std::shared_ptr<SIPSessionInfo> session,
                 std::shared_ptr<SIPMessageInfo> message)
{
  qWarning() << "WARNING: SIPConnection not implemented yet.";
}


void SIPConnection::sendResponse(ResponseType response,
                  std::shared_ptr<SIPRoutingInfo> routing,
                  std::shared_ptr<SIPSessionInfo> session,
                  std::shared_ptr<SIPMessageInfo> message)
{
  qWarning() << "WARNING: SIPConnection not implemented yet.";
}


void SIPConnection::networkPackage(QString message)
{
  qDebug() << "Received a network package for SIP Connection";
  // parse to header and body
  QString header = "";
  QString body = "";
  parsePackage(message, header, body);

  std::shared_ptr<QList<SIPField>> fields;
  std::shared_ptr<QStringList> firstLine;

  if(header != "")
  {
    if(!parseSIPHeader(header))
    {
      qDebug() << "The received message was not correct. ";
      emit parsingError(SIP_BAD_REQUEST, sessionID_); // TODO support other possible error types
      return;
    }
    //fields = SIPConnection::networkToFields(header, firstLine);
  }
  else
  {
    qDebug() << "The whole message was not received";
  }
}


void SIPConnection::parsePackage(QString package, QString& header, QString& body)
{
  qDebug() << "Parsing package to header and body.";

  if(partialMessage_.length() > 0)
  {
    package = partialMessage_ + package;
    partialMessage_ = "";
  }

  int headerEndIndex = package.indexOf("\r\n\r\n", 0, Qt::CaseInsensitive) + 4;
  int contentLengthIndex = package.indexOf("content-length", 0, Qt::CaseInsensitive);

  qDebug() << "header end at:" << headerEndIndex
           << "and content-length at:" << contentLengthIndex;

  if(contentLengthIndex != -1 && headerEndIndex != -1)
  {
    int contentLengthLineEndIndex = package.indexOf("\r\n", contentLengthIndex, Qt::CaseInsensitive);

    QString value = package.mid(contentLengthIndex + 16, contentLengthLineEndIndex - (contentLengthIndex + 16));

    int valueInt= value.toInt();

    qDebug() << "Content-length:" <<  valueInt;

    if(package.length() < headerEndIndex + valueInt)
    {
      partialMessage_ = package;
    }
    else
    {
      partialMessage_ = package.right(package.length() - (headerEndIndex + valueInt));
      header = package.left(headerEndIndex);
      body = package.mid(headerEndIndex, valueInt);

      qDebug() << "Whole message received.";
      qDebug() << "Header:" << header;
      qDebug() << "Content:" << body;
      qDebug() << "Left overs:" << partialMessage_;
    }
  }
  else
  {
    qDebug() << "Message was not received fully";
    partialMessage_.append(package);
  }
}

bool SIPConnection::parseSIPHeader(QString header)
{
  // Divide into lines
  QStringList lines = header.split("\r\n", QString::SkipEmptyParts);
  qDebug() << "Parsing SIP header with" << lines.size() << "lines";
  if(lines.size() == 0)
  {
    qDebug() << "No first line present in SIP header!";
    return false;
  }

  QList<SIPField> fields;
  for(unsigned int i = 1; i < lines.size(); ++i)
  {
    QStringList parameters = lines.at(i).split(";", QString::SkipEmptyParts);

    QRegularExpression re_field("(\\S*): (.+)");
    QRegularExpressionMatch field_match = re_field.match(parameters.at(0));

    if(field_match.hasMatch() && field_match.lastCapturedIndex() == 2)
    {
      SIPField field = {field_match.captured(1),field_match.captured(2),NULL};
      qDebug() << "Parsing field: " << field.name;
      if(parameters.size() > 1)
      {
        for(unsigned int j = 1; j < parameters.size(); ++j)
        {
          SIPParameter parameter;
          if(parseParameter(parameters[j], parameter))
          {
            if(field.parameters == NULL)
            {
              field.parameters = std::shared_ptr<QList<SIPParameter>> (new QList<SIPParameter>);
            }
            field.parameters->append(parameter);
          }
          else
          {
            qDebug() << "Failed to parse SIP parameter:" << parameters[j];
          }
        }
      }
      fields.append(field);
    }
    else
    {
      qDebug() << "Failed to parse line:" << lines.at(i) << "Matches:" << field_match.lastCapturedIndex();
    }
  }

  // check that all required header lines are present
  if(!isLinePresent("To", fields)
     || !isLinePresent("From", fields)
     || !isLinePresent("CSeq", fields)
     || (!isLinePresent("Call-ID", fields) && !isLinePresent("i", fields))
     | !isLinePresent("Via", fields))
  {
    qDebug() << "All mandatory header lines not present!";
    return false;
  }

  std::shared_ptr<SIPRoutingInfo> routing = std::shared_ptr<SIPRoutingInfo>(new SIPRoutingInfo);
  std::shared_ptr<SIPSessionInfo> session = std::shared_ptr<SIPSessionInfo>(new SIPSessionInfo);



  QRegularExpression re_firstLine("(^(\\w+)|(SIP\/2\.0)) (\\S+) (.*)");
  QRegularExpressionMatch firstline_match = re_firstLine.match(lines[0]);

  if(firstline_match.hasMatch() && firstline_match.lastCapturedIndex() >= 5)
  {
    if(firstline_match.captured(5) == "SIP/2.0")
    {
      qDebug() << "Request detected:" << firstline_match.captured(1);
      std::shared_ptr<SIP_Request> request = std::shared_ptr<SIP_Request>(new SIP_Request);

      RequestType requestType = stringToRequest(firstline_match.captured(1));
      if(requestType == SIP_UNKNOWN_REQUEST)
      {
        qDebug() << "Could not recognize request type!";
        return false;
      }

      if(!isLinePresent("Max-Forwards", fields))
      {
        qDebug() << "Mandatory Max-Forwards not present in Request header";
        return false;
      }

      if(requestType == INVITE && !isLinePresent("Contact", fields))
      {
        qDebug() << "Contact header missing from INVITE request";
        return false;
      }
    }
    else if(firstline_match.captured(1) == "SIP/2.0")
    {
      qDebug() << "Response detected:" << firstline_match.captured(1);

    }
    else
    {
      qDebug() << "Could not identify request or response from:" << lines[0]
               << "with first match as:" << firstline_match.captured(1);
    }
  }
  else
  {
    qDebug() << "Failed to parse first line of SIP message:" << lines[0]
             << "Matches detected:" << firstline_match.lastCapturedIndex();
    return false;
  }

  return true;
}

bool SIPConnection::parseParameter(QString text, SIPParameter& parameter)
{
  QRegularExpression re_parameter("([^=]+)=([^;]+)");
  QRegularExpressionMatch parameter_match = re_parameter.match(text);
  if(parameter_match.hasMatch() && parameter_match.lastCapturedIndex() == 2)
  {
    parameter.name = parameter_match.captured(1);
    parameter.value = parameter_match.captured(2);
    return true;
  }

  return false;
}


bool SIPConnection::isLinePresent(QString name, QList<SIPField>& fields)
{
  for(SIPField field : fields)
  {
    if(field.name == name)
    {
      return true;
    }
  }
  qDebug() << "Did not find header:" << name;
  return false;
}


void SIPConnection::parseSIPaddress(QString address, QString& user, QString& location)
{
  QStringList splitAddress = address.split("@");

  if(splitAddress.size() != 2)
  {
    user = "";
    location = "";
    return;
  }

  user = splitAddress.at(0).right(splitAddress.at(0).length() - 5);
  location = splitAddress.at(1).left(splitAddress.at(1).length() - 1);
}


QList<QHostAddress> SIPConnection::parseIPAddress(QString address)
{
  QList<QHostAddress> ipAddresses;

  QRegularExpression re("\\b((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(\\.|$)){4}\\b");
  if(re.match(address).hasMatch())
  {
    qDebug() << "Found IPv4 address:" << address;
    ipAddresses.append(QHostAddress(address));
  }
  else
  {
    qDebug() << "Did not find IPv4 address:" << address;
    QHostInfo hostInfo;
    hostInfo.setHostName(address);

    ipAddresses.append(hostInfo.addresses());
  }
  return ipAddresses;
}

