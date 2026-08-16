#include "turn_credentials.h"

#include <iostream>
#include <string>

int main() {
  int failures = 0;
  auto expect = [&failures](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      ++failures;
    }
  };

  TurnCredentialIssuer issuer("test-secret", "turn.example.com", 3478, 3600);
  const TurnCredentials credentials = issuer.IssueAt("device-123", 1700000000);

  expect(credentials.host == "turn.example.com", "TURN host is preserved");
  expect(credentials.port == 3478, "TURN port is preserved");
  expect(credentials.expires_at == 1700003600,
         "TURN expiration includes configured TTL");
  expect(credentials.username == "1700003600:device-123",
         "TURN REST username contains expiry and user id");
  expect(credentials.password == "leVRZCuGzm2O76WuoBJJD8KDmio=",
         "TURN password matches HMAC-SHA1 test vector");

  if (failures > 0) {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  return 0;
}
