#include "siptransport.h"

#include "sipmessagesanity.h"
#include "sipconversions.h"
#include "sipfieldparsing.h"
#include "sipfieldcomposing.h"
#include "initiation/negotiation/sipcontent.h"
#include "statisticsinterface.h"
#include "common.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QList>
#include <QHostInfo>

#include <iostream>
#include <sstream>
#include <string>

#include <functional>

const uint16_t SIP_PORT = 5060;


// TODO: separate this into common, request and response field parsing.
// This is so we can ignore nonrelevant fields (7.3.2)

// one letter headers are compact forms as defined by RFC 3261
const std::map<QString, std::function<bool(SIPField& field,
                                           std::shared_ptr<SIPMessageHeader>)>> parsing =
{
    {"Accept",              parseUnimplemented},      // TODO
    {"Accept-Encoding",     parseUnimplemented},      // TODO
    {"Accept-Language",     parseUnimplemented},      // TODO
    {"Alert-Info",          parseUnimplemented},      // TODO
    {"Allow",               parseUnimplemented},      // TODO
    {"Authentication-Info", parseUnimplemented},      // TODO
    {"Authorization",       parseUnimplemented},      // TODO
    {"Call-ID",             parseCallIDField},
    {"i",                   parseCallIDField},        // compact form of Call-ID
    {"Call-Info",           parseUnimplemented},      // TODO
    {"Contact",             parseContactField},
    {"m",                   parseContactField},       // compact form of contact
    {"Content-Disposition", parseUnimplemented},      // TODO
    {"Content-Encoding",    parseUnimplemented},      // TODO
    {"e",                   parseUnimplemented},      // TODO, compact form of Content-Encoding
    {"Content-Language",    parseUnimplemented},      // TODO
    {"Content-Length",      parseContentLengthField},
    {"l",                   parseContentLengthField}, // compact form of Content-Length
    {"Content-Type",        parseContentTypeField},
    {"c",                   parseContentTypeField},   // compact form of Content-Type
    {"CSeq",                parseCSeqField},
    {"Date",                parseUnimplemented},      // TODO
    {"Error-Info",          parseUnimplemented},      // TODO
    {"Expires",             parseUnimplemented},      // TODO
    {"From",                parseFromField},
    {"f",                   parseFromField},          // compact form of From
    {"In-Reply_to",         parseUnimplemented},      // TODO
    {"Max-Forwards",        parseMaxForwardsField},
    {"In-Reply_to",         parseUnimplemented},      // TODO
    {"MIME-Version",        parseUnimplemented},      // TODO
    {"Min-Expires",         parseUnimplemented},      // TODO
    {"Organization",        parseUnimplemented},      // TODO
    {"Priority",            parseUnimplemented},      // TODO
    {"Proxy-Authenticate",  parseUnimplemented},      // TODO
    {"Proxy-Authorization", parseUnimplemented},      // TODO
    {"Proxy-Require",       parseUnimplemented},      // TODO
    {"Record-Route",        parseRecordRouteField},
    {"Reply-To",            parseUnimplemented},      // TODO
    {"Require",             parseUnimplemented},      // TODO
    {"Retry-After",         parseUnimplemented},      // TODO
    {"Route",               parseUnimplemented},      // TODO
    {"Server",              parseServerField},
    {"Subject",             parseUnimplemented},      // TODO
    {"s",                   parseUnimplemented},      // TODO, compact form of Subject
    {"Supported",           parseUnimplemented},      // TODO
    {"k",                   parseUnimplemented},      // TODO, compact form of Supported
    {"Timestamp",           parseUnimplemented},      // TODO
    {"To",                  parseToField},
    {"t",                   parseToField},            // compact form of To
    {"Unsupported",         parseUnimplemented},      // TODO
    {"User-Agent",          parseUserAgentField},
    {"Via",                 parseViaField},
    {"v",                   parseViaField},           // compact form of Via
    {"Warning",             parseUnimplemented},      // TODO
    {"WWW-Authenticate",    parseUnimplemented},      // TODO
    {"extension-header",    parseUnimplemented}       // TODO
};


SIPTransport::SIPTransport(quint32 transportID, StatisticsInterface *stats):
  partialMessage_(""),
  connection_(nullptr),
  transportID_(transportID),
  stats_(stats),
  processingInProgress_(0)
{}

