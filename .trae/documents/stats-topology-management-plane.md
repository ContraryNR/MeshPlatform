# 管理面:连接质量上报 + 拓扑可视化

## Context
PROGRESS.md 下一步优先级 1。端到端联调已验收(2026-09-14),现为本项目补"管理面":C++ 客户端周期上报每条 P2P 连接的质量数据 → Java 后端汇聚入库(SQLite)→ SSE 实时推送 → 浏览器 SVG 拓扑图。技术增量:Spring Data JPA、SQLite、SSE、前端可视化,同时为后续 AIOps 诊断铺垫数据底座。

**技术边界(已查证)**:项目所用 libdatachannel 0.24.1(上游 master 亦同)无标准 getStats() 报表,仅有 `pc->rtt()/bytesSent()/bytesReceived()/state()/iceState()/getSelectedCandidatePair()` 与 `dc->bufferedAmount()`。质量指标集为:RTT、上下行速率、发送积压、ICE 状态、选中候选对类型(host/srflx/relay,NAT 打洞结果)。无丢包率(可靠 SCTP 不暴露)。

**已确认决策**:数据库 SQLite(零安装单文件)、前端手写 SVG(vanilla JS 零依赖)、SSE 推送、上报周期 5s。

## 一、C++ 端(client/)

### 1. dcworker.h — 新增 `collectStats()` 槽
- public slots 内新增 `QJsonObject collectStats()`;开头 `if(isShuttingDown||!pc||!dc) return {};`
- rtt:`if(auto rtt=pc->rtt())` 取 optional 毫秒值
- bytesSent/bytesReceived 累计值原样返回;buffered 取 `dc->bufferedAmount()`
- state/iceState 枚举 switch 映射字符串
- `rtc::Candidate local,remote;` 配 `pc->getSelectedCandidatePair(&local,&remote)`,type() 枚举映射 "host"/"srflx"/"relay"
- 返回对象字段:peer(自身 peerHostNum)、rtt、bytesSent、bytesReceived、buffered、state、iceState、localType、remoteType

### 2. dcmanager.h — statsTimer 组包上报
- 构造函数新增 statsTimer(5000ms),startTimer/stopTimer/cleanQOBJ 与 basicTimer 同进退
- 超时 lambda:`if(!onlineMode) return;`(离线模式无服务器,避免 onInternalMsg 落盘分支)
- 遍历 `for(auto& [hostNum,workerGroup] : ipRoute.asKeyValueRange())`(value 是 iterable,不能用 begin/end,符合 agents.md)
- 主通道 `if(dcworker* worker=workerGroup.value(0,nullptr))`;`worker->isShuttingDown` 跳过
- 采集经 `QMetaObject::invokeMethod(worker,"collectStats",Qt::BlockingQueuedConnection,Q_RETURN_ARG(QJsonObject,st))` 投递到 worker 线程执行,与 shutdown() 里 pc.reset()/dc.reset() 串行化,消除跨线程竞争(worker 线程从不会阻塞等待 DC 线程,无死锁;项目已有多处 BlockingQueuedConnection 先例)
- 差分速率:dcmanager 存 `QHash<int,QPair<qint64,qint64>> lastBytes`,新值<旧值(通道重建)则本周期置 0 并重置基线;产出 up/down 字段(B/s)
- edges 非空才组包:`{"type":"stats","target":1,"edges":[...]}` emit `transferWorkerMsg`(复用现有链路:getFinalJson 自动补 source → sendToNetWorker → wssignalingworker 唯一序列化边界,零新增改动)

### 3. 上行消息格式
```json
{"type":"stats","target":1,"source":3,"edges":[
 {"peer":5,"rtt":42,"up":12345,"down":23456,"buffered":1024,
  "state":"connected","iceState":"completed","localType":"srflx","remoteType":"host"}]}
```

## 二、Java 端(server/,包 com.contrarynr.mesh,类内中文注释)

### 4. pom.xml 新增依赖
- `spring-boot-starter-data-jpa`
- `org.xerial:sqlite-jdbc`(BOM 已管理则免版本,解析失败再显式写版本)
- `org.hibernate.orm:hibernate-community-dialects`(版本随 BOM)

### 5. application.properties 追加
```
spring.datasource.url=jdbc:sqlite:mesh.db
spring.datasource.driver-class-name=org.sqlite.JDBC
spring.datasource.hikari.maximum-pool-size=1
spring.jpa.database-platform=org.hibernate.community.dialect.SQLiteDialect
spring.jpa.hibernate.ddl-auto=update
```
**关键**:连接池必须压到 1(SQLite 单写者,Windows 上 SQLITE_BUSY 明显);时间字段一律 long 毫秒,避开 TIMESTAMP 方言映射。

