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
5. hostNum 分配算法:Java PeerRegistry 与 C++ util.h 哈希语义完全一致(SHA-256 取字节 4-7 → %254+1);
   1 不再为 Coordinator 保留 —— Java 端只是信令中介、不参与组网,没有自身主机号,
   两端必须逐位同步(改映射会改变所有主机名的编号,属预期)
6. 管理面上报协议:stats 消息(type=stats,target=1)复用信令 WebSocket 通道,不新增连接;
   空.edges 也上报作为节点在线心跳(孤立节点可见);采集用推模型 —— dcworker 自采后经
   statsCollected 信号推给 dcmanager(边缓存在 pendingEdges),无跨线程方法调用
7. 数据库 SQLite(嵌入式单文件,零安装):xerial sqlite-jdbc + hibernate-community-dialects;
   连接池压到 1(单写者防 SQLITE_BUSY),时间字段一律 long 毫秒纪元;聚合任务错峰到每分第 10 秒
8. libdatachannel 0.24.1(含上游 master)无标准 getStats() 报表,质量指标集为轻量 API:
   RTT/累计流量差分速率/发送积压/ICE 状态/选中候选对类型(host/srflx/relay,NAT 打洞结果);
   无丢包率(DataChannel 走可靠 SCTP 不暴露)
9. 统计上报配置由 server 统一(唯一配置源):下行 statsCfg{enabled,interval,channels,fields} 在客户端
   注册时下发一次,运行时可经 POST /stats/config 改动后向全部节点重新下发;
   内容按"通道 + 字段组"两级配置(rtt/traffic/buffered/state/ice/path),而不再是抽象的"上报模式"
   —— 同一 edge 两端口径由 server 统一保证,不会一端报一端不报;
   客户端不保留任何本地调控入口(C++ 设置对话框的下拉已移除);信令断开时客户端立即从上游关闭上报,
   而不是靠发送点的 socket.isValid() 兜底;快照存活 TTL = ttl-factor × interval(默认 3×5s=15s)
10. Java 代码规范一次性对齐 agents.md(/* */ 块注释、// 后不留空格、多行函数体 { 独立成行、
   构造器注入成员紧凑排布),存量 12 个类全部标准化
11. 管理面权限区分:拓扑/历史/SSE/管理页与信令握手 /ws 全部放行(任何端都能看网络状态),
   仅"改上报配置"的 POST /stats/config 要求管理员身份(HTTP Basic,账号取自 application.properties);
   /ws 必须显式放行 —— 被拦会直接导致 C++ 客户端连不上信令服务器、整个组网失效
12. netPath 判定改为"对端候选地址是否落在本机网卡网段"(掩码前缀比较),而非候选类型:
   单看类型会把"同机/同网段互通"误判成公网(ICE 一旦选中 srflx 候选,类型就不是 host,可实际没走公网);
   同时把选中候选对摘要(candLocal/candRemote)纳入上报作为诊断依据
13. stats 域改为 **信号/槽(sig4j) + 顶层协调者** 架构(2026-09-18,响应代码内遗留的 `Question` 注释):
   - 新增 `statsWorker` 包收纳 stats 相关全部类;`PeerController` 归档到 `archive` 包并移除 register 测试接口
   - 移除 `StatsConfigManager/StatsIngestService/TopologyDistributor`;`ReportConfig→statsConfig`(可变 bean)、
     `TopologyPacker→TopologyTransformer`、`DirSnap→channelSnap`、`Edge→edgeSymbol`
   - 新增 `StatsCoordinator`(纯接线,不处理数据,类比 MainWindow)+ `StatsMsgTranslator`(协议翻译)两个 worker;
     TTL 作为全局单例 statsConfig 的 ttlMs(周期 × ttl-factor),供仓储/控制器统一取
   - `configMsgPacker()`(配置类打包 JSON) + `jsonMsgBasePacker()`(信令层补 type/source/target 信封)分层
   - `EdgeSnapRepository`:fresh/channelsOf 内联进 snapshot;write+apply 合并为 deltaUpdate;判活阈值取自 statsConfig.ttlMs()
   - `PeerStatRecord` 增全成员构造(保留 JPA 无参),新增 `statsTool.toRecord` 落库免 setter 链

## 已完成(截至 2026-09-15)
- MeshPlatform 项目迁移:client(C++ 必要文件)/server(Java)分目录,Git 仓库 + GitHub 公开仓库
- Java 信令服务器:PeerRegistry/PeerController(REST)/SignalingWebSocketHandler(WebSocket 中转+
  断连清理)/SignalingWebSocketConfig;阿里云 Maven 镜像配置;Jackson 3 适配
- C++ 端:wssignalingworker 接替 peernetworker;'?' 治理;QJsonObject 贯穿;构建目录 DLL 全套
  对齐 Qt 6.11.2(windeployqt 重部署)
- 端到端联调验收通过:双实例经 Java 信令组网成功,视频通话正常(2026-09-14)
- 管理面(连接质量上报+拓扑可视化,2026-09-14):
  - C++:dcworker::collectStats(轻量统计快照)+ dcmanager::statsTimer(5s 差分速率+组包上报)
  - Java:StatsService(内存快照+SQLite 明细入库+SSE 广播+每分钟聚合/每小时清理)、
    StatsController(/stats/latest|history|stream)、SsePushService、SignalingWebSocketHandler
    stats 分支与断连拓扑清理、@EnableScheduling
  - 前端:static/(index.html+app.js+style.css)手写 SVG 圆周拓扑,RTT 着色,节点详情,质量表格,零依赖
  - 自动化验证通过:C++ 构建、Java 编译、裸 socket 协议级模拟(注册→stats→快照合并→断连清理→
    定时聚合→/stats/history)
- 管理面 GUI 联调验收通过(2026-09-15):真实双实例组网 + 视频通话 + 前端 http://localhost:8080/
  全部正常;通话中速率上涨、断开后节点按 TTL 从拓扑消失
