#include "geo_location_resolver.h"

#include <asio.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr char kTestIp[] = "8.8.8.8";

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

void ReadRequestAndWriteResponse(asio::ip::tcp::socket* socket,
                                 const std::string& body,
                                 std::string* request_line) {
  asio::streambuf request;
  asio::error_code ec;
  asio::read_until(*socket, request, "\r\n\r\n", ec);
  std::istream request_stream(&request);
  std::getline(request_stream, *request_line);
  if (!request_line->empty() && request_line->back() == '\r') {
    request_line->pop_back();
  }

  std::string response =
      "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
      std::to_string(body.size()) + "\r\n\r\n" + body;
  asio::write(*socket, asio::buffer(response), ec);
}

void ServeOneResponse(asio::ip::tcp::acceptor* acceptor,
                      const std::string& body,
                      std::string* request_line) {
  asio::ip::tcp::socket socket(acceptor->get_executor());
  acceptor->accept(socket);
  ReadRequestAndWriteResponse(&socket, body, request_line);
}

bool TryServeOneResponse(asio::ip::tcp::acceptor* acceptor,
                         const std::string& body,
                         std::string* request_line,
                         std::chrono::milliseconds timeout) {
  asio::error_code ec;
  acceptor->non_blocking(true, ec);
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    asio::ip::tcp::socket socket(acceptor->get_executor());
    acceptor->accept(socket, ec);
    if (!ec) {
      ReadRequestAndWriteResponse(&socket, body, request_line);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

struct ResolveResult {
  ClientNetworkInfo info;
  std::string request_line;
  bool accepted = true;
  bool retryable = false;
};

void ConfigureGeoEnv(unsigned short port, const std::string& path = "") {
  SetEnv("CROSSDESK_GEOIP_LOOKUP", "1");
  SetEnv("CROSSDESK_GEOIP_KEY", "unit-key");
  SetEnv("CROSSDESK_GEOIP_SCHEME", "http");
  SetEnv("CROSSDESK_GEOIP_HOST", "127.0.0.1");
  SetEnv("CROSSDESK_GEOIP_PORT", std::to_string(port));
  if (path.empty()) {
    UnsetEnv("CROSSDESK_GEOIP_PATH");
  } else {
    SetEnv("CROSSDESK_GEOIP_PATH", path);
  }
}

ResolveResult ResolveWithStub(GeoLocationResolver* resolver,
                              const std::string& body,
                              const std::string& path = "") {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor(
      io, {asio::ip::address_v4::loopback(), 0});
  const auto port = acceptor.local_endpoint().port();

  std::string request_line;
  std::thread server(
      [&]() { ServeOneResponse(&acceptor, body, &request_line); });

  ConfigureGeoEnv(port, path);

  GeoLocationResolveResult resolve = resolver->ResolveWithRetryInfo(kTestIp);
  ResolveResult result{resolve.info, "", true, resolve.retryable};
  server.join();
  result.request_line = request_line;
  return result;
}

ResolveResult ResolveWithStub(const std::string& body,
                              const std::string& path = "") {
  GeoLocationResolver resolver;
  return ResolveWithStub(&resolver, body, path);
}

ResolveResult ResolveWithTimedStub(GeoLocationResolver* resolver,
                                   const std::string& body,
                                   std::chrono::milliseconds timeout) {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor(
      io, {asio::ip::address_v4::loopback(), 0});
  const auto port = acceptor.local_endpoint().port();

  std::string request_line;
  bool accepted = false;
  std::thread server([&]() {
    accepted = TryServeOneResponse(&acceptor, body, &request_line, timeout);
  });

  ConfigureGeoEnv(port);

  GeoLocationResolveResult resolve = resolver->ResolveWithRetryInfo(kTestIp);
  ResolveResult result{resolve.info, "", false, resolve.retryable};
  server.join();
  result.request_line = request_line;
  result.accepted = accepted;
  return result;
}

}  // namespace

int main() {
  int failures = 0;
  auto expect = [&failures](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      ++failures;
    }
  };

  ResolveResult ip2location = ResolveWithStub(
      R"({"ip":"8.8.8.8","country_code":"US","country_name":"United States of America","region_name":"California","city_name":"Mountain View","latitude":37.38605,"longitude":-122.08385,"zip_code":"94035","time_zone":"-07:00","asn":"15169","as":"Google LLC","is_proxy":false})");
  expect(ip2location.request_line ==
             "GET /?key=unit-key&ip=8.8.8.8 HTTP/1.0",
         "resolver uses default IP2Location request path");
  expect(ip2location.info.client_ip == "8.8.8.8",
         "resolver keeps queried client ip");
  expect(ip2location.info.country == "United States of America",
         "resolver parses IP2Location country name");
  expect(ip2location.info.region == "California",
         "resolver parses IP2Location region name");
  expect(ip2location.info.city.empty(),
         "resolver does not store IP2Location city name");
  expect(ip2location.info.location ==
             "California, United States of America",
         "resolver builds IP2Location location from region and country");

  ResolveResult country_only = ResolveWithStub(
      R"({"ip":"8.8.8.8","country_code":"US"})",
      "/custom?api_key={key}&ip={ip}");
  expect(country_only.request_line ==
             "GET /custom?api_key=unit-key&ip=8.8.8.8 HTTP/1.0",
         "resolver replaces key placeholder in custom paths");
  expect(country_only.info.country == "US",
         "resolver falls back to country code");
  expect(country_only.info.location == "US",
         "resolver falls back to country-only location");

  GeoLocationResolver retry_resolver;
  ResolveResult empty_first = ResolveWithStub(&retry_resolver, R"({})");
  expect(empty_first.info.location.empty(),
         "empty GeoIP responses are treated as unresolved");
  expect(empty_first.retryable,
         "empty GeoIP responses are marked retryable");
  ResolveResult retry_success =
      ResolveWithStub(&retry_resolver, R"({"country_code":"US"})");
  expect(retry_success.accepted,
         "empty GeoIP cache entry performs another lookup");
  expect(retry_success.info.location == "US",
         "empty GeoIP cache entry can be replaced by a success");
  ResolveResult cached_success = ResolveWithTimedStub(
      &retry_resolver, R"({"country_code":"CA"})",
      std::chrono::milliseconds(20));
  expect(!cached_success.accepted,
         "successful GeoIP results are cached by ip");
  expect(cached_success.info.location == "US",
         "successful GeoIP cache returns the original location");

  return failures == 0 ? 0 : 1;
}
