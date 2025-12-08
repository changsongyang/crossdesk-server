# CrossDesk Server

[English](README.md) / [中文](README_CN.md)

为 [CrossDesk](https://github.com/kunkundi/crossdesk) 设计的服务端，支持WSS加密连接，使用SQLite3存储用户信息。

[License: LGPL-3.0](LICENSE) | [Platform: Windows | Linux | macOS]

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
```
cd docker

sudo docker build -t image-name .
```

## 运行容器

### 基础启动（数据存储在容器内）

启动命令示例：
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

**说明**：
- 证书文件会在首次启动时自动生成在容器内的 `/var/lib/crossdesk/certs`
- 数据库文件会自动创建在容器内的 `/var/lib/crossdesk/db/crossdesk-server.db`
- 日志文件会自动创建在容器内的 `/var/log/crossdesk/`
- **注意**：如果不挂载 volume，容器删除后数据会丢失
- `-v /var/lib/crossdesk:/var/lib/crossdesk`：持久化数据库和证书文件到宿主机
- `-v /var/log/crossdesk:/var/log/crossdesk`：持久化日志文件到宿主机
- 容器删除后，数据仍保留在宿主机上
- **目录自动创建**：如果宿主机没有这些目录，Docker 会自动创建，容器内的代码也会自动创建子目录
- **权限注意（重要）**：如果 Docker 自动创建的目录权限不足（属于 root），容器内用户无法写入，会导致：
  - 证书生成失败，容器启动脚本会报错退出
  - 数据库目录创建失败，程序会抛出异常并崩溃
  - 日志目录创建失败，日志文件无法写入（但程序可能继续运行）
  
  解决方案：在启动容器前手动设置权限：
  ```bash
  sudo mkdir -p /var/lib/crossdesk /var/log/crossdesk
  sudo chown -R $(id -u):$(id -g) /var/lib/crossdesk /var/log/crossdesk
  ```