- 管理面配置化(2026-09-15):
  - 协议新增下行 statsCfg{enabled,interval,channels,fields} —— server 为唯一配置源:
    注册时下发 + 运行时改配置后向全部节点重发;内容按"通道 + 字段组"两级配置,避免信息不对等
  - C++ 新增 reportconfig 类承载配置(开关/周期/通道/字段);dcworker::collectStats 按字段组裁剪产出,
    裸边由 fields 为空显式表达(经 quality 布尔声明,server 不再靠"有没有 state"反推)
    —— 该 quality 声明已于 2026-09-16 移除,见下方"重构(2026-09-16)"
  - C++ 设置对话框移除了"统计上报"下拉及其调用链 —— 上报行为只由 server 决定
  - 信令断开时 wssignalingworker 发 wsDisconnected → dcmanager::onSignalingDown 立即关闭上报
  - StatsService 从 application.properties 注入配置,快照 TTL 由常量改为 ttl-factor × interval 动态推导
  - StatsController 新增 GET/POST /stats/config,含参数校验(interval/channel/字段组白名单)
  - SecurityConfig:新增 spring-boot-starter-security,拓扑与信令握手放行、仅改配置要求管理员
    (HTTP Basic;401 不写 WWW-Authenticate 头,避免浏览器弹原生框)
  - 管理页新增"上报配置"面板(启用开关/周期/通道勾选/字段勾选/管理员凭据),改动即时下发
  - SsePushService:广播失效连接摘除的 catch 由 IOException 放宽到 Exception,
    覆盖 emitter 已完成时的 IllegalStateException(并发竞态下不再向调用方抛)
- hostNum=1 放开(2026-09-15):两端哈希映射 %253+2 → %254+1(值域 [1,254]),1 不再是保留号;
  连带放开采纳侧过滤(source>=1、peerHostNum>=1)与 PeerRegistry 的 occupied 预占
- netPath 判定重做(2026-09-15):改为"对端候选地址是否落在本机网卡网段"(util.h::isInLocalSubnet),
  不再用候选类型推断链路层级;选中候选对摘要(candLocal/candRemote)纳入上报并在管理页展示
- 代码规范统一(2026-09-15):Java 存量 12 个类对齐 agents.md;清理全部 Question/待办注释

## 关键修复(2026-09-15)
- `client/tunoutworker.h`:字符串调用 `invokeMethod(worker,"sendBinaryMsg",...)` 的方法名在 dcworker 中
  并不存在(现名 `sendTunMsg`,签名与语义完全一致)。字符串形式的 invokeMethod 不受编译检查、运行期
  静默失败,表现为虚拟网卡的下行数据包发不出去(信令与视频不经过该路径,故此前联调未暴露)。
  已改回 `sendTunMsg`,并对全仓库 57 处字符串调用做了方法名静态核对(现均可解析到目标方法)

## 重构(2026-09-16)
- `StatsService`:边 key 从字符串拼接("a-b")改为结构化 `record EdgeKey(a,b)`,紧凑构造器内做
  a<b 规范化(任意顺序构造得同一 key,equals/hashCode 语义正确)。buildTopology 不再
  split+parseInt 反解端点,ingest 直接 new EdgeKey;删除 edgeKey() 方法。
  动因:用户质疑"同一 edge 两端信息对等,为何还要从 edgeName 解析两 hostNum"——
  经分析,解析本身是单端上报场景的功能必需(一端下线后 removeNode 只清该端 dirs,
  对端报告留待 TTL 过期,期间对端身份只能从外层 key 恢复,不能只从内层 dirs 取端点);
  真正的冗余根源是"结构化数据存成字符串再反解"的拼接键模式,已根除。
- `quality` 布尔退场,改用"字段缺省即 null/key 缺失"统一表达(2026-09-16):
  动因:用户指出上报字段组是"随意可调"的 —— 既然是`有哪些`字段而非`有无`质量,
  一个布尔就无法承载。具体失效场景:配 fields=rtt(不报流量)时,客户端 quality=true 但不发
  bytesSent/bytesReceived,dcmanager 不计算 up/down,server 却用 asLong(0) 兜底 →
  入库一条 up=0/down=0 的假明细、拓扑输出 "up":0 → 前端显示"0 B/s",而前端本就写了
  `e.up == null ? '—'` 的缺省分支,只是永远收不到 null。
  根因很窄:DirSnap 七个质量字段里 rtt/pcState/iceState/netPath 早已是 null 语义,
  只有 up/down/buffered 是 primitive long,类型上没给"缺省"留位置。
  落地:三字段改 Long;明细表三列改可空(需删 mesh.db 重建,update 不会放宽既有 NOT NULL);
  拓扑 JSON 仅在非 null 时输出该 key;合并用 pickRate/sumLong/maxLong 传播 null;
  裸边由"所有质量字段皆缺省"导出(入库判定含 pcState,拓扑呈现判定不含——pcState 无展示位);
  客户端删 edge["quality"] 与 reportconfig::quality(),改为 reportconfig::bare() 只做本地早返回。
  未采纳 chmod 式位掩码:字段词汇表已在 C++/Java 各有一份,掩码会加第三份"位↔名"映射,
  且 Set→int→再解回逐字段判空属于不产生新信息量的多余转换,低频监控 JSON 省不下几个字节;
  也免疫"配置变更时在途报文按旧字段集上报"的错配(报文自描述)。
  遗留:聚合表 upBytes/downBytes 仍是 long 记 0(沿用 avgRtt"全 null 记 0"的既有权衡),
  即"窗口内从未采集流量"在聚合层仍表现为 0,待后续需要时再动(会改 /stats/history 契约)。
- 新增 `EdgeSnapStore`,把边快照容器的访问逻辑从 StatsService 里搬出去(2026-09-16):
  动因:用户指出 StatsService 里"一层套一层的 removeIf"难以维护 —— 那套三层嵌套的遍历/摘除逻辑
  他曾在注释里花大量时间理清;要求封成独立类、对外只给接口。
  落地:新建 EdgeSnapStore(包内可见),内部持有 Map<Key,Map<上报方hostNum,Map<通道,DirSnap>>>,
  对外只有 put / removePeer / liveEdges 三个动作;DirSnap(单通道方向报告)与 Edge(一条边的存活视图)
  降为其内部类型,DirSnap 顺带接管上报 JSON 的解析(from)与裸边判定(hasQualityData);
  Edge.channels() 提供"两端通道并集",buildTopology 连 TreeSet 并集都不用写了。
  StatsService 侧:删除私有 DirSnap/EdgeKey/edgeSnaps 及 hasQualityData(DirSnap)/textOrNull,
  removeNode 变一行 store.removePeer(hostNum),buildTopology 的边遍历变一行 store.liveEdges(now,ttl)。
  用户原有的嵌套结构注释与"数据竟态"说明原样搬进 EdgeSnapStore(未删)。
