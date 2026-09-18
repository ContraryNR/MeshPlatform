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

### QHash迭代
- key+value 一起迭代: `for(auto [k,v] : hash.asKeyValueRange())` —— **必须按值 `auto`,不能用 `auto&`**:
  其迭代器 `operator*` 返回 `std::pair` 纯右值临时对象,非 const 左值引用绑不上(实测 Qt 6.11.2);
  QVector/QString 等值类型隐式共享,按值绑定是浅拷贝,开销可忽略
- 只迭代 value: `for(auto* w : hash.values())`(QHash 本体 range-for 只产出 value,`*it` 为 `T&`)
- 结构化绑定能否编译只取决于被绑定类型是否 tuple-like(如 `std::pair`),与 value 本身是否 iterable 无关
- 需要零拷贝遍历时才用 STL 迭代器: `for(auto it=hash.cbegin(); it!=hash.cend(); ++it)`,循环体内 `it.key()/it.value()`

### 哈希表访问
- 用 `value(key, nullptr)` 代替 `contains()` + `value()` 两步操作

## 代码规范(Java)
以下规范适用于本项目所有 Java 代码:
- 包名 `com.contrarynr.mesh`,类内中文注释,与 C++ 端注释风格一致
- Spring Boot 4.x:JSON 库为 Jackson 3(groupId `tools.jackson`),勿用 `com.fasterxml` 的 databind

### 类布局
- 构造器注入的成员与构造方法紧凑排布于类定义开头
- 类成员(字段/常量/内部类)声明之间不留空行
- 内部类紧凑排布:成员较多时不写成单行,可拆分成长度相近的多行;花括号不独立成行

### 注释
- 单行注释使用 `//`,不使用 `/* */`;`//` 与注释内容之间不留空格
- 多行注释:`/*` 与 `*/` 不独立成行,续行行首不加 `*` 前缀

### 控制流
- 函数体行数较多时,`{` 独立成行,置于函数声明下一行;for/while/if 多行代码块同理
- 单行语句体不加花括号(同 C++ 规范)

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
- client/dcworker.h / dcmanager.h — libdatachannel 封装,onLocalDescription/onLocalCandidate 产出 SDP/candidate;
  dcworker::collectStats 自采后经 statsCollected 信号推给 dcmanager(推模型,无跨线程方法调用),pendingEdges 缓冲由 statsTimer 按配置周期打包上报(type=stats,仅在线模式);
  产出按 reportconfig 的字段组裁剪:未配置的字段组根本不写该 key,server 以"所有质量字段皆缺省"导出裸边;
  dcmanager::applyStatsConfig 应用 server 下发的整包配置,onSignalingDown 在信令断开时立即停报;
  netPath 由 util.h::isInLocalSubnet 判定(对端候选地址是否落在本机网卡网段),并附 candLocal/candRemote 诊断
- client/reportconfig.h — 统计上报配置(开关/周期/通道/字段组)的唯一载体;来源只有 server 的 statsCfg 下发,
  客户端不提供本地调控入口(设置对话框里的统计上报下拉已移除),避免同一 edge 两端信息不对等
- server/.../SignalingWebSocketHandler.java — WebSocket 信令中转(含断连清理路由表与拓扑快照、stats 分支、
  注册时与运行时下发 statsCfg 上报配置)
- server/.../SecurityConfig.java — 管理面权限区分:拓扑/历史/SSE/管理页与信令握手 /ws 全部放行,
  仅改配置的 POST /stats/config 要求管理员(HTTP Basic);/ws 若被拦会导致组网整体失效
- server/.../PeerRegistry.java — hostNum 分配(哈希算法与 C++ util.h 语义一致,值域 [1,254])
- server/.../archive/PeerController.java — **已归档**:register(POST)测试接口已移除(注册改由信令通道 hostname 报文完成);
  仅保留 list/byNum 只读查询供调试,不需要可整体删除
- server/.../statsWorker/StatsCoordinator.java — **stats 域的统领者(类比客户端 MainWindow)**:本身**不处理任何数据**,
  只做"信号 → 槽"接线。全部 worker 通过信号/槽互连,连线统一下在本类 —— 谁触发谁、触发后往哪流只看 wire() 一处。
  连线清单:signaling.statsMsgReceived → translator.onRawStats;translator.channelSnapReady → repo.onWrite;
  signaling.peerDisconnected → repo.onRemoveNode;repo.snapshotReady → transformer 打包;transformer.topologyReady → buffer 缓存 + ssePush 广播
- server/.../statsWorker/StatsMsgTranslator.java — 协议翻译 worker:原始 stats 报文(JSON) → 摄入项 List<channelSnap>,改协议只动本类;
  只做映射不碰容器,翻译好经 channelSnapReady 信号下发(仓储根本不认识 JSON)
