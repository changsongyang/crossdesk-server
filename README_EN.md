# CrossDesk Server

[中文](README_CN.md) / [English](README.md)

Server designed for [CrossDesk](https://github.com/kunkundi/crossdesk) , supporting WSS-encrypted connections and using SQLite3 to store user information.

[License: LGPL-3.0](LICENSE) | [Platform: Windows | Linux | macOS]

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

### Basic Startup (Data stored in container)

Example startup command:
```bash
docker run -d \
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
  crossdesk/crossdesk-server:v1.1.2
```

**Notes**:
- Certificate files will be automatically generated on first startup in `/var/lib/crossdesk/certs` (inside container)
- Database file will be automatically created at `/var/lib/crossdesk/db/crossdesk-server.db` (inside container)
- Log files will be automatically created in `/var/log/crossdesk/` (inside container)
- **Note**: Data will be lost if container is removed without volume mounts
- `-v /var/lib/crossdesk:/var/lib/crossdesk`: Persist database and certificate files to host
- `-v /var/log/crossdesk:/var/log/crossdesk`: Persist log files to host
- Data will remain on host even if container is removed
- **Auto directory creation**: If these directories don't exist on host, Docker will create them automatically, and the container code will also create subdirectories
- **Permission note (Important)**: If Docker auto-created directories have insufficient permissions (owned by root), the container user cannot write, which will cause:
  - Certificate generation failure, container startup script will exit with error
  - Database directory creation failure, program will throw exception and crash
  - Log directory creation failure, log files cannot be written (but program may continue running)
  
  Solution: Set permissions manually before starting container:
  ```bash
  sudo mkdir -p /var/lib/crossdesk /var/log/crossdesk
  sudo chown -R $(id -u):$(id -g) /var/lib/crossdesk /var/log/crossdesk
  ```