- 顺手修掉一个必崩的 bug:`buildTopology` 原先在 `for (... : edgeSnaps.entrySet())` 循环体内
  直接 `edgeSnaps.remove(e.getKey())` —— 改 modCount 会让下一次 next() 抛
  ConcurrentModificationException。触发条件:≥2 条边,且整体过期的那条不是遍历到的最后一条
  (2026-09-15 联调只双实例即 1 条边,所以没踩到)。现由 EdgeSnapStore.liveEdges 用显式迭代器
  `it.remove()` 处理,不再是"遍历中改容器"。
- EdgeSnapStore 自查后返工(2026-09-16),用户质疑"这个 store 类写的是不是有病",确认有三处真问题:
  1) 读写不分:`liveEdges` 名曰查询实则在删数据 —— 读拓扑必须先触发一次写,且清扫节奏被调用频率
     (管理页轮询 /stats/latest)绑架,反过来没人读就不清理。现拆成纯读的 `liveEdges`(只过滤过期的
     通道/端,两端皆空则整条边不入拓扑)+ 独立的 `sweep`(唯一的摘除时机,由收报路径驱动);
     StatsService 侧同步把 `nodeLastSeen` 的判活清理从 buildTopology 里搬进新的私有
     `sweepStale(now)`,buildTopology 变成纯读(判活改为"过期就跳过,不入拓扑")。
  2) 封装漏洞:`Edge` 交出去的是容器内部那个 Map 的活引用,元素又是 public 字段的可变类 ——
     上层一行 `e.fromA().get(0).up = 999` 就能篡改 store 内部状态。现 `DirSnap` 改 record(不可变),
     读路径交出 `Map.copyOf` 不可变快照。
  3) `removePeer` 留空壳(两端皆空但顶层键值对还在,非等读路径来打扫)。现改用
     `edges.values().removeIf(...)` 顺手摘掉因此变空的边。
  取舍说明:清扫仍由收报路径驱动、没有新增定时器 —— 显示正确性由读路径的过滤保证,清扫只负责内存卫生;
  极端情况(客户端全部异常掉线且不再上报)容器里会留少量已不会显示的残影,量级受拓扑规模限制、重启即消。
- 回归验证(2026-09-16):`.trae/mesh_ws_test2.py`(多通道合并/RTT平均/最差netPath/裸边/断连清理)
  全绿;新增 `.trae/mesh_default_field_check.py` 覆盖缺省语义三情形 —— 仅报 rtt 时不得出现
  up/down/buffered/netPath/iceState 键、切到仅报 traffic+buffered 时 rtt 必须消失、
  真 0 速率与 0 积压必须保留为 0 而非被当作缺省。
- 处理用户在代码里留的 7 个 Question(2026-09-16):
  1) `Key` 改名 `Edge`(边身份,语义直白),原 `Edge`(存活视图)随之改名 `EdgeView` 避免撞名;
  2) `DirSnap.from(JsonNode edge)` 的入参在语义上描述的是一个**通道**(edges 数组的一项),改名 `fromChannel(JsonNode channelReport)`;
     StatsService 的 ingest 循环变量同步改名(协议里的数组键仍叫 edges,未动线上格式 —— 该键名同样有歧义,可后续一并改);
  3) `hasQualityData()` → `hasAnyAttr()`(Java 布尔方法用 has 前缀,不用 ifExistXxx 这种 C++ 味命名);
  4) `textOrNull` 补注释:取 JSON 字段的文本值,非文本(缺失/null/数字/布尔)一律返回 null;
  5) EdgeSnapStore 增加汇聚写接口 `update(source,peer,channelNum,channelReport,now)`:解析+落容器一次完成,
     并把解析结果交回供上层判断是否入明细表;原 `put` 收为私有。
     **未采纳"用一个枚举参数区分 insert/delete/sweep 的单入口"**:三者参数表完全不同
     (insert 要边/端/通道/报文,delete 只要 hostNum,sweep 不要参数),合成一个方法只能靠"参数并集+忽略位",
     类型安全与可读性都会变差;保持三个各司其职的写方法更清楚。
     也**未采纳"insert 后自动 sweep"**:一次 ingest 含 1~4 条通道报告,自动 sweep 会把同一次清扫重复 1~4 遍,
     清扫仍由 ingest 末尾的 sweepStale 统一触发一次。
     **采纳并强化了"确保其他调用处拿到的 liveEdges 总是最新的"**:原设计靠"调用方必须在 synchronized 里访问"
     这条只写在注释里的隐式契约,现已改为 EdgeSnapStore 各方法自带 synchronized(读接口交出 Map.copyOf 快照,
     故返回值在锁外使用安全),隐式契约消失。注意此处**不能用 volatile**:volatile 只给可见性不给互斥,
     HashMap 在并发读写(尤其扩容)下会链表成环甚至死循环,必须靠互斥锁。
  6) 上报配置抽成独立类 `ReportConfig`(对齐 C++ reportconfig.h):ALLOWED_FIELDS / MAX_CHANNEL / ttlMs /
     parseChannels / parseFields 全部移入;StatsService 只留 volatile 引用 + reportConfig()/setReportConfig()。
     顺带修掉一处跨字段撕裂:原来 4 个 volatile 字段逐个赋值,并发读可能读到"新周期配旧字段";
     不可变对象整体替换后不存在这个问题。消费方(SignalingWebSocketHandler / StatsController)改读 reportConfig()。
  7) 拓扑输出去掉一切跨通道汇总:边级不再有 rtt/up/down/buffered/netPath/iceState/candLocal/candRemote,
     只有 {a,b,channels:[{ch,rtt,up,down,buffered,netPath,iceState,candLocal,candRemote}]}。
     通道内部的"两端观测归一"(RTT 取平均、速率镜像兜底、积压取大、路径取差)保留 —— 那是同一物理量的
     两支观测,不属于跨通道汇总。裸边仍只有 a/b(判据:`channels` 键不存在)。
     连带删除 StatsService 的 sumLong(只为边级求和而存在);前端 app.js/index.html 改为逐通道渲染:
     质量表格一行 = 一条边的一个通道(新增"通道"列),节点详情逐通道分块,
     拓扑线只取"代表通道(主通道优先)"的 RTT 上色并在注释里声明那只是页面的渲染选择、服务端不参与。