- server/.../statsWorker/statsConfig.java — 下发给客户端的统计上报配置(开关/周期/字段组)的唯一载体,
  与 C++ 端 reportconfig.h 成员严格一一对应;带状态的可变 bean(set() 整体替换引用,读取方拿到自洽配置),
  只装"会下发、能左右客户端行为"的字段(enabled/intervalMs/fields),另含全局判活阈值 ttlMs = 周期 × ttl-factor
  ALLOWED_FIELDS(合法字段组白名单)与 parseFields(文本→集合)也在这 —— 后者只服务 application.properties 那条路
  (HTTP 请求体已是 JSON 数组)。**ALLOWED_FIELDS 顺序就是管理页复选框渲染顺序**,故必须是有序不可变集合
  (Set.of 会随 JVM 随机盐乱序)。字段组固定 6 个,名字在报文 JSON key / 摄入项分量 / 入库列名四处一致:
  rtt、traffic、buffered、pcState、iceState、path。configMsgPacker() 打包 config JSON(不含 type/target,由信令层信封补)
- server/.../statsWorker/EdgeSnapRepository.java — **自管理仓库**:内存快照(节点心跳 + 边 × 两端 × 通道)、
  明细落库、分钟聚合、24h 清理,外加自己的 tick。Repository 指它在数据流里的位置,非 Spring Data 语义,@Component。
  自管理:① 过期事务(sweep 私有,只由 tick 触发,不由查询触发);② 推流决策(dirty 置位,tick 见脏才推);
  ③ tick 1s。切面就绪不直接推下游 —— 发 snapshotReady 信号,由协调者连到打包链(仓库不认识下游)。
  **不做任何两端归一/取舍**:snapshot 交出"通道 → 上报方 hostNum → 该端原样观测"的并列结构;
  channelSnap 是摄入项(不可变,hasAnyAttr 判裸边);边身份 edgeSymbol(a,b) 构造即 a<b 规范化。
  判活阈值从全局单例 statsConfig.ttlMs() 取(不重复计算)
- server/.../statsWorker/TopologyTransformer.java — 切面 → 分发给浏览器的内容(原 TopologyPacker):节点富化 hostName、
  两端观测各自原样落笔,不碰容器、不做取舍。onSnapshotAccept 槽接收切面 → 打包 → topologyReady 信号发出;
  /stats/latest 用纯变换 pack 现场取。形状:{ts, nodes:[{hostNum,hostName}],
  edges:[{a,b,channels:[{ch, a:{...}, b:{...}}]}]};up/down 是该端自己的方向读数(A.up ≡ B.down);
  字段缺省即 null(key 缺失);只到 channel,无跨通道汇总;裸边只有 a/b
- server/.../statsWorker/TopologyBuffer.java — 拓扑负载出口(原 TopologyDistributor):经 onTopologyAccept 槽留住最近一份
  负载供新页面接入时立即出图(sendLatest);广播动作在 SsePushService
- server/.../statsWorker/SsePushService.java — SSE 传输层:管"一批 SseEmitter 的连接与广播机制",
  失效连接兜 IOException/IllegalStateException;经 onTopologyAccept 信号槽广播 EVENT=topology
- server/.../statsWorker/StatsController.java + signaling 层 — /stats/latest|history|stream|config
  (GET 查配置 + POST 调整并重下发;POST 请求体是 JSON `{enabled,interval,fields[]}` —— fields 缺省=保持原值、
  空数组=裸边)。/stats/latest 走 transformer.pack(repo.snapshot()) 现场取(查询要"此刻",不是最近一次推流);
  /stats/stream 接入用 buffer.sendLatest() 补一份最近负载;改配置编排:statsConfig.set() →
  signalingHandler.broadcastStatsCfg()(信令层只负责包"信封" jsonMsgBasePacker + 下发,内容由 statsConfig 打包)
- server/.../entity/ + repository/ — PeerStatRecord(明细,保留 24h)/PeerStatAggregate(分钟聚合)
- server/src/main/resources/static/ — 管理页(手写 SVG 拓扑,零依赖),http://localhost:8080/
  展示按**选中节点的视角**:选中谁就只看它作为上报方的那一份观测(节点详情与质量表格都跟随,
  未选中节点时表格为空并在标题里写明视角);某端对象不存在 = 该端没报该通道(显示"—")。
  up/down 的"发送/接收"随视角落座,A→B / B→A 两列表头形容的是物理方向而非某一端。
  表格列:主机A/主机B/通道/RTT/A→B/B→A/发送积压/PC 状态/ICE 状态/路径;详情面板逐通道分块给出同一组字段。
  跨通道汇总在服务端与上报链路里已全部移除(拓扑 JSON 只到 channel);
  **拓扑那条线是"一条边只有一条线"的唯一例外**:未选中节点时取 hostNum 较小端的代表通道
  (该端此边无数据时退到另一端);选中节点后**视角沿图传播** —— 以选中节点为源做一次无权 BFS,
  每条边取"跳数更近"的那一端(直连边必然是选中端自己),等距或不可达时归 hostNum 较小端。
  它只决定线的颜色与那个数字,不改动任何数据,图例里已写明

## 进度
见 PROGRESS.md(每次会话结束前更新)