SIPTransport::~SIPTransport()
{}

void SIPTransport::cleanup()
{
  destroyConnection();
}

bool SIPTransport::isConnected()
{
  return connection_ && connection_->isConnected();
}

QString SIPTransport::getLocalAddress()
{
  Q_ASSERT(connection_);

  QString address = connection_->localAddress().toString();
  if (connection_->localAddress().protocol() == QAbstractSocket::IPv6Protocol)
  {
    address = "[" + address + "]";
  }

  return address;
}

QString SIPTransport::getRemoteAddress()
{
  Q_ASSERT(connection_);

  QString address = connection_->remoteAddress().toString();
  if (connection_->remoteAddress().protocol() == QAbstractSocket::IPv6Protocol)
  {
    address = "[" + address + "]";
  }

  return address;
}

uint16_t SIPTransport::getLocalPort()
{
  Q_ASSERT(connection_);
  return connection_->localPort();
}


void SIPTransport::createConnection(SIPTransportProtocol type, QString target)
{
  if(type == TCP)
  {
    printNormal(this, "Initiating TCP connection for sip connection",
                {"TransportID"}, QString::number(transportID_));
    connection_ = std::shared_ptr<TCPConnection>(new TCPConnection());
    signalConnections();
    connection_->establishConnection(target, SIP_PORT);
  }
  else
  {
    qDebug() << "WARNING: Trying to initiate a SIP Connection with unsupported connection type.";
  }
}


void SIPTransport::incomingTCPConnection(std::shared_ptr<TCPConnection> con)
{
  qDebug() << "This SIP connection uses an incoming connection:" << transportID_;
  if(connection_ != nullptr)
  {
    qDebug() << "Replacing existing connection";
  }
  connection_ = con;

  signalConnections();
}


void SIPTransport::signalConnections()
{
  Q_ASSERT(connection_);
  QObject::connect(connection_.get(), &TCPConnection::messageAvailable,
                   this, &SIPTransport::networkPackage);

  QObject::connect(connection_.get(), &TCPConnection::socketConnected,
                   this, &SIPTransport::connectionEstablished);
}


void SIPTransport::connectionEstablished(QString localAddress, QString remoteAddress)
{
  emit sipTransportEstablished(transportID_,
                                localAddress,
                                remoteAddress);
}


void SIPTransport::destroyConnection()
{
  if(connection_ == nullptr)
  {
    printProgramWarning(this, "Trying to destroy an already destroyed connection");
    return;
  }

  if (processingInProgress_ > 0)
  {
     printNormal(this, "Processing in progress while trying to destroy transport");

     while (processingInProgress_ > 0)
     {
       qSleep(5);
     }
  }

  QObject::disconnect(connection_.get(), &TCPConnection::messageAvailable,
                      this, &SIPTransport::networkPackage);

  QObject::disconnect(connection_.get(), &TCPConnection::socketConnected,
                      this, &SIPTransport::connectionEstablished);

  connection_->exit(0); // stops qthread
  connection_->stopConnection(); // exits run loop
  while(connection_->isRunning())
  {
    qSleep(5);
  }

  connection_.reset();

  printNormal(this, "Destroyed SIP Transport connection");
}