- 验证(2026-09-16):Java 编译通过;server 启动正常;/stats/config 正常。两个协议脚本按新契约更新后全绿
  (mesh_ws_test2.py:边级无汇总字段、ch0 RTT=45、逐通道速率镜像兜底、裸边无 channels;
  mesh_default_field_check.py:仅 rtt 时通道对象恰为 {ch,rtt}、真 0 与缺省并存)。
  前端用内置浏览器实测(数据泵 desktop-a/laptop-b):在线节点 2、表格 2 行(主通道/文件传输,含新增"通道"列)、
  拓扑边标注 42ms 且按 RTT 上色、控制台无 JS 报错。
- ttl 迁出 ReportConfig(2026-09-16):用户质疑"ttl 在语义上属于 statsConfig 吗"——确认不属于。
  ReportConfig 的定位是"会下发、能左右客户端行为"的那份契约,而 ttl 是 server 自己的判活口径、不下发;
  硬证据:C++ reportconfig.h 只有 enabled/intervalMs/channels/fields,没有 ttlFactor,原来的"一一对应"是假话。
  落地:ttlFactor 与 ttlMs() 移入 StatsService(private final int + public long ttlMs()),
  ReportConfig 收成 3 个组件(enabled/intervalMs/fields),setReportConfig 少一个参数,
  StatsController 改调 statsService.ttlMs()。判断标准写进两处类注释:改这个值客户端行为会不会变。
- 删除"按通道裁剪"这一层(2026-09-16):用户判断它太精细化、意义不大 —— 指标采集本身已被 fields
  逐个门控(dcworker 里 `if(reportCfg.hasField("rtt"))` 才去读),再叠一层"报哪几条通道"收益太低。
  跨端删除 9 处:C++ reportconfig.h(成员/load/disable/reportsChannel)、dcworker.h(2 处判断)、
  dcmanager.h(2 处)、ReportConfig(组件+MAX_CHANNEL+parseChannels)、StatsService(@Value)、
  SignalingWebSocketHandler(下发与日志)、StatsController(校验与回显)、application.properties、
  index.html/app.js(管理页通道复选框组)。statsCfg 与 /stats/config 从此不含 channels;
  客户端恒定上报全部 4 条通道。
- 验证(2026-09-16):Java 与 C++ 均编译通过;两个协议脚本全绿;/stats/config 已无 channels 字段;
  POST /stats/config 仅带 fields 可用(interval 8000 → ttl 24000,证明 TTL 确实跟着周期走),
  非法字段组 jitter 被 400 拦下;管理页实测:通道复选框组消失、字段 6 个、"通道"列仍在,
  表格 2 行数据正常,SSE 实时更新(更新时间与速率逐轮变化,证明这三个 console 里的旧 SSE 报错
  是我反复重启 server 留下的历史记录,不是当前故障)。
- liveEdges 吞并 aliveSide 成单函数(2026-09-17):用户质疑"对 edges 做一次完整遍历+ttl检查+合并,
  怎么搞成两个函数"。原 aliveSide 是 private static,唯一调用者是 liveEdges 对 a/b 两端各调一次,
  拆分动机只是避免同一段过滤循环写两遍,代价是多一层跳转。现内联为单函数:对 a、b 各一段循环
  (getOrDefault 消掉 null 检查),末尾 Map.copyOf 交不可变快照;顺带消掉原来的 isEmpty 三元
  (Map.copyOf 对空表也返回空表)。EdgeSnapStore 收敛为四个方法各司一职:
  liveEdges(读)/update/removePeer/sweep(写)。验证:Java 编译通过。

## 重构(2026-09-17):管理面数据流方向修正 + 拆层
用户提案(交换上下游 + 拆类 + 命名统一 + 去 EdgeView),经四轮取舍后落地。

- 动因(两处真问题):
  1) StatsService 是管理面"唯一的时钟":ingest 在一个 synchronized 里既写快照、又落库、又 sweep、
     又全量序列化并 SSE 广播 —— 推送频率被上报频率绑架(N 个节点各报一次 = 一轮 N 次全量打包广播,
     而每次广播的负载都是同一份全量拓扑),且 DB I/O 占着容器锁。
  2) "没有上报就没有心跳":sweep 由收报路径驱动,一旦所有客户端同时停报,过期节点与通道永不摘除,
     管理页永远停在最后一个快照上。
- 落地(类与流向):
  - `EdgeSnapStore` → `EdgeSnapRepository`:① 两端观测的合并(mergeRtt/pickRate/maxLong/worstNetPath/
    通道并集)从消费端下沉到仓储 —— 合并属"数据聚合"不属"呈现";② 节点心跳(nodeLastSeen)搬进来,
     节点与边共用一个 TTL 时钟、一处 sweep;③ 明细落库与两个 @Scheduled(分钟聚合 / 24h 清理)一并归入,
     存储层职责完整;④ 锁只保护内存快照,DB I/O 一律在锁外;⑤ 读接口交出一致性切面
     `Snapshot(节点 + 已合并通道视图)`。
  - `EdgeView` 删除:它存在的唯一理由是"交两端生数据、由上层合并",合并下沉后这层包装失去理由,
    其 channels() 并集(TreeSet 合并)随之消失;新增 `MergedCh`(合并后的通道,不含 pcState ——
    该字段只入库、无拓扑展示位)。EdgeView + ChView 两处 record 收成仓储内部一处。
  - 新增 `TopologyPacker`(切面→JSON 的纯变换,节点富化 hostName)、`TopologyDistributor`
    (出口:事件名/序列化一次/留住最近负载供新页面首推)、`TopologyPublisher`(唯一时钟)。
  - `StatsService` 瘦身为摄入侧:ingest/removeNode 转交仓储 + 持有 ReportConfig + ttlMs();
    不再 buildTopology、不再 @Scheduled、不再持锁。
- 取舍记录:
  - 原方案的三条推流规则(ingest 立即推 / TTL 自检推 / 周期兜底推)收敛为一条**纯周期 tick**:
    "立即推"会把推送频率重新绑回上报频率,而"无变化也推"等于承认 tick 才是主线;tick 内先 sweep,
    顺带覆盖了 TTL 自检,三条本就被一条覆盖。
  - 未采纳"store 自己推流"(会让容器反向依赖 SSE),推的时机归 TopologyPublisher。
  - 命名保留 Repository 的代价已知:与 repository 包下的 Spring Data 仓储同名不同义,
    故标 @Component(非 @Repository),并在类注释写明该名字指"数据流里的位置"。
  - 刻意保留 `Snapshot` 这层 record:它承载的是"一致性切面"(节点与边一次加锁取齐),不是"某层 map 的包装";
    拆成两次取数会造出"边引用了未知节点"的瞬时错配。
  - tick 用 1s 细粒度 + 耗时比较,不用 @Scheduled 的固定周期:后者周期是编译期常量,
    读不到运行时替换的 ReportConfig(推流周期 = 上报周期,跟着 /stats/config 热更走)。
