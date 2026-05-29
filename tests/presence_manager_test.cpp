#include "presence_manager.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "device_db_manager.h"

int main() {
  PresenceManager presence;
  websocketpp::connection_hdl hdl;
  int failures = 0;

  auto expect = [&failures](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      ++failures;
    }
  };

  expect(presence.GetOnlineDeviceCount() == 0,
         "initial online device count is zero");
  expect(presence.GetOnlineWebClientCount() == 0,
         "initial online web client count is zero");

  presence.OnLogin("device-1", "device-1", hdl);
  expect(presence.GetOnlineDeviceCount() == 1,
         "regular device increments online device count");
  expect(presence.GetOnlineWebClientCount() == 0,
         "regular device does not increment web client count");

  presence.OnLogin("web-1", "web-1", hdl);
  expect(presence.GetOnlineDeviceCount() == 1,
         "web client does not increment online device count");
  expect(presence.GetOnlineWebClientCount() == 1,
         "web client increments web client count");

  presence.OnLogin("C-000000", "C-000000", hdl);
  expect(presence.GetOnlineDeviceCount() == 1,
         "clone client does not increment online device count");
  expect(presence.GetOnlineWebClientCount() == 1,
         "clone client does not increment web client count");

  presence.OnLogout("C-000000");
  expect(presence.GetOnlineDeviceCount() == 1,
         "clone logout leaves online device count unchanged");
  expect(presence.GetOnlineWebClientCount() == 1,
         "clone logout leaves web client count unchanged");

  presence.OnLogout("web-1");
  expect(presence.GetOnlineDeviceCount() == 1,
         "web logout leaves online device count unchanged");
  expect(presence.GetOnlineWebClientCount() == 0,
         "web logout decrements web client count");

  const auto db_path =
      std::filesystem::temp_directory_path() /
      ("crossdesk_presence_manager_test_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".db");
  {
    DeviceDBManager db(db_path.string());
    db.SetDeviceOnline("device-1", true);
    db.SetDeviceOnline("web-1", true);
    db.SetDeviceOnline("C-000000", true);
    expect(db.GetOnlineDeviceCount() == 1,
           "database online device count excludes web and clone clients");
    auto online_devices = db.ListOnlineDevices();
    expect(online_devices.size() == 1,
           "online device list excludes web and clone clients");
    expect(online_devices[0].device_id == "device-1",
           "online device list returns regular device id");
    expect(online_devices[0].online,
           "online device list marks device online");
    expect(online_devices[0].updated_at > 0,
           "online device list includes updated_at");
  }
  std::filesystem::remove(db_path);

  return failures == 0 ? 0 : 1;
}
