# MeshPlatform — Agent 开发指南

## 项目概述
P2P 组网平台,控制面/数据面分离架构:
- `client/` — C++/Qt6 数据面:TUN 虚拟网卡、WebRTC(libdatachannel)直连、音视频(Opus/OpenCV)、文件传输
- `server/` — Java Spring Boot 4.1 控制面:WebSocket 信令中转(注册/hostNum 分配/newPeer 广播/SDP·candidate 按路由转发)、REST 接口

架构原则:**Java 只做"介绍人"——信令面(控制面),C++ 承载全部有效数据——数据面(直连,不过服务器)**。

## 代码规范(Cpp)
以下规范适用于本项目所有新增 C++ 代码:

### 控制流
- 单行 if/for/else:不加花括号 `{}`
- 只用一次的返回值:不要创建中间变量
- if 条件内声明临时变量(仅在 if/else 块内使用时):直接在 `if()` 里用 C++11 init-statement 声明,避免作用域污染.如 `if(auto* p = getWidget()) { p->doSomething(); }` 而不是先在外面声明 `auto* p = getWidget(); if(p) { ... }`

### QHash的keys/values迭代
- 优先使用 range-based for(`for(auto& [k,v] : hash)` 或 `for(auto* w : hash.values())`)
- 尽量不要用 `.begin()/.end()` 迭代器,除非 value 类型是 iterable(会导致 C++17 structured binding 问题)

### 哈希表访问
- 用 `value(key, nullptr)` 代替 `contains()` + `value()` 两步操作

## 代码规范(Java)
- 包名 `com.contrarynr.mesh`,类内中文注释,与 C++ 端注释风格一致
- Spring Boot 4.x:JSON 库为 Jackson 3(groupId `tools.jackson`),勿用 `com.fasterxml` 的 databind

## 构建与运行
- C++(Qt 6.11.2 + MinGW 13.1 + CMake):
  `D:\ProFile\Qt\Tools\CMake_64\bin\cmake.exe --build E:\Code\MeshPlatform\client\build`
  (构建目录已配置,产物 Mesh.exe;DLL 已由 windeployqt 部署全套 6.11.2)
- Java(OpenJDK 26 + Maven Wrapper,阿里云镜像已配置于 .m2/settings.xml):
  `E:\Code\MeshPlatform\server\mvnw.cmd spring-boot:run`(Tomcat 8080,/ws 端点)
- 联调:先起 server,再双开 Mesh.exe 选在线模式,状态栏依次出现
  "正在连接信令服务器 → 主机编号已分配 → 主机'xx'已建立连接"

## 关键文件
- client/wssignalingworker.h — WebSocket 信令传输层(接替原 peernetworker)
- client/peerjsonworker.h — 信令业务层;`sendToNetWorker` 信号直传 QJsonObject,序列化只在 wssignalingworker(WebSocket 边界)与 saveOrientedFile(文件边界)发生
- client/dcworker.h / dcmanager.h — libdatachannel 封装,onLocalDescription/onLocalCandidate 产出 SDP/candidate
- server/.../SignalingWebSocketHandler.java — WebSocket 信令中转(含断连清理路由表)
- server/.../PeerRegistry.java — hostNum 分配(哈希算法与 C++ util.h 语义一致)
- server/.../PeerController.java — REST(查询/注册)

## 进度
见 PROGRESS.md(每次会话结束前更新)