- 验证(2026-09-17):Java 编译通过;server 启动正常(Tomcat/JPA/定时任务无异常);
  `curl -N /stats/stream` 实测 5s 一跳、首推为最近负载、`/stats/latest` 为现场取数。
  协议级探针(JDK 自带 WebSocket 客户端,注册两个节点 → 互为对端上报 → 断连 → 静置)四种情形全符预期:
  两端报告落同一条边(a/b 升序)、RTT 取平均 50、速率按方向镜像兜底、积压取大、netPath 取差、
  ch1 仅单端上报即升序合并且无多余键、pcState 不出现在 channels;
  断连后节点即时消失而边靠对端报告存续;**双方都断连后静置 17s,在无任何上报的情况下边被 tick 的
  TTL 自检摘净** —— 恰是本次要修的"没有上报就没有心跳"。
  探针为临时单文件源码(java ProbeTmp.java),验证后已删;未用 PowerShell 5.1 的 ClientWebSocket:
  它把 Tomcat 的 `Connection: upgrade, keep-alive` 判为非法头(仅 .NET Framework 有此问题)。
- 观察(未改,待定):iceState/candLocal/candRemote 的合并是"x 端存在即只认 x 端字段",
  而 rtt/速率/积压/netPath 是"x 端字段缺省则回退 y 端"(两种口径沿自旧代码)。于是"哪端 hostNum 更小"
  会决定候选对诊断字段是否显示 —— 同一拓扑换个编号顺序就可能少几列。是否统一为字段级回退待定
  (会改变线上 JSON 内容,故本次未动)。

### 同日返工:把时钟收回仓储(用户纠正)
上一节把推流时钟放在 TopologyPublisher(仓储与 packer 之间),用户否定:仓储该是**自管理仓库**,
它自己的过期事务与推流时机不该外派给一个夹在中间的组件。本节推翻上一节的"时钟位置"与"配置归属"。

- 用户的职责划分(最终落地形态):
  1. `StatsIngestService`(新增)—— 摄入侧翻译官:接收 SignalingWebSocketHandler 转来的原始 stats 报文,
     翻译成仓储认得的摄入项(DirSnap),再下达给仓储。**报文解析从仓储搬到这**:仓储只认语义化摄入项,
     改协议只动本类(仓储从此不认识 JSON,也不再 import Jackson)。
  2. `StatsConfigManager`(新增)—— 管控全部客户端 stats 行为的唯一决策点:ReportConfig(下发的那份契约)
     + ttlMs()。落地理由:TTL = 上报周期 × 倍率,周期与 TTL 必须同源,否则留下"改了周期忘了改判活阈值"的
     静默耦合。用户曾问能否做成 static 全局共享 —— 否:容器里的单例 bean 本身就是全局唯一实例,
     static 字段只会绕开容器(注入不进、测试换不掉、生命周期不受管)。
  3. `EdgeSnapRepository` —— 自带 @Scheduled tick(1s)的自管理仓库。
  4. `TopologyPacker` —— 新增 deliver(切面 → 打包 → 交分发器);pack 仍为纯变换,供 /stats/latest。
  5. `TopologyDistributor` —— 最终发送(未动)。
  删除:`StatsService`(拆成 1、2 两类)、`TopologyPublisher`(时钟收进仓储,打包+转交并入 packer)。
  新流向:摄入侧 → 仓储(自检+推流) → packer → distributor → SSE。
- 仓储的自管理落三处:① sweep 私有、只由 tick 触发(不由任何查询触发);② dirty 由写入/断连/摘除置位,
  tick 见脏才推;③ 1s 节拍既是"写入到上图"的延迟上限,也是推流速率上限。
- 取舍记录:
  - **推流时机改为"不脏不推"**(用户定):于是"推流周期"这个概念整体消失,1s tick 只是节拍与节流粒度。
    代价已知:长时间无变化时页面不再刷新(其"更新于"停在最后一次变化的取样时刻),这是"没变化就不推"
    的应有之义,不是故障。相应地 dirty 初值置脏:启动后先宣告一次空状态,免得空转服务器上的页面
    永远收不到第一帧。
  - **removeNode 只在实际撤掉内容时才置脏**:否则一个早已因 TTL 从图上消失的节点断连也会多推一帧空数据,
    与"不脏不推"自相矛盾(实测中正是它产生了多余的第 4 条 SSE 事件,已修)。
  - **清脏与取切面的顺序**:先清后取(清与取之间落进的写入会重新置脏、由下一拍兜住);反过来会让那段
    时间的写入被这一次清标记吞掉。
  - 仓储自此认识 packer(用户指定的流向"仓储自主推流到 packer")—— 代价是存储层知道下游存在,
    换来的是时钟与推流决策都归仓储;若日后要解耦,可退回接口/回调,本轮不做。
  - 拒绝"static 配置类"与"接口式订阅(Sink)":前者绕开容器,后者用户明确不要这层抽象,直接依赖更直白。
- 验证(2026-09-17):Java 编译通过;server 启动无异常(无循环依赖:仓储 → manager/packer 单向)。
  两个既有回归脚本全绿(mesh_ws_test2.py:逐通道归一/RTT平均/速率镜像/通道并集/裸边/断连清理;
  mesh_default_field_check.py:缺省语义与真 0 并存)。
  新增 `.trae/mesh_ttl_sweep_check.py`:连接保持、只是停报 20s → 节点与边被自行摘净**且**这次摘除被推给页面
  (SSE 全程仅三条:接入首帧 → 上报 → TTL 摘除,之后静默,无冗余帧)—— 正是本次返工要保证的
  "客户端全部掉线后再无上报,也照样清理、照样上图"。
  抓到的多余推送(上述 removeNode 一例)已按"实际撤掉才置脏"修掉并复测通过。

### 聚合表删掉"区间流量"两个字段(2026-09-17,用户指出)
- 动因:用户指出 `PeerStatAggregate.upBytes/downBytes` 事实上没有意义 —— 明细里的 up/down 是
  "客户端按真实采集周期差分出的速率(B/s)",窗口内求和只在周期恰好 1s 时才偶然等于字节数;
  更糟的是它随配置漂移(同一段真实流量,周期 5s→10s,和就减半),连横向比较都不成立。
  用户的判断:记录区间内的流量与流速没有意义,除非用户反馈明确的传输速度异常。
