package com.contrarynr.mesh;
import com.contrarynr.mesh.statsWorker.statsConfig;
import com.github.msteinbeck.sig4j.signal.Signal1;
import com.github.msteinbeck.sig4j.signal.Signal2;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;
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

@Component
public class SignalingWebSocketHandler extends TextWebSocketHandler
{
    //推给 stats 协调者的信号:本类只判报文是否合法,合法即投 —— 不直接操作仓储(协调者经信号做异步中转)
    public final Signal2<Integer, JsonNode> statsMsgReceived = new Signal2<>();
    public final Signal1<Integer> peerDisconnected = new Signal1<>();
    private static final Logger log = LoggerFactory.getLogger(SignalingWebSocketHandler.class);
    private final Map<Integer, WebSocketSession> numToSession = new ConcurrentHashMap<>();
    private final PeerRegistry registry;//对应 coorjsonworker::nameToNumMap(注册表)
    private final statsConfig config;//管理面:统计上报配置(下发/回显的契约来源)
    private final ObjectMapper mapper;//Spring 托管的 Jackson 单例, 干 QJsonDocument 的活
    public SignalingWebSocketHandler(PeerRegistry registry, statsConfig config, ObjectMapper mapper)
    {this.registry = registry;this.config = config;this.mapper = mapper;}

    //jsonMsgSender
    private void sendJson(WebSocketSession session, ObjectNode json) throws IOException
    {
        if (session.isOpen())
            session.sendMessage(new TextMessage(json.toString()));
    }
    //信封:统一追加 type/source/target —— 业务与配置只负责"内容",信息追加按层次收拢在这(见 configMsgPacker)
    private ObjectNode jsonMsgBasePacker(String type, Integer source, Integer target, ObjectNode body)
    {
        ObjectNode msg = mapper.createObjectNode();
        msg.put("type", type);
        if (source != null)
            msg.put("source", source);
        if (target != null)
            msg.put("target", target);
        if (!body.isEmpty())
            msg.setAll(body);
        return msg;
    }

    //newSignalingSession(withoutMsg)-MeanlingLess
    @Override
    public void afterConnectionEstablished(WebSocketSession session)
    {log.info("[WS] 连接建立: {}", session.getId());}

    //newSiganlingSession(withMsg)
    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) throws IOException
    {
        JsonNode msg = mapper.readTree(message.getPayload());
        String type = msg.path("type").asText();
        if ("hostname".equals(type))
            //newHostRegistry Msg
            handleRegister(session, msg);
        else if ("stats".equals(type))
        {
            //channelsStats Msg:只判合法性,投给 stats 协调者
            int source = msg.path("source").asInt();
            if (source >= 1)
                statsMsgReceived.emit(source, msg);
        }
        else
        {
            //otherHost-Oriented Msg
            int target = msg.path("target").asInt();
            WebSocketSession targetSession = numToSession.get(target);
            if (targetSession != null && targetSession.isOpen())
                targetSession.sendMessage(message);
            else
                log.warn("[WS] sendMsg: hostNum={} 不在线, 丢弃 type={}", target, type);
        }
    }

    //newHostRegistry(distribute HostNum/statsConfig)
    private synchronized void handleRegister(WebSocketSession session, JsonNode msg) throws IOException
    {
        String hostName = msg.path("hostname").asText();
        int assigned = registry.register(hostName);
        //Map<hostNum/session>
        numToSession.put(assigned, session);
        //client 端 peerjsonworker.onExternalMsg 以 msg["hostNum"] 解析,须与 newPeer 一致放在 body,不能只塞 target
        ObjectNode hostNumBody = mapper.createObjectNode();
        hostNumBody.put("hostNum", assigned);
        sendJson(session, jsonMsgBasePacker("distributedHostNum", null, assigned, hostNumBody));
        for (Map.Entry<Integer, WebSocketSession> e : numToSession.entrySet())
        {
            if (e.getKey() == assigned || !e.getValue().isOpen())
                continue;
            ObjectNode body = mapper.createObjectNode();
            body.put("hostName", hostName);
            body.put("hostNum", assigned);
            sendJson(e.getValue(), jsonMsgBasePacker("newPeer", null, e.getKey(), body));
        }
        //单播下发当前 statsConfig:内容由配置类打包,本类只补信封
        sendJson(session, jsonMsgBasePacker("statsCfg", null, assigned, config.configMsgPacker()));
        log.info("[WS] {} 注册为 hostNum={}", hostName, assigned);
    }

    //server-client Connection disconnect->update onlineHosts/Edges
    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status)
    {
        numToSession.entrySet().removeIf(e -> {
            if (!e.getValue().equals(session))
                return false;
            peerDisconnected.emit(e.getKey());//摘除由 stats 协调者经信号处理
            return true;
        });
        log.info("[WS] 连接关闭: {} ({})", session.getId(), status);
    }

    //重整配置后向全部在线节点重下发
    public void broadcastStatsCfg()
    {
        for (Map.Entry<Integer, WebSocketSession> e : numToSession.entrySet())
        {
            if (!e.getValue().isOpen())
                continue;
            try
            {
                sendJson(e.getValue(), jsonMsgBasePacker("statsCfg", null, e.getKey(), config.configMsgPacker()));
            }
            catch (IOException ignored)
            {
                //单个会话失败不影响其余节点(该会话的清理交给 afterConnectionClosed)
            }
        }
        log.info("[WS] 统计上报配置已下发全部在线节点: enabled={} interval={}ms fields={}",
                config.enabled(), config.intervalMs(), config.fields());
    }
}