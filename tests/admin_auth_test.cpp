#include "admin_auth.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main() {
  int failures = 0;

  auto expect = [&failures](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      ++failures;
    }
  };

  AdminAuth disabled("", "", std::chrono::seconds(60));
  expect(!disabled.IsEnabled(), "empty credentials disable admin auth");
  expect(!disabled.Login("admin", "secret").has_value(),
         "disabled auth rejects login");

  AdminAuth auth("admin", "secret", std::chrono::seconds(60));
  expect(auth.IsEnabled(), "non-empty credentials enable admin auth");
  expect(!auth.Login("admin", "wrong").has_value(),
         "wrong password rejects login");
  expect(!auth.Login("wrong", "secret").has_value(),
         "wrong username rejects login");

  auto token = auth.Login("admin", "secret");
  expect(token.has_value(), "valid credentials create session token");
  expect(!token->empty(), "session token is not empty");
  expect(auth.ValidateSession(*token), "created session validates");

  std::string cookie = auth.BuildSessionCookie(*token);
  expect(cookie.find("cd_admin_session=" + *token) != std::string::npos,
         "session cookie contains token");
  expect(cookie.find("HttpOnly") != std::string::npos,
         "session cookie is http only");
  expect(cookie.find("Secure") != std::string::npos,
         "session cookie is secure");
  expect(cookie.find("SameSite=Strict") != std::string::npos,
         "session cookie has strict same site policy");

  std::string extracted = AdminAuth::ExtractCookie(
      "foo=bar; cd_admin_session=" + *token + "; theme=dark",
      "cd_admin_session");
  expect(extracted == *token, "cookie extraction returns named cookie value");
  expect(AdminAuth::ExtractCookie("foo=bar", "cd_admin_session").empty(),
         "cookie extraction returns empty when missing");

  auth.Logout(*token);
  expect(!auth.ValidateSession(*token), "logout invalidates session");

  AdminAuth expiring("admin", "secret", std::chrono::seconds(0));
  auto expired = expiring.Login("admin", "secret");
  expect(expired.has_value(), "zero ttl login still creates token");
  expect(!expiring.ValidateSession(*expired),
         "expired session does not validate");
  expect(expiring.BuildExpiredCookie().find("Max-Age=0") != std::string::npos,
         "expired cookie clears browser cookie");

  return failures == 0 ? 0 : 1;
}
