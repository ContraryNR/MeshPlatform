package com.contrarynr.mesh;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/*信令中转 —— 对应 C++ 侧 coornetworker(TCP 收发/路由) + coorjsonworker::onExternalMsg(注册分发) 的合体。

协议与 C++ 版逐字段一致(见 Mesh 项目), 每条消息是一个 JSON 文本:
  上行: {"type":"hostname","hostname":"xxx"}            —— 上线注册
  下行: {"type":"distributedHostNum","hostNum":N,"target":N} —— 告知新节点分到的编号
  下行: {"type":"newPeer","hostName":"xxx","hostNum":N,"target":M} —— 向已有节点广播新节点
  下行: {"type":"statsCfg","enabled":true,"interval":5000,
         "fields":["rtt","traffic","buffered","state","ice","path"],"target":N}
        —— 下发统计上报配置:开关/周期/字段全部由 server 统一调控(客户端无本地调控入口)
  中转: {"type":"sdp"/"candidate","target":N,...}       —— 按 target 原样转发给目标节点

与 C++ 版的一个刻意差异: 原版 Coordinator 本身也是一个 DC 节点(hostNum=1, 注册时
emit goCreateOfferER 主动建连); Java 后端是纯信令服务, 不参与媒体面 ——
各 Qt 节点彼此 full-mesh 直连, 后端只做"介绍人"。这也是真实世界信令服务器的标准形态。*/