void SIPTransport::sendRequest(SIPRequest& request, QVariant &content)
{
  ++processingInProgress_;
  printImportant(this, "Composing and sending SIP Request:", {"Type"},
                 requestToString(request.method));
  Q_ASSERT(request.message->contentType == MT_NONE || content.isValid());
  Q_ASSERT(connection_ != nullptr);

  if((request.message->contentType != MT_NONE && !content.isValid()))
  {
    printProgramWarning(this, "Invalid SIP request content when sending");
    return;
  }

  if (connection_ == nullptr)
  {
     printProgramWarning(this, "Connection does not exist in sendRequest");
     return;
  }

  routing_.getViaAndContact(request.message,
                            connection_->localAddress().toString(),
                            connection_->localPort());

  // start composing the request.
  // First we turn the struct to fields which are then turned to string
  QList<SIPField> fields;
  if (!composeMandatoryFields(fields, request.message) ||
      !includeMaxForwardsField(fields, request.message->maxForwards))
  {
    qDebug() << "WARNING: Failed to add all the fields. Probably because of missing values.";
    return;
  }

  if (!request.message->routes.empty() &&
      !includeRouteField(fields, request.message->routes))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this,  "Failed to add Route-fields");
  }

  if ((request.method == SIP_INVITE || request.method == SIP_REGISTER) &&
      !includeContactField(fields, request.message->contact))
  {
   qDebug() << "WARNING: Failed to add Contact field. Probably because of missing values.";
   return;
  }

  if (request.method == SIP_REGISTER &&
      (request.message->expires == nullptr ||
      !includeExpiresField(fields, *request.message->expires)))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this,  "Failed to add expires-field");
    return;
  }


  QString lineEnding = "\r\n";
  QString message = "";
  // adds content fields and converts the sdp to string if INVITE
  QString sdp_str = addContent(fields,
                               request.message->contentType != MT_NONE,
                               content.value<SDPMessageInfo>());

  if(!getFirstRequestLine(message, request, lineEnding))
  {
    qDebug() << "WARNING: could not get first request line";
    return;
  }

  message += fieldsToString(fields, lineEnding) + lineEnding;
  message += sdp_str;

  // print the first line
  stats_->addSentSIPMessage(requestToString(request.method),
                            message,
                            connection_->remoteAddress().toString());

  connection_->sendPacket(message);
  --processingInProgress_;
}


void SIPTransport::sendResponse(SIPResponse &response, QVariant &content)
{
  ++processingInProgress_;
  printImportant(this, "Composing and sending SIP Response:", {"Type"},
                 responseToPhrase(response.type));
  Q_ASSERT(response.message->cSeq.method != SIP_INVITE
      || response.type != SIP_OK
      || (response.message->contentType == MT_APPLICATION_SDP && content.isValid()));
  Q_ASSERT(connection_ != nullptr);

  if((response.message->cSeq.method == SIP_INVITE && response.type == SIP_OK
     && (!content.isValid() || response.message->contentType != MT_APPLICATION_SDP))
     || connection_ == nullptr)
  {
    printWarning(this, "SDP nullptr or connection does not exist in sendResponse");
    return;
  }

  QList<SIPField> fields;
  if(!composeMandatoryFields(fields, response.message))
  {
    printWarning(this, "Failed to add mandatory fields. Probably because of missing values.");
    return;
  }

  if (!includeRecordRouteField(fields, response.message->recordRoutes))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this,  "Failed to add RecordRoute-fields");
  }

  routing_.getContactAddress(response.message,
                             connection_->localAddress().toString(),
                             connection_->localPort(), DEFAULT_SIP_TYPE);

  if (response.message->cSeq.method == SIP_INVITE && response.type == SIP_OK &&
      !includeContactField(fields, response.message->contact))
  {
    qDebug() << "ERROR: Failed to compose contact field for SIP OK response.";
  }


  // TODO: if the response is 405 SIP_NOT_ALLOWED we must include an allow header field.
  // lists allowed methods.

  QString lineEnding = "\r\n";
  QString message = "";
  // adds content fields and converts the sdp to string if INVITE
  QString sdp_str = addContent(fields, response.message->cSeq.method == SIP_INVITE
                               && response.type == SIP_OK, content.value<SDPMessageInfo>());
  if(!getFirstResponseLine(message, response, lineEnding))
  {
    qDebug() << "WARNING: could not get first request line";
    return;
  }

  message += fieldsToString(fields, lineEnding) + lineEnding;
  message += sdp_str;

  stats_->addSentSIPMessage(QString::number(responseToCode(response.type))
                            + " " + responseToPhrase(response.type),
                            message,
                            connection_->remoteAddress().toString());


  connection_->sendPacket(message);
  --processingInProgress_;
}


bool SIPTransport::composeMandatoryFields(QList<SIPField>& fields,
                                          std::shared_ptr<SIPMessageHeader> message)
{
  return includeViaFields(fields, message->vias) &&
         includeToField(fields, message->to) &&
         includeFromField(fields, message->from) &&
         includeCallIDField(fields, message->callID) &&
         includeCSeqField(fields, message->cSeq);
}

