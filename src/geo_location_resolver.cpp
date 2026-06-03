#include "geo_location_resolver.h"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>

#include "log.h"

namespace {

constexpr char kGeoHost[] = "api.ip2location.io";
constexpr char kGeoPort[] = "443";
constexpr char kGeoScheme[] = "https";
constexpr char kGeoPathTemplate[] = "/?key={key}&ip={ip}";
constexpr int kDefaultTimeoutMs = 1200;

std::string GetEnvString(const char* name, const char* fallback) {
  const char* raw = std::getenv(name);
  return raw && *raw ? std::string(raw) : std::string(fallback);
}

std::string ToLower(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

bool PublicLookupEnabled() {
  const char* raw = std::getenv("CROSSDESK_GEOIP_LOOKUP");
  if (!raw) {
    return false;
  }
  std::string value = ToLower(raw);
  return value == "1" || value == "true" || value == "on" || value == "yes";
}

int LookupTimeoutMs() {
  const char* raw = std::getenv("CROSSDESK_GEOIP_TIMEOUT_MS");
  if (!raw) {
    return kDefaultTimeoutMs;
  }
  char* end = nullptr;
  long value = std::strtol(raw, &end, 10);
  if (end == raw || value < 200 || value > 5000) {
    return kDefaultTimeoutMs;
  }
  return static_cast<int>(value);
}

std::string UrlEncode(const std::string& value) {
  std::ostringstream encoded;
  encoded << std::uppercase << std::hex;
  for (unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~') {
      encoded << ch;
    } else {
      encoded << '%' << std::setw(2) << std::setfill('0')
              << static_cast<int>(ch);
    }
  }
  return encoded.str();
}

void ReplaceAll(std::string* value, const std::string& marker,
                const std::string& replacement) {
  size_t pos = 0;
  while ((pos = value->find(marker, pos)) != std::string::npos) {
    value->replace(pos, marker.size(), replacement);
    pos += replacement.size();
  }
}

std::string LookupPath(const std::string& ip) {
  std::string path =
      GetEnvString("CROSSDESK_GEOIP_PATH", kGeoPathTemplate);
  std::string encoded_ip = UrlEncode(ip);
  std::string key = GetEnvString("CROSSDESK_GEOIP_KEY", "");
  if (path.find("{key}") != std::string::npos && key.empty()) {
    LOG_WARN("GeoIP lookup enabled but CROSSDESK_GEOIP_KEY is empty");
    return "";
  }
  ReplaceAll(&path, "{key}", UrlEncode(key));
  if (path.find("{ip}") == std::string::npos) {
    return path + encoded_ip;
  }
  ReplaceAll(&path, "{ip}", encoded_ip);
  return path;
}

bool IsPrivateOrLocalIp(const std::string& ip) {
  asio::error_code ec;
  asio::ip::address address = asio::ip::make_address(ip, ec);
  if (ec) {
    return true;
  }

  if (address.is_loopback() || address.is_unspecified() ||
      address.is_multicast()) {
    return true;
  }

  if (address.is_v4()) {
    uint32_t value = address.to_v4().to_uint();
    return (value & 0xff000000U) == 0x0a000000U ||
           (value & 0xfff00000U) == 0xac100000U ||
           (value & 0xffff0000U) == 0xc0a80000U ||
           (value & 0xffff0000U) == 0xa9fe0000U;
  }

  const auto v6 = address.to_v6();
  if (v6.is_link_local()) {
    return true;
  }
  const auto bytes = v6.to_bytes();
  return (bytes[0] & 0xfe) == 0xfc;
}

std::string JsonString(const nlohmann::json& body, const char* key) {
  auto it = body.find(key);
  return it != body.end() && it->is_string() ? it->get<std::string>() : "";
}

std::string FirstJsonString(
    const nlohmann::json& body, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    std::string value = JsonString(body, key);
    if (!value.empty()) {
      return value;
    }
  }
  return "";
}

