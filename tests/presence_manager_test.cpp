#include "presence_manager.h"

int main() {
  PresenceManager presence;
  websocketpp::connection_hdl hdl;
  int failures = 0;

  auto expect = [&failures](bool condition) {
    if (!condition) {
      ++failures;
    }
  };

  expect(presence.GetOnlineDeviceCount() == 0);
  expect(presence.GetOnlineWebClientCount() == 0);

  presence.OnLogin("device-1", "device-1", hdl);
  expect(presence.GetOnlineDeviceCount() == 1);
  expect(presence.GetOnlineWebClientCount() == 0);

  presence.OnLogin("web-1", "web-1", hdl);
  expect(presence.GetOnlineDeviceCount() == 1);
  expect(presence.GetOnlineWebClientCount() == 1);

  presence.OnLogout("web-1");
  expect(presence.GetOnlineDeviceCount() == 1);
  expect(presence.GetOnlineWebClientCount() == 0);

  return failures == 0 ? 0 : 1;
}
