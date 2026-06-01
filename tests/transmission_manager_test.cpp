#include "transmission_manager.h"

#include <iostream>
#include <string>

int main() {
  TransmissionManager transmission;
  int failures = 0;

  auto expect = [&failures](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      ++failures;
    }
  };

  expect(transmission.GetActiveConnectionCount() == 0,
         "initial active connection count is zero");

  expect(transmission.BindHostToTransmission("B", "B"),
         "host B binds to transmission B");
  expect(transmission.BindGuestToTransmission("A", "B"),
         "guest A joins transmission B");
  expect(transmission.GetActiveConnectionCount() == 1,
         "guest join increments active connection count once");

  auto snapshots = transmission.GetTransmissionSnapshots();
  expect(snapshots.size() == 1, "snapshot contains one active transmission");
  expect(snapshots[0].transmission_id == "B", "snapshot has transmission id");
  expect(snapshots[0].host_id == "B", "snapshot has host id");
  expect(snapshots[0].guest_ids.size() == 1 &&
             snapshots[0].guest_ids[0] == "A",
         "snapshot has guest id");
  expect(snapshots[0].participant_count == 2,
         "snapshot counts host plus guest");
  expect(snapshots[0].active, "snapshot marks active transmission");

  expect(transmission.DisconnectTransmission("B"),
         "disconnect existing transmission succeeds");
  expect(transmission.GetActiveConnectionCount() == 0,
         "disconnect decrements active connection count");
  expect(transmission.GetTransmissionSnapshots().empty(),
         "disconnect removes transmission snapshot");
  expect(transmission.DisconnectTransmission("B"),
         "disconnect missing transmission is idempotent");

  expect(transmission.BindHostToTransmission("B", "B"),
         "host B rebinds to transmission B");
  expect(transmission.BindGuestToTransmission("A", "B"),
         "guest A rejoins transmission B");
  expect(transmission.GetActiveConnectionCount() == 1,
         "guest rejoin increments active connection count once");

  expect(!transmission.BindGuestToTransmission("B", "B"),
         "host B is not counted as a guest of its own transmission");
  expect(transmission.GetActiveConnectionCount() == 1,
         "host offer path does not increment active connection count");

  expect(transmission.ReleaseGuestFromTransmission("A"),
         "guest A leaves transmission B");
  expect(transmission.GetActiveConnectionCount() == 0,
         "guest leave decrements active connection count to zero");

  expect(transmission.ReleaseTransmission("B"), "host B releases transmission B");
  expect(transmission.GetActiveConnectionCount() == 0,
         "host close does not decrement an already released guest count");

  {
    TransmissionManager paged_transmission;
    paged_transmission.BindHostToTransmission("host-1", "tx-1");
    paged_transmission.BindGuestToTransmission("guest-1", "tx-1");
    paged_transmission.BindHostToTransmission("host-2", "tx-2");
    paged_transmission.BindGuestToTransmission("guest-2", "tx-2");

    size_t filtered_count = 0;
    auto page =
        paged_transmission.GetTransmissionSnapshots(1, 1, "", &filtered_count);
    expect(filtered_count == 2,
           "paged snapshot reports filtered transmission count");
    expect(page.size() == 1, "paged snapshot applies limit and offset");

    auto filtered = paged_transmission.GetTransmissionSnapshots(
        10, 0, "guest-2", &filtered_count);
    expect(filtered_count == 1, "paged snapshot count supports search");
    expect(filtered.size() == 1 && filtered[0].transmission_id == "tx-2",
           "paged snapshot search matches guest id");
  }

  return failures == 0 ? 0 : 1;
}