- 落地:`PeerStatAggregate` 删两字段(类注释写明"为何刻意不存区间流量",并给出真要做时的正确算法
  Σ(速率 × 该样本的真实 elapsed)/1000);聚合 JPQL 的 select 收成 [source, peer, channel, avgRtt, count];
  `StatsController.AggregateInfo` 同步删两字段。前端本就没有消费 /stats/history,无破坏面。
- 踩到的坑(记以备后查):旧库里两列是 **NOT NULL**(Hibernate 当年按 `long` 原始类型建出),实体删字段后
  INSERT 不再带它们 → `SQLITE_CONSTRAINT_NOTNULL ... peer_stat_aggregate.down_bytes` 把定时聚合整个打挂
  (表现是 /stats/history 一直空、日志刷异常)。`ddl-auto=update` 既不会放宽也不会删除既有列 ——
  与 2026-09-16 "三列改可空需重建库" 是同一类问题。
  处理:`ALTER TABLE peer_stat_aggregate DROP COLUMN up_bytes/down_bytes`(SQLite 3.35+ 支持),
  保住了 35 条聚合 + 161 条明细,没删库重建。新建库不受影响。
- 验证:编译通过;启动无异常;喂数据后等到窗口聚合,/stats/history 返回新形态
  `[{sourceHostNum, peerHostNum, channel, windowStart, avgRtt, sampleCount}]`,
  日志 `[Stats] 聚合窗口 ... 完成,3 条通道`,无 SQLITE 异常。
- 顺带澄清(回答用户两问):
  ① 客户端 `elapsed` 不是"每秒采样再求和" —— `dcworker::collectStats` 是**一次瞬时快照**(读一次累计字节),
  采集频率 = server 下发的上报周期;`elapsed` = 距上次采集的**实测间隔**(steady_clock),只为把累计值的
  差分换算成 B/s 服务(定时器会漂移、周期会被改,拿名义周期去除会算错速率)。每个通道每次上报 = 一个样本。
  ② server 端与 stats 相关的时间量共 5 个:客户端上报周期(可改,唯一配置源)/ 判活 TTL(= 周期 × 3,派生)/
  仓储 tick 节拍 1s(固定:推流上限 + 上图延迟上限)/ 聚合窗口 1 分钟 + 每分钟第 10 秒触发(固定)/
  明细保留 24h + 每小时清理(固定)。后三个若要可调应进 application.properties,而不是 ReportConfig
  —— 它们不影响客户端行为。

- 顺带把库结构与实体逐字段核了一遍(用户问"数据库层面的问题是否排除了"),结果:
  明细表 12 列 / 聚合表 7 列与实体完全一致(Java 原始类型 int/long/double → 库里 NOT NULL,
  包装类型 Integer/Long/String → 库里可空),明细表两个索引在;**聚合表的唯一约束 uk_psa_edge_window
  在库里根本不存在**(实体 @Table 声明了,但建表时就没有,ddl-auto=update 不会给既有表补约束/索引),
  已用 `CREATE UNIQUE INDEX` 补上(现无重复行,补索引安全)。库中对象只有这两张表 + 三个索引。
  另一处已修的是上面那个 NOT NULL 拒插(删列),两处都属"库落后于代码"。
- 需要记住的脾气:`ddl-auto=update` 只加新表/新列 —— **不删列、不放宽 NOT NULL、不补约束/索引**。
  所以每次改实体字段后,旧库都会悄悄与代码不一致(今天的两次都是这么暴露的)。
  开发期最省事的做法是改完字段直接删 mesh.db 让 Hibernate 重建;要保历史数据就手动 ALTER/CREATE。

### 复查"channels → 前端总览"的合并逻辑是否删净(2026-09-17,用户要求)
2026-09-16 只删了服务端与前端表格/详情,用户要求确认"全部 channel 独立按分支显示"是否到位。逐层审计结果:
- ✅ 服务端:`TopologyPacker` 输出严格到 channel,边级无任何字段;`worstNetPath/maxLong/pickRate` 只是
  **同一通道两端**的观测归一(不是跨通道);早期只为边级求和而生的 `sumLong` 已不存在;
  `/stats/history` 也按 (边, 通道) 分行。
- ✅ C++ 上报链路:`pendingEdges.append(edge)` 是一通道一项,flush 时原样发出 —— 上报前无合并。
- ✅ 前端质量表格与节点详情:一行 = 一条边的一个通道(有"通道"列)、详情逐通道分块。
- ⚠️ 唯一残留 = **前端拓扑图的线**:每条边只画一条线,颜色与标注取 `repChannel()`(主通道优先)。
  这意味着除主通道外,其余通道的 RTT 在图上不可见(表格/详情里仍有)。用户裁决:**暂不动** ——
  这是"一张图上一条边一条线"必然的渲染代表,不是数据层的合并;已在该处与 repChannel 加注说明,
  并在 agents.md 写明,免得下次又被当成漏删改掉。
- C++ Qt 状态栏那个"跨 peer × 通道的入站/出站总数":用户明确**不改**,能看本地速度总览本就是既有设计意图
  (那是客户端自己的界面,与管理页不是同一个消费者)。
- 顺手清掉两处真残渣:style.css 的 `.ch-val/.ch-cand`(旧单行布局遗留的死样式)、app.js 里悬空的
  `detail-row` 类名(CSS 从无此规则)。清完做了一次类名交叉核对(CSS 规则 ↔ app.js/index.html 引用),
  无死样式、无悬空引用。
- 附注:`server/src/main/resources/static/` 与本次新增的多个 Java 文件在 git 里仍是未跟踪状态
  (本轮未提交,按惯例仅在用户要求时提交)。

### POST /stats/config 改为 JSON 请求体(2026-09-17,用户要求)
- 动因:用户提出"改成 JSON 请求体(而不是字符串)传参,语义更明确" —— 原先 fields 是个逗号串
  (`?fields=rtt,traffic`),服务端要 split/trim/跳空段,前端要 join(',') 再拼,语义藏在字符串格式里。
