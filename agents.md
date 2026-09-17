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
- server/.../PeerController.java — REST(查询/注册)
- server/.../StatsIngestService.java — 统计摄入侧(翻译官):接收 SignalingWebSocketHandler 转来的原始
  stats 报文,翻译成仓储认得的摄入项(DirSnap:定位 peer/ch + 观测字段 + 到达时刻),再下达给仓储;
  报文格式与缺省约定(未配置的字段组在报文里根本不出现,故严禁 asLong(0) 兜底)都归这里 ——
  改协议只动本类,仓储不必认识 JSON;removeNode 的下达也在这
- server/.../StatsConfigManager.java — 统计行为管控:全部客户端 stats 行为的唯一决策点,
  持有 ReportConfig(下发给客户端的那份契约)与判活阈值 ttlMs() = 上报周期 × ttl-factor。
  两者必须同源:若周期归 A、TTL 归 B,就会出现"改了周期忘了改判活阈值"(客户端 30s 一报、
  服务端 15s 判死,拓扑一直闪)。只管决策与持有,不管下发动作(下发走信令层)。
  也**不用静态全局共享**:容器里的单例 bean 本身就是全局唯一实例,static 字段反而绕开容器
  (注入不进、测试换不掉、生命周期不受管)
- server/.../EdgeSnapRepository.java — **自管理仓库**:内存快照(节点心跳 + 边 × 两端 × 通道)、
  明细落库、分钟聚合、24h 清理,外加自己的 tick。命名里的 Repository 指它在数据流里的位置
  (上游:被摄入侧写入,再自主把切面推给 packer),不是 Spring Data 语义,故标 @Component。
  自管理落三处:① 过期事务(sweep 私有,只由 tick 触发,不由任何查询触发 —— 否则清扫节奏会被
  管理页轮询绑架;客户端全部掉线后再无上报也照样清理、照样上图);② 推流决策(dirty 由写入/断连/
  摘除置位,tick 见脏才推,纯心跳不置脏,初值为脏以便启动后先宣告一次空状态);
  ③ tick 节拍 1s = "写入到上图"的延迟上限 + 推流速率上限。顺序讲究:先清脏再取切面
  (清与取之间落进的写入会重新置脏,由下一拍兜住,不会丢)。
  对外动作:write/removeNode/snapshot/aggregate/cleanup + tick;锁只保护内存快照,
  DB I/O 与推流都在锁外;**读写必须分离** —— 读路径只过滤不动容器;
  **两端观测的合并也在这**:snapshot 交出已归一的 MergedCh(通道级)与节点列表组成的一致性切面
  Snapshot;DirSnap 是摄入项(不可变,兼判裸边 hasAnyAttr);边身份 Edge(a,b) 构造即 a<b 规范化
  (A 报"peer=B"与 B 报"peer=A"因此落进同一条边)
- server/.../TopologyPacker.java — 切面 → 分发给浏览器的内容,链上位于仓储与分发器之间:
  仓储 tick 把切面递到 deliver(打包后交给分发器),/stats/latest 则直接用纯变换 pack ——
  两条路径共用同一套打包规则。节点富化 hostName、逐通道落笔,不碰容器;
  字段缺省语义:未配置的字段组在 DirSnap/MergedCh/明细表/拓扑 JSON 里一律为 null(key 缺失),
  与"空闲连接的 0 速率/0 积压"这类真值区分;输出 **只到 channel,不做任何跨通道汇总**
  (没有边级 RTT/总速率/最差路径),通道内的归一(两端 RTT 取平均、速率镜像兜底)不是跨通道合并;
  裸边(无 channels)只有 a/b
- server/.../TopologyDistributor.java — 拓扑负载的出口:定事件名、序列化一次、广播给全部 SSE 页面,
  并留住最近一份负载供新页面接入时立即出图(sendLatest)。与 SsePushService 的分工:后者管
  "一批 SseEmitter 的连接与广播机制",本类管"这个业务的负载",将来多一个出口(如 AI Agent)扩在这里
- server/.../ReportConfig.java — 下发给客户端的统计上报配置(开关/周期/字段组)的唯一载体,
  与 C++ 端 reportconfig.h 的成员严格一一对应;不可变 record,运行时改配置是整体替换引用,
  故读取方拿到的永远是一份自洽的配置(不会出现"新周期配旧字段"的跨字段撕裂);
  ALLOWED_FIELDS(合法字段组白名单)与 parseFields(文本→集合)也在这。
  边界:只装"会下发、能左右客户端行为"的东西 —— 改一个值客户端行为会变才进来;ttl 不满足该条件
- server/.../StatsController.java + SsePushService.java — /stats/latest|history|stream|config
  (GET 查配置 + POST 调整并重新下发)与 SSE 推送管理(失效连接兜 IOException/IllegalStateException);
  /stats/latest 走 packer.pack(repo.snapshot()) 现场取切面(查询要的是"此刻",不是"最近一次推流"),
  /stats/stream 接入时用 TopologyDistributor.sendLatest() 补一份最近负载;
  改配置的编排仍是"manager 改 → 信令层重下发"(manager 不认识 WebSocket)
- server/.../entity/ + repository/ — PeerStatRecord(明细,保留 24h)/PeerStatAggregate(分钟聚合)
- server/src/main/resources/static/ — 管理页(手写 SVG 拓扑,零依赖),http://localhost:8080/
  跨通道汇总在服务端与上报链路里已全部移除(拓扑 JSON 只到 channel);质量表格一行 = 一条边的一个通道、
  节点详情逐通道分块。**唯一的例外是拓扑图的线**:一张图上每条边只画一条线,颜色与标注取"代表通道"
  (主通道优先,否则通道号最小)—— 这是页面刻意的渲染选择,分通道数据在表格与详情里逐条可查,
  别再把它当"漏删的跨通道合并"改掉(app.js 的 repChannel 处有注记)

## 进度
见 PROGRESS.md(每次会话结束前更新)