### 6. 新增类
- `entity/PeerStatRecord`(明细):id(IDENTITY)、sourceHostNum、peerHostNum、rtt(Integer 可空)、up、down、buffered(long)、pcState、iceState、localType、remoteType(String)、ts(long);索引 ts 与 (sourceHostNum,peerHostNum,ts)
- `entity/PeerStatAggregate`(聚合):id、sourceHostNum、peerHostNum、windowStart(long 对齐分钟)、avgRtt(double)、upBytes、downBytes、sampleCount;唯一约束 (sourceHostNum,peerHostNum,windowStart)
- `repository/PeerStatRecordRepository` + `repository/PeerStatAggregateRepository`(后者含聚合 JPQL:按窗口 group by source/peer 取 avg(rtt)/sum(up)/sum(down)/count)
- `StatsService`:内存快照(`Map<Integer,NodeSnap>` 节点、`Map<String,DirSnap>` 边,key="min-max" 有向各存一份);ingest 时补 hostName(PeerRegistry.hostNameFor)、更新快照、落明细、SSE 广播;快照合并规则:边 rtt 取两方向平均(仅一方取该方),a→b 速率优先取 a.up 缺失取 b.down,候选类型仅 b 报告时对调;TTL 15s 超龄丢弃;`removeNode(hostNum)` 供断连清理
- `StatsController`(@RestController /stats):GET /latest(当前快照)、GET /history?minutes=N(聚合查询)、GET /stream(SSE)

### 7. SignalingWebSocketHandler 修改
- handleTextMessage 新增分支:`else if("stats".equals(type))` → statsService.ingest(msg.path("source").asInt(), msg)
- afterConnectionClosed:removeIf 时找出被移除的 hostNum,调 statsService.removeNode

### 8. MeshBackendApplication:+@EnableScheduling
StatsService 两个 @Scheduled:
- 聚合 `cron="10 * * * * *"`(避开整分写入冲突):窗口明细 group by 聚合 upsert 进聚合表(@Transactional)
- 清理(每小时):删 ts < now-24h 的明细

### 9. SSE 实现要点
- StatsController 持 `CopyOnWriteArrayList<SseEmitter>`;`new SseEmitter(0L)` 永不超时;onCompletion/onTimeout/onError 自删
- 连接建立先推一次当前快照(新浏览器立即有图)
- 广播:遍历 emitters,`event().name("topology").data(json字符串)`,IOException 移除;数据用 ObjectNode.toString() 发字符串,绕开 SseEmitter 与 Jackson 3 的转换问题
- 推送 JSON:`{nodes:[{hostNum,hostName}], edges:[{a,b,rtt,up,down,buffered,iceState,aType,bType}], ts}`

## 三、前端(server/src/main/resources/static/,新建目录)

### 10. index.html + app.js + style.css
- EventSource 订阅 /stats/stream,收到 topology 事件重渲染
- SVG 拓扑:节点=在线主机放圆周布局(节点数少,圆周足够),边=连接按 RTT 着色(绿<100ms/黄<300ms/红/灰=无值),线上标 RTT 值
- 节点 hover/点击显示详情面板:速率、积压、ICE 状态、本地/远端候选类型(打洞结果)
- 底部质量数据表格(节点对、RTT、up/down、状态);字段缺失显示 "—"
- 静态资源零依赖离线可用

## 四、分阶段验收
1. **上报+日志**:构建 `D:\ProFile\Qt\Tools\CMake_64\bin\cmake.exe --build E:\Code\MeshPlatform\client`;起 server(`E:\Code\MeshPlatform\server\mvnw.cmd spring-boot:run`)后双开 Mesh.exe,服务器日志每 5s 打 stats,原信令功能回归正常
2. **入库**:server 工作目录生成 mesh.db,明细表有行、ts 递增
3. **REST**:/stats/latest 见节点+边;1 分钟后 /stats/history?minutes=5 见聚合
4. **SSE**:`curl -N http://localhost:8080/stats/stream` 每 5s 收 topology 事件;断开无异常刷屏
5. **前端**:http://localhost:8080/ 圆周拓扑、着色、hover 详情、表格
6. **断连**:杀一个 Mesh 实例,节点消失≤15s,日志确认快照清理

## 五、风险与回退
- **SQLITE_BUSY**:池=1 + 聚合错峰第 10 秒;仍失败 JDBC URL 加 `?journal_mode=WAL`
- **BlockingQueuedConnection 卡顿**:单 worker 读取微秒级,5s 周期无感
- **建连早期 rtt/候选对为空**:字段缺省,前端显示 "—"
- **回退点**:全部增量——Handler stats 分支独立于 sdp/candidate 转发;C++ 注释掉 statsTimer->start() 即回退;前端纯静态可单独删;JPA 依赖与信令面无耦合

## 收尾
完成后更新 PROGRESS.md(已完成项、架构决策:stats 消息协议/SQLite 选型/libdatachannel 轻量指标边界)与 agents.md 关键文件清单。