QString SIPTransport::fieldsToString(QList<SIPField>& fields, QString lineEnding)
{
  QString message = "";
  for(SIPField& field : fields)
  {
    if (field.name != "" &&
        !field.valueSets.empty() &&
        !field.valueSets[0].words.empty())
    {
      message += field.name + ": ";

      for (int i = 0; i < field.valueSets.size(); ++i)
      {
        // add words.
        for (int j = 0; j < field.valueSets.at(i).words.size(); ++j)
        {
          message += field.valueSets.at(i).words.at(j);

          if (j != field.valueSets.at(i).words.size() - 1)
          {
            message += " ";
          }
        }

        // add parameters
        if(field.valueSets.at(i).parameters != nullptr)
        {
          for(SIPParameter& parameter : *field.valueSets.at(i).parameters)
          {
            if (parameter.value != "")
            {
              message += ";" + parameter.name + "=" + parameter.value;
            }
            else
            {
              message += ";" + parameter.name;
            }
          }
        }

        // should we add a comma(,) or end of line
        if (i != field.valueSets.size() - 1)
        {
          message += ",";
        }
        else
        {
          message += lineEnding;
        }
      }
    }
    else
    {
      printProgramError(this, "Failed to convert field to string", "Field name", field.name);
    }
  }
  return message;
}


void SIPTransport::networkPackage(QString package)
{
  if (!isConnected())
  {
    printWarning(this, "Connection not open. Discarding received message");
    return;
  }

  ++processingInProgress_;
  // parse to header and body
  QStringList headers;
  QStringList bodies;

  if (!parsePackage(package, headers, bodies) ||  headers.size() != bodies.size())
  {
    printWarning(this, "Did not receive the whole SIP message");
    --processingInProgress_;
    return;
  }

  for (int i = 0; i < headers.size(); ++i)
  {
    QString header = headers.at(i);
    QString body = bodies.at(i);

    QList<SIPField> fields;
    QString firstLine = "";

    if (!headerToFields(header, firstLine, fields))
    {
      printError(this, "Parsing error converting header to fields.");
      return;
    }

    if (header != "" && firstLine != "" && !fields.empty())
    {

      // Here we start identifying is this a request or a response
      QRegularExpression requestRE("^(\\w+) (sip:\\S+@\\S+) SIP/(" + SIP_VERSION + ")");
      QRegularExpression responseRE("^SIP/(" + SIP_VERSION + ") (\\d\\d\\d) (.+)");
      QRegularExpressionMatch request_match = requestRE.match(firstLine);
      QRegularExpressionMatch response_match = responseRE.match(firstLine);

      // Something is wrong if it matches both
      if (request_match.hasMatch() && response_match.hasMatch())
      {
        printDebug(DEBUG_PROGRAM_ERROR, this,
                   "Both the request and response matched, which should not be possible!");
        --processingInProgress_;
        return;
      }

      // If it matches request
      if (request_match.hasMatch() && request_match.lastCapturedIndex() == 3)
      {
        if (isConnected())
        {
          stats_->addReceivedSIPMessage(request_match.captured(1),
                                        package, connection_->remoteAddress().toString());
        }

        if (!parseRequest(request_match.captured(1), request_match.captured(3),
                          fields, body))
        {
          qDebug() << "Failed to parse request";
          emit parsingError(SIP_BAD_REQUEST, transportID_);
        }
      }
      // first line matches a response
      else if (response_match.hasMatch() && response_match.lastCapturedIndex() == 3)
      {
        if (isConnected())
        {
          stats_->addReceivedSIPMessage(response_match.captured(2) + " " + response_match.captured(3),
                                        package, connection_->remoteAddress().toString());
        }

        if (!parseResponse(response_match.captured(2),
                           response_match.captured(1),
                           response_match.captured(3),
                           fields, body))
        {
          qDebug() << "ERROR: Failed to parse response: " << response_match.captured(2);
          emit parsingError(SIP_BAD_REQUEST, transportID_);
        }
      }
      else
      {
        qDebug() << "Failed to parse first line of SIP message:" << firstLine
                 << "Request index:" << request_match.lastCapturedIndex()
                 << "response index:" << response_match.lastCapturedIndex();

        emit parsingError(SIP_BAD_REQUEST, transportID_);
      }
    }
    else
    {
      qDebug() << "The whole message was not received";
    }
  }
  --processingInProgress_;
}


