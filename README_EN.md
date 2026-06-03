# CrossDesk Server

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)]()
[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![GitHub last commit](https://img.shields.io/github/last-commit/kunkundi/crossdesk-server)](https://github.com/kunkundi/crossdesk-server/commits/web-client)
[![Build Status](https://github.com/kunkundi/crossdesk-server/actions/workflows/build.yml/badge.svg)](https://github.com/kunkundi/crossdesk/actions)  
[![Docker Pulls](https://img.shields.io/docker/pulls/crossdesk/crossdesk-server)](https://hub.docker.com/r/crossdesk/crossdesk-server/tags)
[![GitHub issues](https://img.shields.io/github/issues/kunkundi/crossdesk-server.svg)]()
[![GitHub stars](https://img.shields.io/github/stars/kunkundi/crossdesk-server.svg?style=social)]()
[![GitHub forks](https://img.shields.io/github/forks/kunkundi/crossdesk-server.svg?style=social)]()

[ [中文](README.md) / English ]

Server designed for [CrossDesk](https://github.com/kunkundi/crossdesk) , supporting WSS-encrypted connections and using SQLite3 to store user information.

---

Requirements:
- [xmake](https://xmake.io/#/guide/installation)

Build:
```
git clone https://github.com/kunkundi/crossdesk-server.git

cd crossdesk-server

xmake b crossdesk_server
```

## About Xmake
#### Build Options
```
# Switch build mode
xmake f -m debug/release

# Optional build parameters
-r : Rebuild the target
-v : Show detailed build logs
-y : Automatically confirm prompts

# Example
xmake b -vy crossdesk_server
```
For more information, please refer to the [official Xmake documentation](https://xmake.io/guide/quick-start.html) .

## Build Docker Image
```
cd docker

sudo docker build -t image-name .
```

## Run Container

### Startup Command
```
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=xxx.xxx.xxx.xxx \
  -e INTERNAL_IP=xxx.xxx.xxx.xxx \
  -e CROSSDESK_SERVER_PORT=xxxx \
  -e COTURN_PORT=xxxx \
  -e MIN_PORT=xxxxx \
  -e MAX_PORT=xxxxx \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.3
```

The parameters you need to pay attention to are as follows:

**Parameters**
- **EXTERNAL_IP**: The server’s public IP. This corresponds to **Server Address** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **INTERNAL_IP**: The server’s internal IP.
- **CROSSDESK_SERVER_PORT**: The port used by the self-hosted service. This corresponds to **Server Port** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **COTURN_PORT**: The port used by the COTURN service. This corresponds to **Relay Service Port** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **MIN_PORT / MAX_PORT**: The port range used by the COTURN service. Example: `MIN_PORT=50000`, `MAX_PORT=60000`. Adjust the range depending on the number of clients.
- `-v /var/lib/crossdesk:/var/lib/crossdesk`: Persists database and certificate files on the host machine.
- `-v /var/log/crossdesk:/var/log/crossdesk`: Persists log files on the host machine.

**Example**:
```bash
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=114.114.114.114 \
  -e INTERNAL_IP=10.0.0.1 \
  -e CROSSDESK_SERVER_PORT=9099 \
  -e COTURN_PORT=3478 \
  -e MIN_PORT=50000 \
  -e MAX_PORT=60000 \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.3
```

**Notes**
- **The server must open the following ports: COTURN_PORT/udp, COTURN_PORT/tcp, MIN_PORT–MAX_PORT/udp, and CROSSDESK_SERVER_PORT/tcp.**
- If you don’t mount volumes, all data will be lost when the container is removed.
- Certificate files will be automatically generated on first startup and persisted to the host at `/var/lib/crossdesk/certs`.
- The database file will be automatically created and stored at `/var/lib/crossdesk/db/crossdesk-server.db`.
- Log files will be created and stored at `/var/log/crossdesk/`.

**Permission Notice**
If the directories automatically created by Docker belong to root and have insufficient write permissions, the container user may not be able to write to them. This can cause:
  - Certificate generation failure, leading to startup script errors and container exit.
  - Database directory creation failure, causing the program to throw exceptions and crash.
  - Log directory creation failure, preventing logs from being written (though the program may continue running).

**Solution:** Manually set permissions before starting the container:
```bash
sudo mkdir -p /var/lib/crossdesk /var/log/crossdesk
sudo chown -R $(id -u):$(id -g) /var/lib/crossdesk /var/log/crossdesk
```

## Service Stats Endpoint

After the service starts, you can query runtime stats through the same HTTPS port:

Official CA deployment example:

```bash
curl https://your-domain.example.com:9090/stats
```

Self-signed certificate deployment example:

```bash
curl --cacert /var/lib/crossdesk/certs/api.crossdesk.cn_root.crt \
  https://your-server-ip:9090/stats
```

Notes:
- Official CA certificates are usually trusted by the operating system, so `curl` does not need an extra `--cacert`
- Self-signed certificates require the root certificate `api.crossdesk.cn_root.crt` to be provided explicitly
- The request host must match the domain name or IP address in the server certificate; do not replace it with `127.0.0.1` arbitrarily

The `/api/stats` path is also supported. Example response:

```json
{
  "online_device_count": 12,
  "online_web_client_count": 2,
  "active_connection_count": 3,
  "online_duration_seconds": 86400,
  "total_online_seconds": 259200,
  "total_control_seconds": 3600,
  "total_controlled_seconds": 7200
}
```

- `online_device_count`: Number of online devices, excluding temporary `web-*` clients and `C-*` clone clients
- `online_web_client_count`: Number of online web clients, counting only temporary `web-*` clients
- `active_connection_count`: Number of active in-progress connections, summed from the current guests connected to each host; a guest's own login or join connection is not counted separately
- `online_duration_seconds`: Sum of the current online session duration for online devices, excluding temporary `web-*` clients and `C-*` clone clients
- `total_online_seconds`: Sum of accumulated device online duration, including the current session duration for devices that are still online
- `total_control_seconds`: Sum of accumulated duration where devices are controlling another device, including active remote-control sessions
- `total_controlled_seconds`: Sum of accumulated duration where devices are being controlled, including active remote-control sessions
- The response includes `Access-Control-Allow-Origin: *`, so it can be called directly from browser `fetch`

### Certificate Files
If you use the built-in self-signed certificate flow, you can find the root certificate `api.crossdesk.cn_root.crt` at `/var/lib/crossdesk/certs` on the host machine.
Download it to your client device and select it in the **Certificate File Path** field under the CrossDesk client’s **Self-Hosted Server Settings**.

If you deploy an official CA certificate, you usually do not need to distribute this root certificate separately, because clients and `curl` will validate the certificate with the system trust store.

## Admin Dashboard

The server can serve an embedded admin dashboard at `/admin` on the same HTTPS port.

Enable it by setting both environment variables before startup:

```bash
ADMIN_USERNAME=admin
ADMIN_PASSWORD=change-this-password
```

Docker example:

```bash
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=114.114.114.114 \
  -e INTERNAL_IP=10.0.0.1 \
  -e CROSSDESK_SERVER_PORT=9099 \
  -e COTURN_PORT=3478 \
  -e MIN_PORT=50000 \
  -e MAX_PORT=60000 \
  -e ADMIN_USERNAME=admin \
  -e ADMIN_PASSWORD=change-this-password \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.3
```

After startup, open:

```text
https://your-domain.example.com:9090/admin
```

The dashboard shows online devices, online web clients, active remote-control sessions, accumulated online duration, accumulated control duration, and accumulated controlled duration, and supports disconnecting a selected remote-control session. It includes a China user-distribution map where darker provinces have more users, with users outside China summarized separately. The client-presence table defaults to online devices and provides online, active remote-control, offline, all, and web-client filters; it also supports search, pagination, sorting, expandable details, and the geographic location resolved from the current online connection IP. Client IP and geolocation are kept only as in-memory online state, not persisted as device profile data in the database. The current online duration refreshes live while a client is online, and the row stays visible with the last-online timestamp after the client goes offline.

Public IP geolocation does not call an external service by default. Private, loopback, and Docker private-network addresses are shown as `Private network`. Set `CROSSDESK_GEOIP_LOOKUP=1` and provide an IP2Location API key with `CROSSDESK_GEOIP_KEY` to enable public GeoIP lookups. The default request is `https://api.ip2location.io/?key={key}&ip={ip}`. The resolver reads `country_name`, `country_code`, `region_name`, and `city_name`, and builds location text such as `Mountain View, California, United States of America`. Configure the endpoint with `CROSSDESK_GEOIP_SCHEME`, `CROSSDESK_GEOIP_HOST`, `CROSSDESK_GEOIP_PORT`, and `CROSSDESK_GEOIP_PATH`; `{ip}` and `{key}` in the path are replaced before the request. The lookup timeout can be adjusted with `CROSSDESK_GEOIP_TIMEOUT_MS`; the default is 1200ms. Successful IP results are cached; failed results are not stored as device locations. A background IP queue deduplicates lookups by IP and re-enqueues failures with backoff. If no online device is using an IP when a retry runs, the job is dropped. Backoff starts at 60000ms and doubles up to 1800000ms by default. Tune this with `CROSSDESK_GEOIP_FAILURE_TTL_MS` and `CROSSDESK_GEOIP_FAILURE_MAX_TTL_MS`.

The admin frontend assets live in `src/admin/web` and are copied to `/crossdesk-server/admin` in the container. Set `CROSSDESK_ADMIN_WEB_DIR` to serve a custom frontend directory. The China map boundary asset `china-provinces.json` is generated from the ISC-licensed `china-map-geojson@1.0.4` province-level GeoJSON data.
