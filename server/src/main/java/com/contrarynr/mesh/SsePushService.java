package com.contrarynr.mesh;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/*SSE 推送中心 —— 管理所有浏览器端的 SseEmitter 生命周期与广播。

与 WebSocket 信令的区别:信令面向 C++ 客户端(双向、低延迟、自动重连语义自定义);
SSE 面向管理页面(单向服务端推送、浏览器原生 EventSource 自动重连),职责互补。

Spring 语义速查:
 - SseEmitter: Spring MVC 的服务器推送句柄,交给 Tomcat 异步响应通道写出
 - 超时传 0L = 永不超时(拓扑推送是长连接,断开由 onError/onCompletion 感知)*/
@Component
public class SsePushService {
    private static final Logger log = LoggerFactory.getLogger(SsePushService.class);
    //已连接的浏览器页面 —— CopyOnWriteArrayList 适配"读多写少+遍历时移除"的场景
    private final List<SseEmitter> emitters = new CopyOnWriteArrayList<>();
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
            //IOException:页面已关闭/网络断
            //IllegalStateException:该 emitter 已完成或超时(广播与失效回调并发时的竞态),
            // 同样是"通道不可用",一并摘除,避免死连接常驻列表并在每次广播时抛异常
            //广播路径复用本方法,所以兜在这里即可,broadcast 无需再包一层 try-catch
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
