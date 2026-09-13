package com.contrarynr.mesh;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
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

/**
 * 信令中转 —— 对应 C++ 侧 coornetworker(TCP 收发/路由) + coorjsonworker::onExternalMsg(注册分发) 的合体。
 *
 * 协议与 C++ 版逐字段一致(见 Mesh 项目), 每条消息是一个 JSON 文本:
 *   上行: {"type":"hostname","hostname":"xxx"}            —— 上线注册
 *   下行: {"type":"distributedHostNum","hostNum":N,"target":N} —— 告知新节点分到的编号
 *   下行: {"type":"newPeer","hostName":"xxx","hostNum":N,"target":M} —— 向已有节点广播新节点
 *   中转: {"type":"sdp"/"candidate","target":N,...}       —— 按 target 原样转发给目标节点
 *
 * 与 C++ 版的一个刻意差异: 原版 Coordinator 本身也是一个 DC 节点(hostNum=1, 注册时
 * emit goCreateOfferER 主动建连); Java 后端是纯信令服务, 不参与媒体面 ——
 * 各 Qt 节点彼此 full-mesh 直连, 后端只做"介绍人"。这也是真实世界信令服务器的标准形态。
 */
@Component
public class SignalingWebSocketHandler extends TextWebSocketHandler {

    private static final Logger log = LoggerFactory.getLogger(SignalingWebSocketHandler.class);

    /** 对应 coornetworker::hostSocketMap —— hostNum -> WebSocket 会话(路由表) */
    private final Map<Integer, WebSocketSession> numToSession = new ConcurrentHashMap<>();

    private final PeerRegistry registry;   // 对应 coorjsonworker::nameToNumMap(注册表)
    private final ObjectMapper mapper;     // Spring 托管的 Jackson 单例, 干 QJsonDocument 的活

    public SignalingWebSocketHandler(PeerRegistry registry, ObjectMapper mapper) {
        this.registry = registry;
        this.mapper = mapper;
    }

    /** 连接建立: 对应 C++ 里 server->nextPendingConnection() 的时刻, 此刻还不知道对方是谁 */
    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        log.info("[WS] 连接建立: {}", session.getId());
    }

    /**
     * 收到文本消息 —— 对应 coornetworker::onReadyRead 的分发:
     * C++ 靠单线程事件循环天然串行, Spring 是多线程并发回调, 所以注册流程加了 synchronized。
     * (WebSocket 帧自带边界, 不再需要 C++ 里手写的 '\n' 粘包切分)
     */
    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) throws IOException {
        JsonNode msg = mapper.readTree(message.getPayload());
        String type = msg.path("type").asText();

        if ("hostname".equals(type)) {
            handleRegister(session, msg);
        } else {
            // sdp / candidate 等: 按 target 中转 —— 对应 C++ 里 "target!=1 则 write 给目标 socket" 的分支
            int target = msg.path("target").asInt();
            WebSocketSession targetSession = numToSession.get(target);
            if (targetSession != null && targetSession.isOpen()) {
                // 原样转发原始 JSON 文本, 不重序列化 —— 与 C++ 直接 write(oneMsg+'\n') 完全一致
                targetSession.sendMessage(message);
            } else {
                log.warn("[WS] sendMsg: hostNum={} 不在线, 丢弃 type={}", target, type);
            }
        }
    }

    /** 对应 coorjsonworker::onExternalMsg 的 type=="hostname" 分支 */
    private synchronized void handleRegister(WebSocketSession session, JsonNode msg) throws IOException {
        String hostName = msg.path("hostname").asText();
        int assigned = registry.register(hostName);

        // 绑定: 该会话从此代表这个 hostNum —— 对应 (*numToSocketMap)[distributedHostNum]=socket
        numToSession.put(assigned, session);

        // 1) 告知新节点它的编号 —— 对应 distJson
        ObjectNode dist = mapper.createObjectNode();   // ObjectNode 用起来就是 QJsonObject
        dist.put("type", "distributedHostNum");
        dist.put("hostNum", assigned);
        dist.put("target", assigned);
        sendJson(session, dist);

        // 2) 向其他在线节点广播 newPeer —— 对应 newPeerJson 循环
        //    (原版遍历 nameToNumMap 全员, 但发往已断线的 socket 也会被丢弃, 效果等价)
        for (Map.Entry<Integer, WebSocketSession> e : numToSession.entrySet()) {
            if (e.getKey() == assigned || !e.getValue().isOpen()) {
                continue;
            }
            ObjectNode peer = mapper.createObjectNode();
            peer.put("type", "newPeer");
            peer.put("hostName", hostName);
            peer.put("hostNum", assigned);
            peer.put("target", e.getKey());
            sendJson(e.getValue(), peer);
        }
        log.info("[WS] {} 注册为 hostNum={}", hostName, assigned);
    }

    /** 连接关闭 —— 对应 C++ disconnected 分支: 移除会话映射, 但保留注册表(与原版语义一致) */
    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        numToSession.entrySet().removeIf(e -> e.getValue().equals(session));
        log.info("[WS] 连接关闭: {} ({})", session.getId(), status);
    }

    private void sendJson(WebSocketSession session, ObjectNode json) throws IOException {
        if (session.isOpen()) {
            session.sendMessage(new TextMessage(json.toString()));
        }
    }
}