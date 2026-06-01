#include "admin_controller.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "admin_auth.h"
#include "device_db_manager.h"
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
  expect(AdminController::IsAdminRoute("/api/admin/overview?session_limit=1"),
         "/api/admin/overview with query is admin route");
  expect(!AdminController::IsAdminRoute("/api/stats"),
         "/api/stats is not admin route");
  expect(!AdminController::IsAdminRoute("/api/adminx"),
         "/api/adminx is not admin route");

  expect(AdminController::ExtractDisconnectTransmissionId(
             "/api/admin/sessions/100284391/disconnect") == "100284391",
         "disconnect route extracts transmission id");
  expect(AdminController::ExtractDisconnectTransmissionId(
             "/api/admin/sessions/100284391/disconnect?x=1") == "100284391",
         "disconnect route extracts transmission id with query");
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
  AdminHttpResponse overview = controller.Handle(
      {"GET",
       "/api/admin/overview?session_limit=1&session_offset=0&session_search=guest-1",
       "", "cd_admin_session=" + *token});
  expect(overview.status == 200, "overview with pagination returns ok");
  auto overview_body = nlohmann::json::parse(overview.body);
  expect(overview_body["sessions"].size() == 1,
         "overview applies session pagination");
  expect(overview_body["sessions_page"]["total"] == 1,
         "overview reports filtered session total");
  AdminHttpResponse stats = controller.Handle(
      {"GET", "/api/admin/stats", "", "cd_admin_session=" + *token});
  expect(stats.status == 200, "stats returns ok");
  auto stats_body = nlohmann::json::parse(stats.body);
  expect(stats_body["stats"]["active_connection_count"] == 1,
         "stats reports active connection count");
  expect(stats_body["stats"]["online_duration_seconds"] == 0,
         "stats reports online duration without database");

  const auto db_path =
      std::filesystem::temp_directory_path() /
      ("crossdesk_admin_controller_test_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".db");
  {
    DeviceDBManager db(db_path.string());
    db.SetDeviceOnline("device-admin-1", true);
    db.SetDeviceOnline("device-admin-offline", true);
    db.SetDeviceOnline("device-admin-offline", false);
    db.SetDeviceOnline("device-admin-control", true);
    db.SetDeviceOnline("web-admin-1", true);
    db.StartRemoteControlSession("tx-admin", "device-admin-1",
                                 "device-admin-offline");
    db.EndRemoteControlSession("tx-admin", "device-admin-1",
                               "device-admin-offline");
    db.StartRemoteControlSession("tx-admin-live", "device-admin-1",
                                 "device-admin-offline");
    db.StartRemoteControlSession("tx-admin-clone", "device-admin-1",
                                 "C-device-admin-control");
    AdminController db_controller(&auth, nullptr, transmission, &db,
                                  [](const std::string&, nlohmann::json) {});
    AdminHttpResponse db_overview = db_controller.Handle(
        {"GET", "/api/admin/overview?device_search=device-admin-1", "",
         "cd_admin_session=" + *token});
    expect(db_overview.status == 200,
           "overview with device durations returns ok");
    auto db_overview_body = nlohmann::json::parse(db_overview.body);
    expect(db_overview_body["devices"].size() == 1,
           "overview defaults to filtered online devices");
    expect(db_overview_body["devices"][0]["online_since"] > 0,
           "overview reports device online_since");
    expect(db_overview_body["devices"][0].contains("online_duration_seconds"),
           "overview reports device online duration");
    expect(db_overview_body["devices"][0].contains("total_online_seconds"),
           "overview reports device total online duration");
    expect(db_overview_body["devices"][0].contains("total_control_seconds"),
           "overview reports device total control duration");
    expect(db_overview_body["devices"][0].contains("total_controlled_seconds"),
           "overview reports device total controlled duration");
    expect(db_overview_body["stats"].contains("online_duration_seconds"),
           "overview stats include online duration");
    expect(db_overview_body["stats"].contains("total_control_seconds"),
           "overview stats include total control duration");
    expect(db_overview_body["stats"].contains("total_controlled_seconds"),
           "overview stats include total controlled duration");
    expect(db_overview_body["device_counts"]["all"] == 1,
           "overview search scopes all device count");
    expect(db_overview_body["device_counts"]["online"] == 1,
           "overview reports online device count");
    expect(db_overview_body["device_counts"]["offline"] == 0,
           "overview search scopes offline count");
    expect(db_overview_body["device_counts"]["active"] == 1,
           "overview search scopes active count");
    expect(db_overview_body["device_counts"]["web"] == 0,
           "overview search scopes web count");

    AdminHttpResponse offline_overview = db_controller.Handle(
        {"GET",
         "/api/admin/overview?device_filter=offline&device_search=device-admin-offline",
         "",
         "cd_admin_session=" + *token});
    expect(offline_overview.status == 200,
           "overview with offline device returns ok");
    auto offline_body = nlohmann::json::parse(offline_overview.body);
    expect(offline_body["devices"].size() == 1,
           "overview keeps offline device in presence list");
    expect(!offline_body["devices"][0]["online"].get<bool>(),
           "overview reports offline status");
    expect(offline_body["devices"][0]["last_online_at"] > 0,
           "overview reports last online timestamp for offline device");
    expect(offline_body["devices"][0]["online_duration_seconds"] == 0,
           "overview reports zero current duration for offline device");

    AdminHttpResponse active_overview = db_controller.Handle(
        {"GET",
         "/api/admin/overview?device_filter=active&device_sort=device_id&device_order=asc",
         "",
         "cd_admin_session=" + *token});
    expect(active_overview.status == 200,
           "overview with active device filter returns ok");
    auto active_body = nlohmann::json::parse(active_overview.body);
    expect(active_body["devices_page"]["total"] == 3,
           "overview reports active device total");
    expect(active_body["devices"].size() == 3,
           "overview returns active devices");
    expect(active_body["devices"][0]["id"] == "device-admin-1",
           "overview applies device sort order");
    expect(active_body["devices"][0]["active_controlled_count"] == 2,
           "overview reports active controlled count");
    expect(active_body["devices"][1]["id"] == "device-admin-control" &&
               active_body["devices"][1]["active_control_count"] == 1,
           "overview maps clone guest control count to base device");
    expect(active_body["devices"][2]["active_control_count"] == 1,
           "overview reports active control count");

    AdminHttpResponse web_overview = db_controller.Handle(
        {"GET", "/api/admin/overview?device_filter=web", "",
         "cd_admin_session=" + *token});
    expect(web_overview.status == 200,
           "overview with web client filter returns ok");
    auto web_body = nlohmann::json::parse(web_overview.body);
    expect(web_body["devices"].size() == 1,
           "overview returns web clients on web filter");
    expect(web_body["devices"][0]["kind"] == "web",
           "overview marks web client kind");
  }
  std::filesystem::remove(db_path);

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
