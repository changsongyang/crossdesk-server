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