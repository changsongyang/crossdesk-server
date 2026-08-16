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

```bash
# Run from the repository root. This image contains CrossDesk Server only.
sudo docker build -f docker/dockerfile -t image-name .
```

## Run Services

### Use Published Images (Recommended for Servers)

Download `compose.yaml` and `env.example` from the GitHub Release. The released `env.example` pins `CROSSDESK_IMAGE` to that release tag:

```bash
cp env.example .env

# Set the public/private IPs, ports, and TURN shared secret.
vi .env

sudo docker compose pull
sudo docker compose up -d
sudo docker compose ps
```

When using `.env.example` from the repository, change `CROSSDESK_IMAGE` from `latest` to the exact release tag intended for production.

### Use the CI Test Image

After a successful CI build of the default branch, the multi-architecture test image is published as `crossdesk/crossdesk-server:test`. This mutable tag tracks the latest successful build and is intended only for test environments, not production.

Set the following value in `.env` on the test server:

```dotenv
CROSSDESK_IMAGE=crossdesk/crossdesk-server:test
```

Pull the image before every deployment. `--no-build` ensures that the server uses the image published by CI:

```bash
sudo docker compose pull
sudo docker compose up -d --no-build
sudo docker compose ps
```

### Build from Local Source

Build the binary as described above, place it at `dist/crossdesk_server`, and then run:

```bash
cp .env.example .env
vi .env
sudo docker compose up -d --build
```

Compose starts two independent containers:

- `crossdesk_server` runs CrossDesk Server and generates the shared certificates on first startup.
- `crossdesk_coturn` runs the pinned official Coturn image after the certificates are ready, and can be upgraded, restarted, and resource-limited independently.

### Container Management Commands

Run the following commands from the directory containing `compose.yaml`. Compose reads `.env` from the same directory by default:

```bash
# Start all containers.
sudo docker compose up -d

# Show container status.
sudo docker compose ps

# Stop all containers while keeping them available for a later start.
sudo docker compose stop

# Restart all containers.
sudo docker compose restart

# Pull the images selected in .env and recreate containers whose images changed.
sudo docker compose pull
sudo docker compose up -d
```

`docker compose restart` does not apply changes from `.env` or `compose.yaml`. Run `docker compose up -d` after changing configuration or image versions.

If the files are stored elsewhere, specify their paths explicitly. For example:

```bash
sudo docker compose \
  -f /path/to/compose.yaml \
  --env-file /root/workspace/server_config/.env \
  up -d
```

**Parameters**

- **EXTERNAL_IP**: The server’s public IP. This corresponds to **Server Address** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **INTERNAL_IP**: The server’s internal IP.
- **CROSSDESK_SERVER_PORT**: The port used by the self-hosted service. This corresponds to **Server Port** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **COTURN_PORT**: The port used by the COTURN service. This corresponds to **Relay Service Port** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **MIN_PORT / MAX_PORT**: The port range used by the COTURN service. Example: `MIN_PORT=50000`, `MAX_PORT=60000`. Adjust the range depending on the number of clients.
- **COTURN_PUBLIC_HOST**: Optional public TURN hostname or IP; defaults to `EXTERNAL_IP` when empty.
- **COTURN_AUTH_SECRET**: Signing secret shared only by CrossDesk Server and Coturn. Generate it with `openssl rand -hex 32`; never send it to clients.
- **COTURN_CREDENTIAL_TTL_SECONDS**: Lifetime of temporary TURN credentials issued by the signaling service, from 60 to 86400 seconds; defaults to 3600.
- **COTURN_STATELESS_NONCE_SECRET**: Generate it with `openssl rand -hex 32` and keep it stable so Coturn restarts do not force every client through an extra 438 re-authentication round trip.
- **COTURN_LOG_LEVEL**: Defaults to `warning` to avoid per-request debug logging.
- **COTURN_MEMORY_LIMIT**: Coturn container memory limit; defaults to `512m` and can be adjusted for expected concurrency.
- **CROSSDESK_DATA_DIR / CROSSDESK_LOG_DIR**: Host directories for persistent data, certificates, and CrossDesk logs.

The signaling service issues fresh TURN REST API usernames and passwords after client login and before each new ICE connection is created. Clients no longer embed a fixed Coturn account. `COTURN_AUTH_SECRET` and `COTURN_STATELESS_NONCE_SECRET` serve different purposes and should be generated independently.

### Logging Mode (Default: Hybrid)

Compose uses a hybrid logging model by default:

- CrossDesk business logs continue to be written under `/var/log/crossdesk/` and are persisted to the host through `CROSSDESK_LOG_DIR` for backup, download, and application troubleshooting.
- Coturn runtime logs go only to stdout. Docker rotates them with `max-size=50m` and `max-file=3`, retaining approximately 150 MB at most so abnormal public traffic cannot grow the log indefinitely.
- Coturn no longer creates `/var/log/crossdesk/turn.log`, avoiding duplicate copies in both a log file and Docker's container log.

