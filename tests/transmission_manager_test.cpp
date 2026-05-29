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

  return failures == 0 ? 0 : 1;
}