bool SIPTransport::parsePackage(QString package, QStringList& headers, QStringList& bodies)
{
  // get any parts which have been received before
  if(partialMessage_.length() > 0)
  {
    package = partialMessage_ + package;
    partialMessage_ = "";
  }

  int headerEndIndex = package.indexOf("\r\n\r\n", 0, Qt::CaseInsensitive) + 4;
  int contentLengthIndex = package.indexOf("content-length", 0, Qt::CaseInsensitive);

  // read maximum of 20 messages. 3 is -1 + 4
  for (int i = 0; i < 20 && headerEndIndex != 3; ++i)
  {
    printDebug(DEBUG_NORMAL, this,  "Parsing package to header and body",
                {"Messages parsed so far", "Header end index", "content-length index"},
    {QString::number(i), QString::number(headerEndIndex), QString::number(contentLengthIndex)});

    int contentLength = 0;
    // did we find the contact-length field and is it before end of header.
    if (contentLengthIndex != -1 && contentLengthIndex < headerEndIndex)
    {
      int contentLengthLineEndIndex = package.indexOf("\r\n", contentLengthIndex,
                                                      Qt::CaseInsensitive);
      QString value = package.mid(contentLengthIndex + 16,
                                  contentLengthLineEndIndex - (contentLengthIndex + 16));

      contentLength = value.toInt();
      if (contentLength < 0)
      {
        // TODO: Warn the user maybe. Maybe also ban the user at least temporarily.
        printDebug(DEBUG_PEER_ERROR, this,
                   "Got negative content-length! Peer is doing something very strange.");
        return false;
      }
    }

    printNormal(this, "Parsed Content-length", {"Content-length"}, {QString::number(contentLength)});

    // if we have the whole message
    if(package.length() >= headerEndIndex + contentLength)
    {
      headers.push_back(package.left(headerEndIndex));
      bodies.push_back(package.mid(headerEndIndex, contentLength));

      //printDebug(DEBUG_IMPORTANT,this, "Whole SIP message Received",
      //            {"Body", "Content"}, {headers.last(), bodies.last()});

      package = package.right(package.length() - (headerEndIndex + contentLength));
    }

    headerEndIndex = package.indexOf("\r\n\r\n", 0, Qt::CaseInsensitive) + 4;
    contentLengthIndex = package.indexOf("content-length", 0, Qt::CaseInsensitive);
  }

  partialMessage_ = package;
  return !headers.empty() && headers.size() == bodies.size();
}


bool SIPTransport::headerToFields(QString header, QString& firstLine, QList<SIPField>& fields)
{
  // Divide into lines
  QStringList lines = header.split("\r\n", QString::SkipEmptyParts);
  qDebug() << "Parsing SIP header with" << lines.size() << "lines";
  if(lines.size() == 0)
  {
    qDebug() << "No first line present in SIP header!";
    return false;
  }

  // The message may contain fields that extend more than one line.
  // Combine them so field is only present on one line
  combineContinuationLines(lines);
  firstLine = lines.at(0);

  QStringList debugLineNames = {};
  for(int i = 1; i < lines.size(); ++i)
  {
    SIPField field = {"", {}};
    QStringList valueSets;

    if (parseFieldName(lines[i], field) &&
        parseFieldValueSets(lines[i], valueSets))
    {
      // Check the correct number of valueSets for Field
      if (valueSets.size() == 0 || valueSets.size() > 100)
      {
        printDebug(DEBUG_PEER_ERROR, this,
                   "Incorrect amount of comma separated sets in field",
                    {"Field", "Amount"}, {field.name, QString::number(valueSets.size())});
        return false;
      }

      for (QString& value : valueSets)
      {
        if (!parseFieldValue(value, field))
        {
          qDebug() << "Failed to parse field:" << field.name;
          return false;
        }
      }

      debugLineNames << field.name;
      fields.push_back(field);
    }
    else
    {
      return false;
    }
  }

  qDebug() << "Found following SIP fields:" << debugLineNames;

  return true;
}


