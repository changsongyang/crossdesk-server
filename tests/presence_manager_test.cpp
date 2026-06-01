#include "presence_manager.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

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
  int64_t total_online_before_restart = 0;
  int64_t total_control_before_restart = 0;
  int64_t total_controlled_before_restart = 0;
  {
    DeviceDBManager db(db_path.string());
    db.SetDeviceOnline("device-1", true);
    db.SetDeviceOnline("web-1", true);
    db.SetDeviceOnline("C-000000", true);
    expect(db.GetOnlineDeviceCount() == 1,
           "database online device count excludes web and clone clients");
    expect(db.CountDevicePresence() == 1,
           "database device presence count excludes web and clone clients");
    auto online_devices = db.ListOnlineDevices();
    expect(online_devices.size() == 1,
           "online device list excludes web and clone clients");
    expect(online_devices[0].device_id == "device-1",
           "online device list returns regular device id");
    expect(online_devices[0].online,
           "online device list marks device online");
    expect(online_devices[0].updated_at > 0,
           "online device list includes updated_at");
    expect(online_devices[0].online_since > 0,
           "online device list includes online_since");
    expect(online_devices[0].online_duration_seconds >= 0,
           "online device list includes current online duration");
    expect(online_devices[0].total_online_seconds >=
               online_devices[0].online_duration_seconds,
           "online device list includes total online duration");
    db.SetDeviceOnline("device-2", true);
    db.SetDeviceOnline("device-3", true);
    expect(db.CountOnlineDevices() == 3,
           "database online device count includes regular devices");
    expect(db.CountDevicePresence() == 3,
           "database device presence count includes regular devices");
    expect(db.ListOnlineDevices(2, 0, "").size() == 2,
           "online device list supports page limit");
    expect(db.ListOnlineDevices(2, 2, "").size() == 1,
           "online device list supports page offset");
    auto filtered_devices = db.ListOnlineDevices(10, 0, "device-2");
    expect(db.CountOnlineDevices("device-2") == 1,
           "online device count supports search");
    expect(filtered_devices.size() == 1 &&
               filtered_devices[0].device_id == "device-2",
           "online device list supports search");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto duration_stats = db.GetOnlineDurationStats();
    expect(duration_stats.current_online_seconds >= 1,
           "database sums current online duration");
    db.SetDeviceOnline("device-1", false);
    auto accumulated_stats = db.GetOnlineDurationStats();
    expect(accumulated_stats.total_online_seconds >= 1,
           "database accumulates total online duration after logout");
    expect(db.CountOnlineDevices() == 2,
           "offline device no longer counts as online");
    expect(db.CountDevicePresence("device-1") == 1,
           "offline device remains in presence count");
    expect(db.CountDevicePresence("", "online") == 2,
           "presence count supports online filter");
    expect(db.CountDevicePresence("", "offline") == 1,
           "presence count supports offline filter");
    auto sorted_presence =
        db.ListDevicePresence(10, 0, "", "all", "device_id", "asc");
    expect(!sorted_presence.empty() &&
               sorted_presence[0].device_id == "device-1",
           "presence list supports device id sort");
    auto offline_devices = db.ListDevicePresence(10, 0, "device-1");
    expect(offline_devices.size() == 1 && !offline_devices[0].online,
           "presence list includes offline device");
    expect(offline_devices[0].updated_at > 0,
           "offline device keeps last online timestamp");
    expect(offline_devices[0].online_since == 0,
           "offline device clears current online start");
    expect(offline_devices[0].online_duration_seconds == 0,
           "offline device current online duration is zero");
    expect(offline_devices[0].total_online_seconds >= 1,
           "offline device keeps accumulated online duration");
    expect(db.StartRemoteControlSession("tx-1", "device-2", "device-1"),
           "remote control session starts");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    expect(db.CountDevicePresence("", "active") == 2,
           "presence count supports active remote filter");
    auto active_devices =
        db.ListDevicePresence(10, 0, "", "active", "device_id", "asc");
    expect(active_devices.size() == 2,
           "presence list supports active remote filter");
    expect(active_devices[0].device_id == "device-1" &&
               active_devices[0].active_control_count == 1,
           "presence list reports active control count");
    expect(active_devices[1].device_id == "device-2" &&
               active_devices[1].active_controlled_count == 1,
           "presence list reports active controlled count");
    db.SetDeviceOnline("device-4", true);
    expect(db.StartRemoteControlSession("tx-clone", "device-2", "C-device-4"),
           "clone remote control session starts");
    auto clone_control =
        db.ListDevicePresence(10, 0, "device-4", "active");
    expect(clone_control.size() == 1 &&
               clone_control[0].active_control_count == 1,
           "presence list maps clone guest control count to base device");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    expect(db.EndRemoteControlSession("tx-clone", "device-2", "C-device-4"),
           "clone remote control session ends");
    auto clone_persisted = db.ListDevicePresence(10, 0, "device-4");
    expect(!clone_persisted.empty() &&
               clone_persisted[0].total_control_seconds >= 1,
           "clone guest control duration persists on base device");
    auto guest_control = db.ListDevicePresence(10, 0, "device-1");
    auto host_controlled = db.ListDevicePresence(10, 0, "device-2");
    expect(!guest_control.empty() &&
               guest_control[0].total_control_seconds >= 1,
           "guest accumulates active control duration");
    expect(!host_controlled.empty() &&
               host_controlled[0].total_controlled_seconds >= 1,
           "host accumulates active controlled duration");
    auto active_remote_stats = db.GetOnlineDurationStats();
    expect(active_remote_stats.total_control_seconds >= 1,
           "stats include active control duration");
    expect(active_remote_stats.total_controlled_seconds >= 1,
           "stats include active controlled duration");
    expect(db.EndRemoteControlSession("tx-1", "device-2", "device-1"),
           "remote control session ends");
    auto remote_stats = db.GetOnlineDurationStats();
    expect(remote_stats.total_control_seconds >= 1,
           "stats persist total control duration");
    expect(remote_stats.total_controlled_seconds >= 1,
           "stats persist total controlled duration");
    expect(db.StartRemoteControlSession("tx-stale", "device-2", "device-3"),
           "stale remote control session starts before restart");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto restart_checkpoint_stats = db.GetOnlineDurationStats();
    total_online_before_restart =
        restart_checkpoint_stats.total_online_seconds;
    total_control_before_restart =
        restart_checkpoint_stats.total_control_seconds;
    total_controlled_before_restart =
        restart_checkpoint_stats.total_controlled_seconds;
    expect(db.RecordRuntimeHeartbeat(),
           "database records runtime heartbeat before restart");
  }
  {
    DeviceDBManager restarted_db(db_path.string());
    expect(restarted_db.CountOnlineDevices() == 0,
           "database clears stale online devices on restart");
    auto restarted_stats = restarted_db.GetOnlineDurationStats();
    expect(restarted_stats.current_online_seconds == 0,
           "database clears stale current online duration on restart");
    expect(restarted_stats.total_online_seconds >=
               total_online_before_restart,
           "database preserves total online duration across restart");
    expect(restarted_stats.total_control_seconds >=
               total_control_before_restart,
           "database preserves total control duration across restart");
    expect(restarted_stats.total_controlled_seconds >=
               total_controlled_before_restart,
           "database preserves total controlled duration across restart");
    auto restarted_devices =
        restarted_db.ListDevicePresence(10, 0, "device-2");
    expect(!restarted_devices.empty() && !restarted_devices[0].online,
           "restart marks previously online device offline");
    expect(!restarted_devices.empty() &&
               restarted_devices[0].online_since == 0,
           "restart clears stale online_since");
  }
  std::filesystem::remove(db_path);

  return failures == 0 ? 0 : 1;
}
