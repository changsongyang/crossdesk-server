#include "admin_controller.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "admin_auth.h"
#include "transmission_manager.h"

int main() {
  int failures = 0;

  auto expect = [&failures](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      ++failures;
    }
  };

  expect(AdminController::IsAdminRoute("/admin"), "/admin is admin route");
  expect(AdminController::IsAdminRoute("/api/admin/overview"),
         "/api/admin/overview is admin route");
  expect(!AdminController::IsAdminRoute("/api/stats"),
         "/api/stats is not admin route");
  expect(!AdminController::IsAdminRoute("/api/adminx"),
         "/api/adminx is not admin route");

  expect(AdminController::ExtractDisconnectTransmissionId(
             "/api/admin/sessions/100284391/disconnect") == "100284391",
         "disconnect route extracts transmission id");
  expect(AdminController::ExtractDisconnectTransmissionId(
             "/api/admin/sessions/100284391") == "",
         "non-disconnect route does not extract id");

  auto transmission = std::make_shared<TransmissionManager>();
  AdminAuth disabled("", "", std::chrono::seconds(60));
  AdminController disabled_controller(
      &disabled, nullptr, transmission, nullptr,
      [](const std::string&, nlohmann::json) {});

  AdminHttpResponse disabled_response = disabled_controller.Handle(
      {"GET", "/api/admin/overview", "", ""});
  expect(disabled_response.status == 503,
         "disabled admin API returns service unavailable");
  expect(disabled_response.body.find("admin_disabled") != std::string::npos,
         "disabled admin API returns admin_disabled error");

  AdminAuth auth("admin", "secret", std::chrono::seconds(60));
  AdminController controller(&auth, nullptr, transmission, nullptr,
                             [](const std::string&, nlohmann::json) {});
  AdminHttpResponse unauthorized =
      controller.Handle({"GET", "/api/admin/overview", "", ""});
  expect(unauthorized.status == 401,
         "protected admin API requires session");
  expect(unauthorized.body.find("unauthorized") != std::string::npos,
         "protected admin API returns unauthorized error");

  AdminHttpResponse login = controller.Handle(
      {"POST", "/api/admin/login",
       R"({"username":"admin","password":"secret"})", ""});
  expect(login.status == 200, "valid login returns ok");
  bool has_cookie = false;
  for (const auto& header : login.headers) {
    if (header.first == "Set-Cookie" &&
        header.second.find("cd_admin_session=") != std::string::npos) {
      has_cookie = true;
    }
  }
  expect(has_cookie, "valid login sets session cookie");

  AdminHttpResponse bad_login = controller.Handle(
      {"POST", "/api/admin/login",
       R"({"username":"admin","password":"wrong"})", ""});
  expect(bad_login.status == 401, "invalid login returns unauthorized");

  transmission->BindHostToTransmission("host-1", "host-1");
  transmission->BindGuestToTransmission("guest-1", "host-1");
  auto token = auth.Login("admin", "secret");
  AdminHttpResponse disconnect = controller.Handle(
      {"POST", "/api/admin/sessions/host-1/disconnect", "",
       "cd_admin_session=" + *token});
  expect(disconnect.status == 200, "disconnect returns ok");
  expect(transmission->GetTransmissionSnapshots().empty(),
         "disconnect releases transmission");

  AdminHttpResponse already_closed = controller.Handle(
      {"POST", "/api/admin/sessions/host-1/disconnect", "",
       "cd_admin_session=" + *token});
  expect(already_closed.status == 200,
         "disconnect missing transmission is idempotent");
  expect(already_closed.body.find("already_closed") != std::string::npos,
         "idempotent disconnect reports already_closed");

  return failures == 0 ? 0 : 1;
}