bool SIPTransport::combineContinuationLines(QStringList& lines)
{
  for (int i = 1; i < lines.size(); ++i)
  {
    // combine current line with previous if there a space at the beginning
    if (lines.at(i).front().isSpace())
    {
      printNormal(this,  "Found a continuation line");
      lines[i - 1].append(lines.at(i));
      lines.erase(lines.begin() + i);
      --i;
    }
  }

  return true;
}


bool SIPTransport::parseFieldName(QString& line, SIPField& field)
{
  QRegularExpression re_field("(\\S*):\\s+(.+)");
  QRegularExpressionMatch field_match = re_field.match(line);

  // separate name from rest
  if(field_match.hasMatch() && field_match.lastCapturedIndex() == 2)
  {
    field.name = field_match.captured(1);
    line = field_match.captured(2);
  }
  else
  {
    return false;
  }
  return true;
}


bool SIPTransport::parseFieldValueSets(QString& line, QStringList& outValueSets)
{
  // separate value sections by commas
  outValueSets = line.split(",", QString::SkipEmptyParts);
  return !outValueSets.empty();
}


bool SIPTransport::parseFieldValue(QString& valueSet, SIPField& field)
{
  // RFC3261_TODO: Uniformalize case formatting. Make everything big or small case expect quotes.
  SIPValueSet set = SIPValueSet{{}, nullptr};

  QString currentWord = "";
  bool isQuotation = false;
  bool isURI = false;
  bool isParameter = false;
  int comments = 0;

  SIPParameter parameter;

  for (QChar& character : valueSet)
  {
    // add character to word if it is not parsed out
    if (isURI || (isQuotation && character != "\"") ||
        (character != " "
        && character != "\""
        && character != ";"
        && character != "="
        && character != "("
        && character != ")"
        && !comments))
    {
      currentWord += character;
    }

    // push current word if it ended
    if (!comments)
    {
      if ((character == "\"" && isQuotation) ||
          (character == ">" && isURI))
      {
        if (!isParameter && currentWord != "")
        {
          set.words.push_back(currentWord);
          currentWord = "";
        }
        else
        {
          return false;
        }
      }
      else if (character == " " && !isQuotation)
      {
        if (!isParameter && currentWord != "")
        {
          set.words.push_back(currentWord);
        }
        currentWord = "";
      }
      else if((character == "=" && isParameter) ||
         (character == ";" && !isURI))
      {
        if (!isParameter)
        {
          if (character == ";" && currentWord != "")
          {
            // last word before parameters
            set.words.push_back(currentWord);
            currentWord = "";
          }
        }
        else // isParameter
        {
          if (character == "=")
          {
            if (parameter.name != "")
            {
              return false; // got name when we already have one
            }
            else
            {
              parameter.name = currentWord;
              currentWord = "";
            }
          }
          else if (character == ";")
          {
            addParameterToSet(parameter, currentWord, set);
          }
        }
      }
    }

    // change the state of parsing
    if (character == "\"") // quote
    {
      isQuotation = !isQuotation;
    }
    else if (character == "<") // start of URI
    {
      if (isQuotation || isURI)
      {
        return false;
      }
      isURI = true;
    }
    else if (character == ">") // end of URI
    {
      if (!isURI || isQuotation)
      {
        return false;
      }
      isURI = false;
    }
    else if (character == "(")
    {
      ++comments;
    }
    else if (character == ")")
    {
      if (!comments)
      {
        return false;
      }

      --comments;
    }
    else if (!isURI && !isQuotation && character == ";") // parameter
    {
      isParameter = true;
    }
  }

  // add last word
  if (isParameter)
  {
    addParameterToSet(parameter, currentWord, set);
  }
  else if (currentWord != "")
  {
    set.words.push_back(currentWord);
    currentWord = "";
  }

  field.valueSets.push_back(set);

  return true;
}


void SIPTransport::addParameterToSet(SIPParameter& currentParameter, QString &currentWord,
                                     SIPValueSet& valueSet)
{
  if (currentParameter.name == "")
  {
    currentParameter.name = currentWord;
  }
  else
  {
    currentParameter.value = currentWord;
  }
  currentWord = "";

  if (valueSet.parameters == nullptr)
  {
    valueSet.parameters = std::shared_ptr<QList<SIPParameter>> (new QList<SIPParameter>);
  }
  valueSet.parameters->push_back(currentParameter);
  currentParameter = SIPParameter{"",""};
}


