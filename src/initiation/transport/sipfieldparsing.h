#pragma once

#include "initiation/siptypes.h"

/* A module for parsing SIP header fields SIP message, but not the first line.
 *
 * returns whether the parsing was successful.
 *
 * Currently these parse the whole comma separated list, but it would probably
 *  be better if they parsed only one value from the list at a time. No need
 *   to loop through the list in every one of these
 *
 *
 * Please call check parsing possibility with parsingPreCheck before calling any
 * of the parsing functions.
 *
*/


// checks parsing preconditions such as whether message exists and whether all
// word lists have words in them
bool parsingPreChecks(SIPField& field,
                      std::shared_ptr<SIPMessageHeader> message,
                      bool emptyPossible = false);

bool parseAcceptField(SIPField& field,
                      std::shared_ptr<SIPMessageHeader> message);

bool parseAcceptEncodingField(SIPField& field,
                              std::shared_ptr<SIPMessageHeader> message);

bool parseAcceptLanguageField(SIPField& field,
                              std::shared_ptr<SIPMessageHeader> message);

bool parseAlertInfoField(SIPField& field,
                         std::shared_ptr<SIPMessageHeader> message);

bool parseAllowField(SIPField& field,
                     std::shared_ptr<SIPMessageHeader> message);

bool parseAuthInfoField(SIPField& field,
                        std::shared_ptr<SIPMessageHeader> message);

bool parseAuthorizationField(SIPField& field,
                             std::shared_ptr<SIPMessageHeader> message);

bool parseCallIDField(SIPField& field,
                      std::shared_ptr<SIPMessageHeader> message);

bool parseCallInfoField(SIPField& field,
                        std::shared_ptr<SIPMessageHeader> message);

bool parseContactField(SIPField& field,
                       std::shared_ptr<SIPMessageHeader> message);

bool parseContentDispositionField(SIPField& field,
                                  std::shared_ptr<SIPMessageHeader> message);

bool parseContentEncodingField(SIPField& field,
                               std::shared_ptr<SIPMessageHeader> message);

bool parseContentLanguageField(SIPField& field,
                               std::shared_ptr<SIPMessageHeader> message);

bool parseContentLengthField(SIPField& field,
                             std::shared_ptr<SIPMessageHeader> message);

bool parseContentTypeField(SIPField& field,
                           std::shared_ptr<SIPMessageHeader> message);

bool parseCSeqField(SIPField& field,
                    std::shared_ptr<SIPMessageHeader> message);

bool parseDateField(SIPField& field,
                    std::shared_ptr<SIPMessageHeader> message);

bool parseErrorInfoField(SIPField& field,
                         std::shared_ptr<SIPMessageHeader> message);

bool parseExpireField(SIPField& field,
                      std::shared_ptr<SIPMessageHeader> message);

bool parseFromField(SIPField& field,
                    std::shared_ptr<SIPMessageHeader> message);

bool parseInReplyToField(SIPField& field,
                         std::shared_ptr<SIPMessageHeader> message);

bool parseMaxForwardsField(SIPField& field,
                           std::shared_ptr<SIPMessageHeader> message);

bool parseMinExpiresField(SIPField& field,
                          std::shared_ptr<SIPMessageHeader> message);

bool parseMIMEVersionField(SIPField& field,
                           std::shared_ptr<SIPMessageHeader> message);

bool parseOrganizationField(SIPField& field,
                            std::shared_ptr<SIPMessageHeader> message);

bool parsePriorityField(SIPField& field,
                        std::shared_ptr<SIPMessageHeader> message);

bool parseProxyAuthenticateField(SIPField& field,
                                 std::shared_ptr<SIPMessageHeader> message);

bool parseProxyAuthorizationField(SIPField& field,
                                  std::shared_ptr<SIPMessageHeader> message);

bool parseProxyRequireField(SIPField& field,
                            std::shared_ptr<SIPMessageHeader> message);

bool parseRecordRouteField(SIPField& field,
                           std::shared_ptr<SIPMessageHeader> message);

bool parseReplyToField(SIPField& field,
                       std::shared_ptr<SIPMessageHeader> message);

bool parseRequireField(SIPField& field,
                       std::shared_ptr<SIPMessageHeader> message);

bool parseRetryAfterField(SIPField& field,
                          std::shared_ptr<SIPMessageHeader> message);

bool parseRouteField(SIPField& field,
                     std::shared_ptr<SIPMessageHeader> message);

bool parseServerField(SIPField& field,
                      std::shared_ptr<SIPMessageHeader> message);

bool parseSubjectField(SIPField& field,
                       std::shared_ptr<SIPMessageHeader> message);

bool parseSupportedField(SIPField& field,
                         std::shared_ptr<SIPMessageHeader> message);

bool parseTimestampField(SIPField& field,
                         std::shared_ptr<SIPMessageHeader> message);

bool parseToField(SIPField& field,
                  std::shared_ptr<SIPMessageHeader> message);

bool parseUnsupportedField(SIPField& field,
                           std::shared_ptr<SIPMessageHeader> message);

bool parseUserAgentField(SIPField& field,
                         std::shared_ptr<SIPMessageHeader> message);

bool parseViaField(SIPField& field,
                   std::shared_ptr<SIPMessageHeader> message);

bool parseWarningField(SIPField& field,
                       std::shared_ptr<SIPMessageHeader> message);

bool parseWWWAuthenticateField(SIPField& field,
                               std::shared_ptr<SIPMessageHeader> message);
