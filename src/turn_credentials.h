#ifndef _TURN_CREDENTIALS_H_
#define _TURN_CREDENTIALS_H_

#include <cstdint>
#include <string>

struct TurnCredentials {
  std::string host;
  uint16_t port = 0;
  std::string username;
  std::string password;
  int64_t expires_at = 0;
};

class TurnCredentialIssuer {
 public:
  TurnCredentialIssuer(std::string shared_secret, std::string host,
                       uint16_t port, uint32_t ttl_seconds);

  TurnCredentials Issue(const std::string& user_id) const;
  TurnCredentials IssueAt(const std::string& user_id,
                          int64_t now_seconds) const;

 private:
  std::string shared_secret_;
  std::string host_;
  uint16_t port_ = 0;
  uint32_t ttl_seconds_ = 0;
};

#endif
