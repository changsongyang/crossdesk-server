#include "geo_location_resolver.h"

#include <asio.hpp>
#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#include "log.h"

namespace {

constexpr char kGeoHost[] = "ip-api.com";
constexpr char kGeoPort[] = "80";
constexpr int kDefaultTimeoutMs = 1200;

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
    return true;
  }
  std::string value = ToLower(raw);
  return value != "0" && value != "false" && value != "off" &&
         value != "no";
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

void AppendLocationPart(std::vector<std::string>* parts,
                        const std::string& value) {
  if (value.empty()) {
    return;
  }
  for (const auto& part : *parts) {
    if (part == value) {
      return;
    }
  }
  parts->push_back(value);
}

std::string BuildLocation(const std::string& city, const std::string& region,
                          const std::string& country) {
  std::vector<std::string> parts;
  AppendLocationPart(&parts, city);
  AppendLocationPart(&parts, region);
  AppendLocationPart(&parts, country);

  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += parts[i];
  }
  return result;
}

ClientNetworkInfo ResolvePublicIp(const std::string& ip) {
  ClientNetworkInfo info;
  info.client_ip = ip;

  asio::ip::tcp::iostream stream;
  stream.expires_after(std::chrono::milliseconds(LookupTimeoutMs()));
  stream.connect(kGeoHost, kGeoPort);
  if (!stream) {
    LOG_WARN("GeoIP lookup connect failed for [{}]: {}", ip,
             stream.error().message());
    return info;
  }

  std::string path = "/json/" + UrlEncode(ip) +
                     "?fields=status,country,regionName,city";
  stream << "GET " << path << " HTTP/1.0\r\n"
         << "Host: " << kGeoHost << "\r\n"
         << "Accept: application/json\r\n"
         << "Connection: close\r\n"
         << "User-Agent: CrossDesk-Server\r\n\r\n";
  stream.flush();

  std::string status_line;
  std::getline(stream, status_line);
  if (status_line.find(" 200 ") == std::string::npos) {
    LOG_WARN("GeoIP lookup returned [{}] for [{}]", status_line, ip);
    return info;
  }

  std::string header;
  while (std::getline(stream, header) && header != "\r") {
  }

  std::ostringstream body_stream;
  body_stream << stream.rdbuf();
  try {
    nlohmann::json body = nlohmann::json::parse(body_stream.str());
    if (JsonString(body, "status") != "success") {
      return info;
    }
    info.country = JsonString(body, "country");
    info.region = JsonString(body, "regionName");
    info.city = JsonString(body, "city");
    info.location = BuildLocation(info.city, info.region, info.country);
  } catch (const std::exception& e) {
    LOG_WARN("GeoIP lookup parse failed for [{}]: {}", ip, e.what());
  }

  return info;
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
