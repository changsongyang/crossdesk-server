#include "turn_credentials.h"

#include <chrono>
#include <climits>
#include <limits>
#include <stdexcept>
#include <utility>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace {

std::string Base64Encode(const unsigned char* data, size_t size) {
  if (!data || size == 0 || size > static_cast<size_t>(INT_MAX)) {
    throw std::runtime_error("Invalid TURN credential digest");
  }

  // EVP_EncodeBlock also writes a trailing NUL byte.
  std::string encoded(4 * ((size + 2) / 3) + 1, '\0');
  const int encoded_size = EVP_EncodeBlock(
      reinterpret_cast<unsigned char*>(encoded.data()), data,
      static_cast<int>(size));
  if (encoded_size <= 0) {
    throw std::runtime_error("Failed to encode TURN credential");
  }
  encoded.resize(static_cast<size_t>(encoded_size));
  return encoded;
}

}  // namespace

TurnCredentialIssuer::TurnCredentialIssuer(std::string shared_secret,
                                           std::string host, uint16_t port,
                                           uint32_t ttl_seconds)
    : shared_secret_(std::move(shared_secret)),
      host_(std::move(host)),
      port_(port),
      ttl_seconds_(ttl_seconds) {
  if (shared_secret_.empty()) {
    throw std::invalid_argument("TURN shared secret must not be empty");
  }
  if (shared_secret_.size() > static_cast<size_t>(INT_MAX)) {
    throw std::invalid_argument("TURN shared secret is too large");
  }
  if (host_.empty()) {
    throw std::invalid_argument("TURN public host must not be empty");
  }
  if (port_ == 0) {
    throw std::invalid_argument("TURN port must not be zero");
  }
  if (ttl_seconds_ == 0) {
    throw std::invalid_argument("TURN credential TTL must not be zero");
  }
}

TurnCredentials TurnCredentialIssuer::Issue(const std::string& user_id) const {
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  return IssueAt(user_id, now);
}

TurnCredentials TurnCredentialIssuer::IssueAt(const std::string& user_id,
                                               int64_t now_seconds) const {
  if (user_id.empty()) {
    throw std::invalid_argument("TURN credential user id must not be empty");
  }
  if (now_seconds < 0 ||
      now_seconds > std::numeric_limits<int64_t>::max() - ttl_seconds_) {
    throw std::invalid_argument("Invalid TURN credential issue time");
  }

  TurnCredentials credentials;
  credentials.host = host_;
  credentials.port = port_;
  credentials.expires_at = now_seconds + ttl_seconds_;
  credentials.username =
      std::to_string(credentials.expires_at) + ":" + user_id;

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_size = 0;
  const unsigned char* result = HMAC(
      EVP_sha1(), shared_secret_.data(), static_cast<int>(shared_secret_.size()),
      reinterpret_cast<const unsigned char*>(credentials.username.data()),
      credentials.username.size(), digest, &digest_size);
  if (!result || digest_size == 0) {
    throw std::runtime_error("Failed to sign TURN credential");
  }
  credentials.password = Base64Encode(digest, digest_size);
  return credentials;
}