- 落地:
  - `@RequestParam boolean enabled, int interval, String fields` →
    `@RequestBody ConfigUpdate(Boolean enabled, Integer interval, LinkedHashSet<String> fields)`,fields 直接是 JSON 数组。
  - 两处语义比原来更明确:① fields 缺省(null)= 保持原值、空数组 = 裸边(原先靠"参数缺失"与"空串"区分);
    ② enabled/interval 用包装类型 + 显式判空 → 体里漏写时回 400,而不是让 boolean/int 静默变成 false/0
    (那会悄悄把上报关掉)。
  - app.js 同步:`URLSearchParams` + `join(',')` → `JSON.stringify({enabled, interval:Number(...), fields:[...]})`
    + `Content-Type: application/json`。
  - `ReportConfig.parseFields` 保留,但从此只服务 application.properties 那条路(@Value 注入的仍是逗号串)。
- 自己引入又修掉的一处:DTO 里 fields 先写成 `Set<String>`,Jackson 对 Set 默认绑成 **HashSet(哈希序)**
  → 回显顺序被打乱(发 `rtt,traffic,buffered,state,ice,path` 回显成 `path,rtt,buffered,ice,state,traffic`),
  而原 `parseFields` 用 LinkedHashSet 是保序的;改成 `LinkedHashSet<String>` 后照抄书写顺序。
- 验证(curl 实测,请求形状与页面 fetch 等价):
  无认证 POST → 401;合法 JSON(interval 8000 + fields[2 项])→ 200 且 ttl 跟着变 24000;
  非法字段组 → 400;漏写 enabled → 400;不带 fields → 保持上一份;空数组 `[]` → 裸边;顺序原样回显;
  日志三连 `[WS] 统计上报配置已下发全部在线节点: ... fields=[...]`,无异常。
- 观察(未改,pre-existing):`ALLOWED_FIELDS = Set.of(...)` 的迭代顺序随 JVM 运行而变(两次启动分别是
  `buffered,rtt,path,state,traffic,ice` 与 `path,rtt,buffered,ice,traffic,state`),而管理页的字段复选框组
  是按它渲染的 → 勾选框顺序会在重启后跳。换成有序不可变集合即可(一行),本次未动。
- 附注:400 的**具体原因**默认不写进 HTTP 响应体(Spring Boot 的 `server.error.include-message` 默认 never),
  页面也只显示"下发失败: HTTP 400";要让原因可见,加一行 `server.error.include-message=always` 即可,本次未动。
- 补充裁决(用户问"能不能用 @ConfigurationProperties 免掉字符串转集合",经实测后决定**保持现状**):
  - 想省掉手写解析,其实连注解都不用 —— 把 `@Value` 的参数类型从 `String` 改成 `Set<String>`,
    Spring 默认转换器就会按逗号拆分。用 `@Value` 背后同一个转换器(Spring Framework 7.0.9)实测:
    `rtt,traffic` ✓、`rtt, traffic` **会 trim** ✓、`rtt,rtt,ice` 去重且保序(LinkedHashSet)✓、
    空串 → 空集合 ✓(留空=裸边 的语义保住);**唯一缺口:空段被保留成空字符串元素**
    (`rtt,,ice` → size=3、`rtt,` 与 `"  "` 也各多一个空元素),而 parseFields 是显式跳过的。
  - 挂 `@ConfigurationProperties` 到 ReportConfig 本体不合适:① 它只对启动时那一次绑定生效,
    而这个类的另一半身份是"运行时可整体替换的领域值对象"(POST 用 JSON 绑定后 new 一个,volatile 持有);
    ② 前缀 `mesh.stats` 与类的边界不重合(同前缀下的 `ttl-factor` 刻意不属于它,注解会静默忽略 —— 行为对但语义错位)。
  - 结论:**parseFields 保留**,两个解析器各自守一个边界 —— 配置文件(人写,逗号串是 properties 的惯用形态,
    且现在没有任何地方再解析 URL 参数字符串了)与 HTTP 接口(程序对程序,JSON 数组)。
    真正该避免的是"同一边界里两套格式",本例不存在;要彻底统一应走"另立 @ConfigurationProperties 属性类"那条路,
    而不是把 HTTP 退回字符串。此决定与实测数据留档,免得后人凭印象来回改。

### 取消"通道内两端观测归一",展示改为按上报方视角(2026-09-17,用户提出)
- 动因:用户提出"如果这种情况都要合并,那让双端都上报的意义是什么?" —— 主张区分一条边的两端,
  选中哪个节点就展示哪个节点作为 source 的通道状态。两条支撑事实:① 落库层**一直是按单端存的**
  (PeerStatRecord 带 sourceHostNum;分钟聚合 group by source/peer/channel),合并只发生在展示层;
  ② 旧合并规则本身在制造缺陷 —— iceState/candLocal/candRemote 是"x 端存在就只认 x 端"(x = hostNum 较小端),
  同一张拓扑换个编号顺序就可能少几列;buffered 取大也掩盖了"是哪一端在堵"。
- 边界澄清(用户论据需要修正的一处):up/down **不是两份独立测量** —— A.up ≡ B.down(我发 = 对端收),
  两端上报的是同一物理量的两次观测,故旧"镜像兜底"不是丢信息而是补缺;取消合并后两份并列出现,
  它们理应相等,差异本身成为诊断信号(计时错位/某端没报/计数异常)。rtt 同样是同一连接的两次测量,
  但两端各自测得的数值有意义(buffered/netPath/iceState/候选对则完全各属一端)。
- 落地:
  - 仓储:删除 MergedCh 与整组合并函数(merge/mergeRtt/pickRate/maxLong/worstNetPath + NET_PATH_RANK);
    snapshot 改为交出"通道 → 上报方 hostNum → 该端原样观测"的并列结构;DirSnap 新增 hasAnyData
    (进拓扑的判据,比入库判据 hasAnyAttr 少一个无展示位的 pcState)。
  - Packer:形状变为 `{a,b,channels:[{ch, a:{...}, b:{...}}]}`,两端各自原样落笔,某端没报该通道则整个对象不出现。
  - 前端:节点详情与质量表格**完全跟随选中节点**(未选中时表格为空并在标题写明视角,提示"点击节点查看");
    A→B / B→A 两列按物理方向落座(选中端是 b 时它报的 down 就是 A→B),换成选中对端只是换一组计数器看同一对方向;
    详情面板的"发送/接收"直接取该端自己的读数(不再需要按方向翻转)。拓扑线保持"一条边一条线",
    固定取 hostNum 较小端的代表通道(a 端无数据时退到 b 端),图例里写明"线上数字 = hostNum 较小端所报 RTT"。
  - C++ 与数据库**零改动**(它们本来就报/存单端数据)。