std::string ToLowerAscii(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

bool SameText(const std::string& lhs, const std::string& rhs) {
  return !lhs.empty() && ToLowerAscii(lhs) == ToLowerAscii(rhs);
}

void AppendLocationPart(std::string* location, const std::string& part) {
  if (!location || part.empty()) {
    return;
  }
  if (!location->empty()) {
    *location += ", ";
  }
  *location += part;
}

std::string BuildLocation(const ClientNetworkInfo& info) {
  std::string location;
  AppendLocationPart(&location, info.city);
  if (!SameText(info.region, info.city)) {
    AppendLocationPart(&location, info.region);
  }
  if (!SameText(info.country, info.city) &&
      !SameText(info.country, info.region)) {
    AppendLocationPart(&location, info.country);
  }
  return location;
}

std::string BuildHttpRequest(const std::string& host, const std::string& port,
                             const std::string& path,
                             const std::string& scheme) {
  std::string host_header = host;
  if ((scheme == "https" && port != "443") ||
      (scheme != "https" && port != "80")) {
    host_header += ":" + port;
  }

  std::ostringstream request;
  request << "GET " << path << " HTTP/1.0\r\n"
          << "Host: " << host_header << "\r\n"
          << "Accept: application/json\r\n"
          << "Connection: close\r\n"
          << "User-Agent: CrossDesk-Server\r\n\r\n";
  return request.str();
}

std::string FetchHttpResponse(const std::string& host, const std::string& port,
                              const std::string& request, int timeout_ms) {
  asio::ip::tcp::iostream stream;
  stream.expires_after(std::chrono::milliseconds(timeout_ms));
  stream.connect(host, port);
  if (!stream) {
    LOG_WARN("GeoIP lookup connect failed to [{}:{}]: {}", host, port,
             stream.error().message());
    return "";
  }

  stream << request;
  stream.flush();

  std::ostringstream response;
  response << stream.rdbuf();
  return response.str();
}

std::string FetchHttpsResponse(const std::string& host, const std::string& port,
                               const std::string& request, int timeout_ms) {
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ssl::context ctx(asio::ssl::context::tls_client);
  ctx.set_verify_mode(asio::ssl::verify_none);
  asio::ssl::stream<asio::ip::tcp::socket> stream(io, ctx);
  SSL_set_tlsext_host_name(stream.native_handle(), host.c_str());

  asio::steady_timer timer(io);
  asio::streambuf response;
  std::shared_ptr<std::string> request_body =
      std::make_shared<std::string>(request);
  asio::error_code final_ec;
  bool done = false;

  auto finish = [&](const asio::error_code& ec) {
    if (done) {
      return;
    }
    done = true;
    final_ec = ec;
    timer.cancel();
    resolver.cancel();
    asio::error_code ignored;
    stream.lowest_layer().close(ignored);
  };

  timer.expires_after(std::chrono::milliseconds(timeout_ms));
  timer.async_wait([&](const asio::error_code& ec) {
    if (!ec) {
      finish(asio::error::timed_out);
    }
  });

  resolver.async_resolve(
      host, port,
      [&](const asio::error_code& ec,
          const asio::ip::tcp::resolver::results_type& results) {
        if (ec) {
          finish(ec);
          return;
        }
        asio::async_connect(
            stream.lowest_layer(), results,
            [&](const asio::error_code& ec,
                const asio::ip::tcp::endpoint&) {
              if (ec) {
                finish(ec);
                return;
              }
              stream.async_handshake(
                  asio::ssl::stream_base::client,
                  [&](const asio::error_code& ec) {
                    if (ec) {
                      finish(ec);
                      return;
                    }
                    asio::async_write(
                        stream, asio::buffer(*request_body),
                        [&](const asio::error_code& ec, std::size_t) {
                          if (ec) {
                            finish(ec);
                            return;
                          }
                          asio::async_read(
                              stream, response, asio::transfer_all(),
                              [&](const asio::error_code& ec, std::size_t) {
                                if (ec != asio::error::eof &&
                                    ec != asio::ssl::error::stream_truncated) {
                                  finish(ec);
                                  return;
                                }
                                finish({});
                              });
                        });
                  });
            });
      });

  io.run();
  if (final_ec) {
    LOG_WARN("GeoIP HTTPS lookup failed for [{}:{}]: {}", host, port,
             final_ec.message());
    return "";
  }

  std::ostringstream response_text;
  response_text << &response;
  return response_text.str();
}

std::string HttpBody(const std::string& response, const std::string& ip) {
  size_t status_end = response.find('\n');
  if (status_end == std::string::npos) {
    LOG_WARN("GeoIP lookup returned an empty response for [{}]", ip);
    return "";
  }

  std::string status_line = response.substr(0, status_end);
  if (status_line.find(" 200 ") == std::string::npos) {
    LOG_WARN("GeoIP lookup returned [{}] for [{}]", status_line, ip);
    return "";
  }

  size_t body_pos = response.find("\r\n\r\n");
  size_t separator_size = 4;
  if (body_pos == std::string::npos) {
    body_pos = response.find("\n\n");
    separator_size = 2;
  }
  if (body_pos == std::string::npos) {
    return "";
  }
  return response.substr(body_pos + separator_size);
}

ClientNetworkInfo ParseGeoJson(const std::string& body_text,
                               const std::string& ip) {
  ClientNetworkInfo info;
  info.client_ip = ip;
  try {
    nlohmann::json body = nlohmann::json::parse(body_text);
    info.country = FirstJsonString(
        body, {"country_name", "country", "country_code"});
    info.region = FirstJsonString(
        body, {"region_name", "region", "province", "state"});
    info.city = FirstJsonString(body, {"city_name", "city"});
    info.location = JsonString(body, "location");
    if (info.location.empty()) {
      info.location = BuildLocation(info);
    }
  } catch (const std::exception& e) {
    LOG_WARN("GeoIP lookup parse failed for [{}]: {}", ip, e.what());
  }
  return info;
}

ClientNetworkInfo ResolvePublicIp(const std::string& ip) {
  ClientNetworkInfo info;
  info.client_ip = ip;

  std::string host = GetEnvString("CROSSDESK_GEOIP_HOST", kGeoHost);
  std::string port = GetEnvString("CROSSDESK_GEOIP_PORT", kGeoPort);
  std::string scheme = ToLower(GetEnvString("CROSSDESK_GEOIP_SCHEME", kGeoScheme));
  if (host.empty() || port.empty()) {
    return info;
  }

  std::string path = LookupPath(ip);
  if (path.empty()) {
    return info;
  }

  int timeout_ms = LookupTimeoutMs();
  std::string request = BuildHttpRequest(host, port, path, scheme);
  std::string response =
      scheme == "https" ? FetchHttpsResponse(host, port, request, timeout_ms)
                        : FetchHttpResponse(host, port, request, timeout_ms);
  if (response.empty()) {
    return info;
  }

  std::string body = HttpBody(response, ip);
  return body.empty() ? info : ParseGeoJson(body, ip);
}

}  // namespace

ClientNetworkInfo GeoLocationResolver::Resolve(const std::string& ip) {
  ClientNetworkInfo info;
  info.client_ip = ip;
  if (ip.empty()) {
    return info;
  }

  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(ip);
    if (it != cache_.end()) {
      return it->second;
    }
  }

  if (IsPrivateOrLocalIp(ip)) {
    info.location = "Private network";
  } else if (PublicLookupEnabled()) {
    info = ResolvePublicIp(ip);
  }

  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[ip] = info;
  }
  return info;
}