bool SIPTransport::fieldsToMessageHeader(QList<SIPField>& fields,
                                         std::shared_ptr<SIPMessageHeader>& message)
{
  message->cSeq.cSeq = 0;
  message->cSeq.method = SIP_NO_REQUEST;
  message->maxForwards = 0;
  message->to =   {{"",SIP_URI{}}, ""};
  message->from = {{"",SIP_URI{}}, ""};
  message->contentType = MT_NONE;
  message->contentLength = 0;
  message->expires = 0;

  for(int i = 0; i < fields.size(); ++i)
  {
    if(parsing.find(fields[i].name) == parsing.end())
    {
      qDebug() << "Field not supported:" << fields[i].name;
    }
    else
    {
      if(!parsing.at(fields.at(i).name)(fields[i], message))
      {
        qDebug() << "Failed to parse following field:" << fields.at(i).name;
        //qDebug() << "Values:" << fields.at(i).valueSets.at(0).values;
        return false;
      }
    }
  }
  return true;
}


bool SIPTransport::parseRequest(QString requestString, QString version,
                                QList<SIPField> &fields, QString& body)
{  
  qDebug() << "Request detected:" << requestString;

  printImportant(this, "Parsing incoming request", {"Type"}, {requestString});
  SIPRequest request;
  request.method = stringToRequest(requestString);
  request.sipVersion = version;
  request.message = std::shared_ptr<SIPMessageHeader> (new SIPMessageHeader);


  if(request.method == SIP_NO_REQUEST)
  {
    qDebug() << "Could not recognize request type!";
    return false;
  }

  if (!requestSanityCheck(fields, request.method))
  {
    return false;
  }

  if (!fieldsToMessageHeader(fields, request.message))
  {
    return false;
  }

  QVariant content;
  if (body != "" && request.message->contentType != MT_NONE)
  {
    parseContent(content, request.message->contentType, body);
  }

  emit incomingSIPRequest(request, getLocalAddress(), content, transportID_);
  return true;
}


bool SIPTransport::parseResponse(QString responseString, QString version,
                                 QString text, QList<SIPField> &fields,
                                 QString& body)
{
  printImportant(this, "Parsing incoming response", {"Type"}, {responseString});

  SIPResponse response;
  response.type = codeToResponse(stringToResponseCode(responseString));
  response.message = std::shared_ptr<SIPMessageHeader> (new SIPMessageHeader);
  response.text = text;
  response.sipVersion = version;

  if (!responseSanityCheck(fields, response.type))
  {
    return false;
  }

  if (!fieldsToMessageHeader(fields, response.message))
  {
    return false;
  }

  QVariant content;
  if (body != "" && response.message->contentType != MT_NONE)
  {
    parseContent(content, response.message->contentType, body);
  }

  if (isConnected())
  {
    routing_.processResponseViaFields(response.message->vias,
                                      connection_->localAddress().toString(),
                                      connection_->localPort());
  }
  else
  {
    printWarning(this, "Disconnected while parsing response");
    return false;
  }

  emit incomingSIPResponse(response, content);

  return true;
}


void SIPTransport::parseContent(QVariant& content, MediaType type, QString& body)
{
  if(type == MT_APPLICATION_SDP)
  {
    SDPMessageInfo sdp;
    if(parseSDPContent(body, sdp))
    {
      qDebug () << "Successfully parsed SDP";
      content.setValue(sdp);
    }
    else
    {
      qDebug () << "Failed to parse SDP message";
    }
  }
  else
  {
    qDebug() << "Unsupported content type detected!";
  }
}


QString SIPTransport::addContent(QList<SIPField>& fields, bool haveContent,
                                 const SDPMessageInfo &sdp)
{
  QString sdp_str = "";

  if(haveContent)
  {
    sdp_str = composeSDPContent(sdp);
    if(sdp_str == "" ||
       !includeContentLengthField(fields, sdp_str.length()) ||
       !includeContentTypeField(fields, "application/sdp"))
    {
      qDebug() << "WARNING: Could not add sdp fields to request";
      return "";
    }
  }
  else if(!includeContentLengthField(fields, 0))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this, 
               "Could not add content-length field to sip message!");
  }
  return sdp_str;
}