@Component
public class SignalingWebSocketHandler extends TextWebSocketHandler {
    private static final Logger log = LoggerFactory.getLogger(SignalingWebSocketHandler.class);
    private final Map<Integer, WebSocketSession> numToSession = new ConcurrentHashMap<>();
    private final PeerRegistry registry;//对应 coorjsonworker::nameToNumMap(注册表)
    private final StatsIngestService statsIngest;//管理面:连接质量上报的翻译与下达入口
    private final StatsConfigManager configManager;//管理面:下发/回显统计上报配置的来源
    private final ObjectMapper mapper;//Spring 托管的 Jackson 单例, 干 QJsonDocument 的活
    public SignalingWebSocketHandler(PeerRegistry registry, StatsIngestService statsIngest,
    StatsConfigManager configManager, ObjectMapper mapper)
    {this.registry = registry;this.statsIngest = statsIngest;this.configManager = configManager;this.mapper = mapper;}
    //连接建立: 对应 C++ 里 server->nextPendingConnection() 的时刻, 此刻还不知道对方是谁
    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        log.info("[WS] 连接建立: {}", session.getId());
    }
    /*收到文本消息 —— 对应 coornetworker::onReadyRead 的分发:
    C++ 靠单线程事件循环天然串行, Spring 是多线程并发回调, 所以注册流程加了 synchronized。
    (WebSocket 帧自带边界, 不再需要 C++ 里手写的 '\n' 粘包切分)*/

    //`C++端`
    //wsSocket->open(QUrl(QString("ws://%1:%2/ws").arg(ip).arg(port)));
    //**WebSocket一对一连接建立成功**
    //`Java端`
    //回调函数->TextWebSocketHandler.handleTextMessage->通过参数传递session(相当于tcpServer::connect传递的tcpSocket)
    //检查附带的message(初始消息)
    //hostName->注册到numToSession(Map容器)
    //`无附带消息`则`无效`

    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) throws IOException {
        JsonNode msg = mapper.readTree(message.getPayload());
        String type = msg.path("type").asText();
        if ("hostname".equals(type))
            handleRegister(session, msg);
        else if ("stats".equals(type))
        {
            //管理面:连接质量上报(客户端 5s 一报,不中转,交摄入侧翻译后下达给仓储)
            int source = msg.path("source").asInt();
            if (source >= 1)//0 为未分配号,忽略(1 已是可分配主机号)
                statsIngest.ingest(source, msg);
        }
        else
        {
            //sdp / candidate 等: 按 target 中转 —— 对应 C++ 里 "target!=1 则 write 给目标 socket" 的分支
            int target = msg.path("target").asInt();
            WebSocketSession targetSession = numToSession.get(target);
            if (targetSession != null && targetSession.isOpen())
                //原样转发原始 JSON 文本, 不重序列化 —— 与 C++ 直接 write(oneMsg+'\n') 完全一致
                targetSession.sendMessage(message);
            else
                log.warn("[WS] sendMsg: hostNum={} 不在线, 丢弃 type={}", target, type);
        }
    }
    //对应 coorjsonworker::onExternalMsg 的 type=="hostname" 分支
    private synchronized void handleRegister(WebSocketSession session, JsonNode msg) throws IOException
    {
        String hostName = msg.path("hostname").asText();
        int assigned = registry.register(hostName);
        //绑定: 该会话从此代表这个 hostNum —— 对应 (*numToSocketMap)[distributedHostNum]=socket
        numToSession.put(assigned, session);
        //1) 告知新节点它的编号 —— 对应 distJson
        ObjectNode dist = mapper.createObjectNode();//ObjectNode 用起来就是 QJsonObject
        dist.put("type", "distributedHostNum");
        dist.put("hostNum", assigned);
        dist.put("target", assigned);
        sendJson(session, dist);
        //2) 向其他在线节点广播 newPeer —— 对应 newPeerJson 循环
        //(原版遍历 nameToNumMap 全员, 但发往已断线的 socket 也会被丢弃, 效果等价)
        for (Map.Entry<Integer, WebSocketSession> e : numToSession.entrySet())
        {
            if (e.getKey() == assigned || !e.getValue().isOpen())
                continue;
            ObjectNode peer = mapper.createObjectNode();
            peer.put("type", "newPeer");
            peer.put("hostName", hostName);
            peer.put("hostNum", assigned);
            peer.put("target", e.getKey());
            sendJson(e.getValue(), peer);
        }
        //3) 下发统计上报配置 —— server 是唯一配置源,注册完成即生效
        //(此刻客户端刚拿到号、还没上报过第一条 stats,下发时机刚好)
        sendStatsCfg(session, assigned);
        log.info("[WS] {} 注册为 hostNum={}", hostName, assigned);
    }
    //连接关闭 —— 对应 C++ disconnected 分支:移除会话映射(保留注册表),并同步清理拓扑快照
    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status)
    {
        numToSession.entrySet().removeIf(e -> {
            if (!e.getValue().equals(session))
                return false;
            statsIngest.removeNode(e.getKey());//该会话代表的节点下线,拓扑中即时消失
            return true;
        });
        log.info("[WS] 连接关闭: {} ({})", session.getId(), status);
    }
    //向单个节点下发统计上报配置(注册时) —— 对应 C++ peerjsonworker 的 type=="statsCfg" 分支
    //enabled:总开关;interval:上报周期(ms);fields:要上报的字段组(空=裸边)
    private void sendStatsCfg(WebSocketSession session, int hostNum) throws IOException
    {
        ObjectNode cfg = mapper.createObjectNode();
        cfg.put("type", "statsCfg");
        cfg.put("target", hostNum);
        ReportConfig rc = configManager.reportConfig();
        cfg.put("enabled", rc.enabled());
        cfg.put("interval", rc.intervalMs());
        ArrayNode flds = cfg.putArray("fields");
        for (String f : rc.fields())
            flds.add(f);
        sendJson(session, cfg);
    }
    //向全部在线节点重新下发配置(运行时经 POST /stats/config 改动后调用)
    public void broadcastStatsCfg()
    {
        for (Map.Entry<Integer, WebSocketSession> e : numToSession.entrySet())
        {
            if (!e.getValue().isOpen())
                continue;
            try
            {
                sendStatsCfg(e.getValue(), e.getKey());
            }
            catch (IOException ignored)
            {
                //单个会话失败不影响其余节点(该会话的清理交给 afterConnectionClosed)
            }
        }
        ReportConfig rc = configManager.reportConfig();
        log.info("[WS] 统计上报配置已下发全部在线节点: enabled={} interval={}ms fields={}",
                rc.enabled(), rc.intervalMs(), rc.fields());
    }
    private void sendJson(WebSocketSession session, ObjectNode json) throws IOException
    {
        if (session.isOpen())
            session.sendMessage(new TextMessage(json.toString()));
    }
}