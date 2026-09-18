package com.contrarynr.mesh.statsWorker;
import com.github.msteinbeck.sig4j.slot.Slot1;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;
import tools.jackson.databind.node.ObjectNode;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

//SSE 推送SSE 传输层:管"一批 SseEmitter 的连接与广播机制";
//负载内容由 TopologyTransformer 产出、经协调者连到 onTopologyAccept 信号广播。
@Component
public class SsePushService
{
    public static final String EVENT = "topology";
    private static final Logger log = LoggerFactory.getLogger(SsePushService.class);
    //已连接的浏览器页面 —— CopyOnWriteArrayList 适配"读多写少+遍历时移除"的场景
    private final List<SseEmitter> emitters = new CopyOnWriteArrayList<>();
    //拓扑信号槽:协调者把打包器产出的 topologyReady 信号连到这里 -> 对全部页面广播
    public final Slot1<ObjectNode> onTopologyAccept = node -> broadcast(EVENT, node.toString());
    //注册一个新的 SSE 客户端:挂好三个失效回调后返回,由 Controller 交给响应流
    public SseEmitter register()
    {
        SseEmitter emitter = new SseEmitter(0L);
        emitters.add(emitter);
        emitter.onCompletion(() -> emitters.remove(emitter));
        emitter.onTimeout(() -> emitters.remove(emitter));
        emitter.onError(t -> emitters.remove(emitter));
        log.info("[SSE] 页面接入,当前在线 {}", emitters.size());
        return emitter;
    }
    //单发:发送失败(页面已关闭/网络断)即从列表摘除,不向上抛
    public void send(SseEmitter emitter, String eventName, String data)
    {
        try
        {
            emitter.send(SseEmitter.event().name(eventName).data(data));
        }
        catch (Exception e)
        {
            //IOException:页面已关闭/网络断;IllegalStateException:该 emitter 已完成或超时
            //(广播与失效回调并发时的竞态) —— 同样是"通道不可用",一并摘除避免死连接常驻
            emitters.remove(emitter);
        }
    }
    //广播:对全部页面推送同一事件
    public void broadcast(String eventName, String data)
    {
        for (SseEmitter emitter : emitters)
            send(emitter, eventName, data);
    }
}