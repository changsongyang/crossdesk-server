#ifndef _GEO_LOCATION_RESOLVER_H_
#define _GEO_LOCATION_RESOLVER_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "device_db_manager.h"

class GeoLocationResolver {
 public:
  ClientNetworkInfo Resolve(const std::string& ip);

 private:
  std::mutex cache_mutex_;
  std::unordered_map<std::string, ClientNetworkInfo> cache_;
};

#endif  // _GEO_LOCATION_RESOLVER_H_
