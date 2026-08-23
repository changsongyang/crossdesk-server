# CrossDesk Server

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)]()
[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![GitHub last commit](https://img.shields.io/github/last-commit/kunkundi/crossdesk-server)](https://github.com/kunkundi/crossdesk-server/commits/web-client)
[![Build Status](https://github.com/kunkundi/crossdesk-server/actions/workflows/build.yml/badge.svg)](https://github.com/kunkundi/crossdesk/actions)  
[![Docker Pulls](https://img.shields.io/docker/pulls/crossdesk/crossdesk-server)](https://hub.docker.com/r/crossdesk/crossdesk-server/tags)
[![GitHub issues](https://img.shields.io/github/issues/kunkundi/crossdesk-server.svg)]()
[![GitHub stars](https://img.shields.io/github/stars/kunkundi/crossdesk-server.svg?style=social)]()
[![GitHub forks](https://img.shields.io/github/forks/kunkundi/crossdesk-server.svg?style=social)]()

[ [English](README_EN.md) / 中文 ]

为 [CrossDesk](https://github.com/kunkundi/crossdesk) 设计的服务端，支持WSS加密连接，使用SQLite3存储用户信息。

---

## 如何编译

依赖：
- [xmake](https://xmake.io/#/guide/installation)

编译
```
git clone https://github.com/kunkundi/crossdesk-server.git

cd crossdesk-server

xmake b crossdesk_server
```

## 关于 Xmake
#### 编译选项
```
# 切换编译模式
xmake f -m debug/release

# 可选编译参数
-r ：重新构建目标
-v ：显示详细的构建日志
-y ：自动确认提示

# 示例
xmake b -vy crossdesk_server
```
更多使用方法可参考 [Xmake官方文档](https://xmake.io/guide/quick-start.html) 。

## 构建镜像

```bash
# 在仓库根目录执行；该镜像只包含 CrossDesk Server
sudo docker build -f docker/dockerfile -t image-name .
```

## 运行服务

### 使用已发布镜像（服务器推荐）

从 GitHub Release 下载 `compose.yaml` 和 `env.example`。Release 中的 `env.example` 已将 `CROSSDESK_IMAGE` 固定为对应的发布 tag：

```bash
cp env.example .env

# 编辑公网 IP、内网 IP、端口和 TURN 共享密钥
vi .env

sudo docker compose pull
sudo docker compose up -d
sudo docker compose ps
```

如果使用仓库中的 `.env.example`，建议将 `CROSSDESK_IMAGE` 从 `latest` 改为需要部署的固定版本 tag。

### 使用 CI 测试镜像

默认分支的 CI 构建成功后会发布多架构测试镜像 `crossdesk/crossdesk-server:test`。该标签会随最新成功构建更新，仅用于测试环境，不应在生产环境使用。

在测试服务器的 `.env` 中设置：

```dotenv
CROSSDESK_IMAGE=crossdesk/crossdesk-server:test
```

每次部署前应先拉取最新镜像；`--no-build` 可确保服务器直接使用 CI 发布的镜像：

```bash
sudo docker compose pull
sudo docker compose up -d --no-build
sudo docker compose ps
```

### 从本地源码构建

先按照上文编译并将可执行文件放到 `dist/crossdesk_server`，然后执行：

```bash
cp .env.example .env
vi .env
sudo docker compose up -d --build
```

Compose 会启动两个相互独立的容器：

- `crossdesk_server`：只运行 CrossDesk Server，并负责首次生成共享证书。
- `crossdesk_coturn`：运行官方固定 Coturn 镜像，等待证书生成后启动；可独立升级、重启和限制资源。

### 容器管理命令

以下命令应在 `compose.yaml` 所在目录执行，Compose 默认读取同目录下的 `.env`：

```bash
# 启动全部容器
sudo docker compose up -d

# 查看容器状态
sudo docker compose ps

# 停止全部容器（保留容器，可再次启动）
sudo docker compose stop

# 重启全部容器
sudo docker compose restart

# 拉取 .env 中指定的镜像，并重新创建有更新的容器
sudo docker compose pull
sudo docker compose up -d
```

`docker compose restart` 不会应用 `.env` 或 `compose.yaml` 的修改；配置或镜像版本发生变化后，应执行 `docker compose up -d`。

如果两个文件不在当前目录，请显式指定路径，例如：

```bash
sudo docker compose \
  -f /path/to/compose.yaml \
  --env-file /root/workspace/server_config/.env \
  up -d
```

**参数**

- EXTERNAL_IP：服务器公网 IP , 对应 CrossDesk 客户端**自托管服务器配置**中填写的**服务器地址**
- INTERNAL_IP：服务器内网 IP
- CROSSDESK_SERVER_PORT：自托管服务使用的端口，对应 CrossDesk 客户端**自托管服务器配置**中填写的**服务器端口**
- COTURN_PORT: COTURN 服务使用的端口, 对应 CrossDesk 客户端**自托管服务器配置**中填写的**中继服务端口**
- MIN_PORT/MAX_PORT：COTURN 服务使用的端口范围，例如：MIN_PORT=50000, MAX_PORT=60000，范围可根据客户端数量调整。
- COTURN_PUBLIC_HOST：可选的 TURN 公网域名或 IP；留空时使用 `EXTERNAL_IP`。
- COTURN_AUTH_SECRET：CrossDesk Server 与 Coturn 共享的签名密钥，使用 `openssl rand -hex 32` 生成；不得下发客户端。
- COTURN_CREDENTIAL_TTL_SECONDS：信令服务签发给客户端的 TURN 临时凭据有效期，范围 60–86400 秒，默认 3600 秒。
- COTURN_STATELESS_NONCE_SECRET：使用 `openssl rand -hex 32` 生成并保持不变，避免 Coturn 重启后所有客户端因 nonce 密钥变化触发额外的 438 重认证。
- COTURN_LOG_LEVEL：默认 `warning`，避免按请求打印调试日志。
- COTURN_MEMORY_LIMIT：Coturn 容器内存上限，默认 `512m`，可按并发量调整。
- CROSSDESK_DATA_DIR/CROSSDESK_LOG_DIR：宿主机上的数据、证书和日志目录。

客户端登录成功以及每次创建新的 ICE 连接前，信令服务都会签发新的 TURN REST API 临时用户名和密码。客户端不再内置固定的 Coturn 账户。`COTURN_AUTH_SECRET` 与 `COTURN_STATELESS_NONCE_SECRET` 用途不同，应分别生成。

### 日志模式（默认：混合模式）

Compose 默认采用混合日志模式：

- CrossDesk 业务日志继续写入 `/var/log/crossdesk/`，并通过 `CROSSDESK_LOG_DIR` 持久化到宿主机，便于备份、下载和业务排查。
- Coturn 运行日志只写标准输出，由 Docker 按 `max-size=50m`、`max-file=3` 自动轮转，最多保留约 150 MB，防止公网异常流量造成日志无限增长。
- Coturn 不再创建 `/var/log/crossdesk/turn.log`，避免同一批日志同时写入文件和 Docker 日志。

如果从旧的 `docker run` 部署迁移，并希望继续使用 `/root/workspace/server_config`，可在 `.env` 中设置：

```dotenv
CROSSDESK_DATA_DIR=/root/workspace/server_config
CROSSDESK_LOG_DIR=/root/workspace/server_config/logs
```

这样原有的 `certs`、`db` 和 CrossDesk 日志目录都可以继续使用。Coturn 的高频运行日志仍由 Docker 限量轮转，不再写入该目录。

查看或导出 Coturn 日志：

```bash
# 持续查看最近 200 行
sudo docker logs -f --tail 200 crossdesk_coturn

# 查看最近一小时
sudo docker logs --since 1h crossdesk_coturn

# 需要保留某次事件时，手动导出到持久化日志目录
sudo docker logs --since 1h crossdesk_coturn \
  > /root/workspace/server_config/logs/coturn-export.log 2>&1
```

Coturn 的 Docker 日志会在删除容器时一起删除；需要长期留存的事件日志应在删除容器前导出，或接入集中日志系统。

**注意**：

- **服务器需开放端口：COTURN_PORT/udp，COTURN_PORT/tcp，MIN_PORT-MAX_PORT/udp，CROSSDESK_SERVER_PORT/tcp。**
- Coturn 使用 `EXTERNAL_IP/INTERNAL_IP` 映射，适用于云服务器公网 NAT 场景。
- 两个容器的 Docker stdout/stderr 日志均限制为最多 3 个 50 MB 文件。
- 证书文件会在首次启动时自动生成并持久化到宿主机的 `/var/lib/crossdesk/certs` 路径下
- 数据库文件会自动创建并持久化到宿主机的 `/var/lib/crossdesk/db/crossdesk-server.db` 路径下
- CrossDesk 业务日志持久化到 `/var/log/crossdesk/`；Coturn 日志通过 `docker logs crossdesk_coturn` 查看并自动轮转。

**权限注意**：如果 Docker 自动创建的目录权限不足（属于 root），容器内用户无法写入，会导致：
  - 证书生成失败，容器启动脚本会报错退出
  - 数据库目录创建失败，程序会抛出异常并崩溃
  - 日志目录创建失败，日志文件无法写入（但程序可能继续运行）
  
**解决方案**：在启动容器前手动设置权限：
```bash
sudo mkdir -p /var/lib/crossdesk /var/log/crossdesk
sudo chown -R $(id -u):$(id -g) /var/lib/crossdesk /var/log/crossdesk
```

## 服务状态接口

服务启动后，可通过同一 HTTPS 端口读取运行状态：

官方 CA 部署示例：

```bash
curl https://your-domain.example.com:9090/stats
```

自签证书部署示例：

```bash
curl --cacert /var/lib/crossdesk/certs/api.crossdesk.cn_root.crt \
  https://your-server-ip:9090/stats
```

说明：
- 官方 CA 证书通常已被系统信任，`curl` 无需额外指定 `--cacert`
- 自签证书需要显式指定根证书 `api.crossdesk.cn_root.crt`
- 请求地址必须与服务端证书中的域名或 IP 一致，不能随意替换为 `127.0.0.1`

也支持路径 `/api/stats`，返回示例：

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

- `online_device_count`：当前在线设备数，不包含临时 `web-*` 客户端和 `C-*` 分身客户端
- `online_web_client_count`：当前在线 Web 客户端数，仅统计临时 `web-*` 客户端
- `active_connection_count`：当前处于连接中的会话数，按各 host 当前连接的 guest 数汇总；guest 自身登录或加入产生的连接不另行计数
- `online_duration_seconds`：当前在线设备本次在线时长的总和，不包含临时 `web-*` 客户端和 `C-*` 分身客户端
- `total_online_seconds`：设备累计在线时长的总和，包含当前仍在线设备的本次在线时长
- `total_control_seconds`：设备作为控制端的累计远控时长总和，包含当前仍在进行的远控会话
- `total_controlled_seconds`：设备作为被控端的累计远控时长总和，包含当前仍在进行的远控会话
- 响应已带 `Access-Control-Allow-Origin: *`，可直接被网页端 `fetch` 调用

## 证书文件
如果使用项目自带的自签证书方案，可在宿主机的 `/var/lib/crossdesk/certs` 路径下找到根证书 `api.crossdesk.cn_root.crt`，下载到你的客户端主机，并在客户端的**自托管服务器设置**中选择相应的**证书文件路径**。

如果使用官方 CA 证书，则通常不需要单独分发上述根证书，客户端和 `curl` 会直接使用系统信任链校验证书。

## 后台管理页面

服务端可以在同一个 HTTPS 端口提供内置后台管理页面，访问路径为 `/admin`。

启动前同时设置以下两个环境变量即可启用后台管理：

```bash
ADMIN_USERNAME=admin
ADMIN_PASSWORD=change-this-password
```

Compose 示例：在 `.env` 中设置：

```dotenv
ADMIN_USERNAME=admin
ADMIN_PASSWORD=change-this-password
```

然后应用配置：

```bash
sudo docker compose up -d
```

启动后打开：

```text
https://your-domain.example.com:9090/admin
```

后台页面会显示在线设备数、在线 Web 客户端数、活动远控会话、累计在线时长、累计控制时长和累计被控时长，并支持断开选中的远控会话。页面包含中国用户分布地图，可用颜色深浅查看各省份用户数量，国外用户会单独汇总展示。客户端状态列表默认展示在线 PC 客户端，并提供在线、远控中、离线、全部状态筛选以及 PC/Web 客户端类别选择；列表支持搜索、分页、排序和展开详情，并显示当前在线连接 IP 解析出的地理位置。客户端 IP 和地理位置只作为在线状态保存在内存中，不作为设备资料持久化到数据库；客户端在线时本次在线时长会实时刷新，下线后保留记录并显示最后在线时间点。

IP 地理位置解析默认完全关闭。关闭时仅记录客户端 IP，不启动解析线程、不进行私网分类，也不输出 GeoIP 相关日志。设置 `CROSSDESK_GEOIP_LOOKUP=1` 后，内网、回环和 Docker 私有网段会显示为 `Private network`，公网 IP 则通过 `CROSSDESK_GEOIP_KEY` 配置的 IP2Location API key 查询；默认请求 `https://api.ip2location.io/?key={key}&ip={ip}`。解析只读取 `country_name`、`country_code` 和 `region_name`，并拼出类似 `California, United States of America` 的位置文本，不再读取或展示城市。地域统计只有在国家和可识别省份都缺失时才计入未解析。使用 IP2Location.io 免费计划或无 key API 时需要展示归因，后台地域分布区域会显示 `CrossDesk uses IP2Location.io IP geolocation web service.` 并链接到 `https://www.ip2location.io`。查询端点可通过 `CROSSDESK_GEOIP_SCHEME`、`CROSSDESK_GEOIP_HOST`、`CROSSDESK_GEOIP_PORT` 和 `CROSSDESK_GEOIP_PATH` 配置，其中路径里的 `{ip}` 和 `{key}` 会被替换。查询超时时间可通过 `CROSSDESK_GEOIP_TIMEOUT_MS` 调整，默认 1200ms。成功 IP 结果会缓存；失败结果不作为设备位置缓存，而是由后台 IP 队列按 IP 去重并按退避重新入队。重试时如果已没有在线设备使用该 IP，任务会直接丢弃。退避默认从 60000ms 开始翻倍，最高 1800000ms，可通过 `CROSSDESK_GEOIP_FAILURE_TTL_MS` 和 `CROSSDESK_GEOIP_FAILURE_MAX_TTL_MS` 调整。

后台前端资源位于 `src/admin/web`，容器内默认复制到 `/crossdesk-server/admin`；如需使用自定义前端目录，可设置 `CROSSDESK_ADMIN_WEB_DIR`。中国地图边界数据 `china-provinces.json` 由 ISC 许可的 `china-map-geojson@1.0.4` 省级 GeoJSON 数据生成。