- 验证(2026-09-17):编译通过;起服后三个回归脚本全绿 ——
  mesh_ws_test2.py 断言改为"两端并列各自原样"(ch0 两端 rtt 40/50 并存而非旧的平均 45、速率不跨端镜像、
  真 0 保留、未上报的那端不出现空对象);mesh_default_field_check.py 的缺省/真 0 三情形按端断言;
  mesh_ttl_sweep_check.py 照旧(无上报时自行摘净)。附注:未过浏览器实测(本机无可用浏览器驱动),
  前端改动为纯渲染逻辑,数据契约已由脚本覆盖。

### 收尾两处 + 待决清单(2026-09-17 夜)
1. **拓扑线的视角改为跟随选中**(用户指定的"完美"形态):选中任一节点时,连在它上面的边全部切到
   "以该节点为 source"的视角;未选中时取 hostNum 较小端(a 端此边无数据时退到另一端)。
   图例文案与 app.js 头注释同步。至此页面只有一种口径:线、详情、表格随同一个选中节点走。
2. **ALLOWED_FIELDS 改为有序不可变集合**:`Collections.unmodifiableSet(new LinkedHashSet<>(List.of(...)))`。
   它的迭代顺序就是管理页 6 个字段复选框的渲染顺序,原来用 `Set.of` 时该顺序随 JVM 的随机盐
   每次启动都变(实测两次启动曾得到不同的顺序),页面上复选框会跳。实测两次启动
   `allowedFields` 均为 `["rtt","traffic","buffered","state","ice","path"]`,不再跳。
- 验证(2026-09-17 夜):编译通过;两次启动比对 allowedFields 顺序一致;mesh_ws_test2.py 与
  mesh_default_field_check.py 全绿(这两处改动都是渲染/常量层面,不动数据契约);server 已停、端口已释放。
- **待用户决策(未动)**:
  1. ~~是否给 pcState 一个展示位~~ → 用户裁决:加(见下一节,已落地)。
  2. **application.properties 侧的字段组没有白名单校验**:拼错(如 `trafic`)会静默失效 —— 下发的字段名
     客户端不认识,那组数据永远不报且无任何报错。可选:启动时校验并 fail-fast(或 warn 后继续)。
  3. 是否提交 git(static/ 整个目录与本轮全部改动仍未被跟踪)。
  4. 400 的具体原因是否要进响应体(`server.error.include-message=always`,一行)。

### 第三轮:pcState 展示位 + 字段组改名 + 视角沿图传播(2026-09-17 夜,用户提出)
1. **pcState 补上展示位**(用户裁决"干脆加一栏展示 pcState"):
   - 仓储:`DirSnap.hasAnyData()` 删除 —— pcState 有了展示位之后,它与入库判据 `hasAnyAttr()` 答案一致,
     合并为一个判据(注释写明:两个问题现在同解,将来若出现"只入库不上图"的字段就在这里分岔)。
   - Packer:单端对象新增 `pcState`,字段顺序与入库列一致(rtt/up/down/buffered/pcState/iceState/netPath/候选对)。
   - 前端:质量表格新增"PC 状态"列(置于 ICE 状态左侧)、节点详情新增"PC 状态"行。
   - 后果:只配 `pcState` 一组也不再退化成裸边 —— 该组数据现在页面上可见了。
2. **字段组改名 `state`→`pcState`、`ice`→`iceState`**(用户:"太不具体了"):
   - 契约要求名字四处一致:字段组名 = 报文 JSON key = DirSnap 分量 = 入库列名(PC 的报文 key 也从 state 改名)。
   - 服务端:ReportConfig 白名单、application.properties 默认值、StatsIngestService 读键、WS 协议注释。
   - 客户端:dcworker.h 的 hasField 与报文 key(6 个 switch 分支)、reportconfig.h 的字段组说明。
   - **旧客户端必须重新编译**才能配合新服务端(否则 hasField 匹配不上,这两组字段不再上报)。
   - 客户端实测:用 Qt 自带工具链(Ninja + mingw1310_64)g++ 重编译链接**通过**。
3. **拓扑线的视角改为"沿图传播"**(用户的 Floyd 式设想):选中节点后以它为源做一次**无权 BFS**,
   每条边取"跳数更近"的那一端(直连边必然是选中端自己),等距或不可达时归 hostNum 较小端;
   未选中时一律取较小端。实现是单源 BFS 而非 Floyd —— 此图无权、且只需单源最短路,效果即
   "离选中节点最近者的视角"从选中节点沿图向外扩散。
- 验证:服务端编译 + 起服;`/stats/config` 字段组已是 `["rtt","traffic","buffered","pcState","iceState","path"]`;
  mesh_ws_test2.py 新增 pcState 断言、与 mesh_default_field_check.py 双双全绿(报文里 pcState 正确落位);
  客户端 ninja 构建通过;server 已停、端口已释放。
- 仍未做:properties 侧白名单校验、400 原因进响应体(见上节待决 2/4)。

## 下一步可选方向(按建议优先级)
1. **LLM 网络诊断助手(AIOps)** — 基于汇聚的状态数据,自然语言查询网络状态、
   NAT 打洞失败根因分析(打洞路径数据已就绪)、异常告警。技术增量:LLM API 集成、Prompt 工程。
   数据底座(内存快照/明细/分钟聚合)与配置下发通道均已就绪,可直接消费 /stats/latest|history
2. **智能路由/接入建议** — 基于历史打洞成功率给出网络策略(周期长,可选)
3. **STUN 与 netPath 的语义澄清** — 单机双开若 ICE 选中 srflx 候选,即使对端就在本机也会判为 wan;
   诊断字段已就绪,下一步是查清"为什么没选 host-host"(可能是防火墙拦入站),再决定是否调整
   ICE 配置(局域网场景是否需要 STUN)

范畴边界:所有功能服务于"让网络本身更可用/可观测/可诊断",不做通用后台管理,
不为 AI 而 AI(如塞聊天机器人)。

## 环境
- Qt 6.11.2: D:\ProFile\Qt(6.11.2\mingw_64, Tools/mingw1310_64, Tools/CMake_64)
- JDK 26: C:\Users\contr\.jdks\openjdk-26.0.2.1
- Maven 镜像: C:\Users\contr\.m2\settings.xml(阿里云)
- GitHub: MeshPlatform 仓库(public),推送身份 ContraryNR <ContraryNR@163.com>
- 旧项目(参考,勿再修改): E:\Code\QtCode\Mesh