When migrating from the previous `docker run` deployment and retaining `/root/workspace/server_config`, set the following in `.env`:

```dotenv
CROSSDESK_DATA_DIR=/root/workspace/server_config
CROSSDESK_LOG_DIR=/root/workspace/server_config/logs
```

This reuses the existing `certs`, `db`, and CrossDesk log directories. Coturn's high-volume runtime log remains bounded and rotated by Docker instead of being written there.

View or export Coturn logs with:

```bash
# Follow the most recent 200 lines.
sudo docker logs -f --tail 200 crossdesk_coturn

# Show the last hour.
sudo docker logs --since 1h crossdesk_coturn

# Export an incident to the persistent log directory when needed.
sudo docker logs --since 1h crossdesk_coturn \
  > /root/workspace/server_config/logs/coturn-export.log 2>&1
```

Docker removes the Coturn container log when the container is deleted. Export incident logs before removal when long-term retention is required, or forward them to a centralized logging system.

**Notes**

- **The server must open the following ports: COTURN_PORT/udp, COTURN_PORT/tcp, MIN_PORT–MAX_PORT/udp, and CROSSDESK_SERVER_PORT/tcp.**
- Coturn uses `EXTERNAL_IP/INTERNAL_IP` mapping for cloud servers behind public NAT.
- Docker stdout/stderr logs for both containers are limited to three 50 MB files.
- Certificate files will be automatically generated on first startup and persisted to the host at `/var/lib/crossdesk/certs`.
- The database file will be automatically created and stored at `/var/lib/crossdesk/db/crossdesk-server.db`.
- CrossDesk business logs are persisted under `/var/log/crossdesk/`; view rotating Coturn logs with `docker logs crossdesk_coturn`.

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

Compose example: set the following values in `.env`:

```dotenv
ADMIN_USERNAME=admin
ADMIN_PASSWORD=change-this-password
```

Then apply the configuration:

```bash
sudo docker compose up -d
```

After startup, open:

```text
https://your-domain.example.com:9090/admin
```

The dashboard shows online devices, online web clients, active remote-control sessions, accumulated online duration, accumulated control duration, and accumulated controlled duration, and supports disconnecting a selected remote-control session. It includes a China user-distribution map where darker provinces have more users, with users outside China summarized separately. The client-presence table defaults to online PC clients and provides online, active remote-control, offline, and all status filters plus a PC/Web client-kind selector; it also supports search, pagination, sorting, expandable details, and the geographic location resolved from the current online connection IP. Client IP and geolocation are kept only as in-memory online state, not persisted as device profile data in the database. The current online duration refreshes live while a client is online, and the row stays visible with the last-online timestamp after the client goes offline.

Public IP geolocation does not call an external service by default. Private, loopback, and Docker private-network addresses are shown as `Private network`. Set `CROSSDESK_GEOIP_LOOKUP=1` and provide an IP2Location API key with `CROSSDESK_GEOIP_KEY` to enable public GeoIP lookups. The default request is `https://api.ip2location.io/?key={key}&ip={ip}`. The resolver reads only `country_name`, `country_code`, and `region_name`, and builds location text such as `California, United States of America`; city fields are no longer read or displayed. Geo distribution counts a client as unknown only when both country and a recognizable province are missing. If you use the IP2Location.io Free plan or keyless API, attribution is required; the admin geo-distribution area displays `CrossDesk uses IP2Location.io IP geolocation web service.` with a link to `https://www.ip2location.io`. Configure the endpoint with `CROSSDESK_GEOIP_SCHEME`, `CROSSDESK_GEOIP_HOST`, `CROSSDESK_GEOIP_PORT`, and `CROSSDESK_GEOIP_PATH`; `{ip}` and `{key}` in the path are replaced before the request. The lookup timeout can be adjusted with `CROSSDESK_GEOIP_TIMEOUT_MS`; the default is 1200ms. Successful IP results are cached; failed results are not stored as device locations. A background IP queue deduplicates lookups by IP and re-enqueues failures with backoff. If no online device is using an IP when a retry runs, the job is dropped. Backoff starts at 60000ms and doubles up to 1800000ms by default. Tune this with `CROSSDESK_GEOIP_FAILURE_TTL_MS` and `CROSSDESK_GEOIP_FAILURE_MAX_TTL_MS`.

The admin frontend assets live in `src/admin/web` and are copied to `/crossdesk-server/admin` in the container. Set `CROSSDESK_ADMIN_WEB_DIR` to serve a custom frontend directory. The China map boundary asset `china-provinces.json` is generated from the ISC-licensed `china-map-geojson@1.0.4` province-level GeoJSON data.
