# 开发进度

## 本次开发目的
简历项目升级:在自研 C++/Qt P2P 组网平台(原 E:\Code\QtCode\Mesh)基础上,增加 Java 控制面,使项目覆盖
"C++ 高性能数据面 + Java 控制面"的跨语言架构,并规划 Agent 层。求职方向:摆脱纯 C++/Qt 市场局限,
以真实需求(自用组网)驱动 Spring Boot 实战,替代照抄苍穹外卖类实训项目。

## 架构决策记录
1. 控制面/数据面分离:Java 信令服务器只做注册/编号分配/信令路由,媒体与文件走 P2P 直连不过服务器
2. 传输层替换:原 C++ TCP 自定义分帧协议('\n' 切帧)整体废弃,WebSocket 帧自带边界;
   '\n' 已从源头(peerjsonworker emit)消除,而非下游剥离
3. 信号链贯穿结构化数据:peerjsonworker→wssignalingworker 传递 QJsonObject,
   JSON 序列化只发生在 IO 边界(WebSocket 发送点/离线文件保存点),上下行链路对称
4. Spring Boot 4.1:默认 JSON 库为 Jackson 3(tools.jackson),非 com.fasterxml
5. hostNum 分配算法:Java PeerRegistry 与 C++ util.h 哈希语义完全一致(SHA-256 取字节 4-7 → %253+2)

## 已完成(截至 2026-09-14)
- MeshPlatform 项目迁移:client(C++ 必要文件)/server(Java)分目录,Git 仓库 + GitHub 公开仓库
- Java 信令服务器:PeerRegistry/PeerController(REST)/SignalingWebSocketHandler(WebSocket 中转+
  断连清理)/SignalingWebSocketConfig;阿里云 Maven 镜像配置;Jackson 3 适配
- C++ 端:wssignalingworker 接替 peernetworker;'?' 治理;QJsonObject 贯穿;构建目录 DLL 全套
  对齐 Qt 6.11.2(windeployqt 重部署)
- 端到端联调验收通过:双实例经 Java 信令组网成功,视频通话正常(2026-09-14)

## 下一步可选方向(按建议优先级)
1. **管理面:拓扑可视化 + 连接质量** — C++ 端周期上报(getStats:RTT/丢包/ICE 状态/流量) →
   后端汇聚入库(引入数据库+定时聚合) → SSE 实时推送 → 前端拓扑图渲染。
   技术增量:Spring Data JPA/数据库、SSE、前端可视化;同时为 Agent 铺垫数据底座
2. **LLM 网络诊断助手(AIOps)** — 基于汇聚的状态数据,自然语言查询网络状态、
   NAT 打洞失败根因分析、异常告警。技术增量:LLM API 集成、Prompt 工程
3. **智能路由/接入建议** — 基于历史打洞成功率给出网络策略(周期长,可选)

范畴边界:所有功能服务于"让网络本身更可用/可观测/可诊断",不做通用后台管理,
不为 AI 而 AI(如塞聊天机器人)。

## 环境
- Qt 6.11.2: D:\ProFile\Qt(6.11.2\mingw_64, Tools/mingw1310_64, Tools/CMake_64)
- JDK 26: C:\Users\contr\.jdks\openjdk-26.0.2.1
- Maven 镜像: C:\Users\contr\.m2\settings.xml(阿里云)
- GitHub: MeshPlatform 仓库(public),推送身份 ContraryNR <ContraryNR@163.com>
- 旧项目(参考,勿再修改): E:\Code\QtCode\Mesh