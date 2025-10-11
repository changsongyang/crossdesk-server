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