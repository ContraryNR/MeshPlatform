package com.contrarynr.mesh.statsWorker;
import com.github.msteinbeck.sig4j.slot.Slot1;
import org.springframework.stereotype.Component;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;
import tools.jackson.databind.node.ObjectNode;

//拓扑负载的出口:留住最近一份负载供新页面接入时立即出图(sendLatest);
//广播动作在 SsePushService,本类只管缓存这份"最近负载"。
@Component
public class TopologyBuffer
{
    private final SsePushService ssePush;
    public TopologyBuffer(SsePushService ssePush){this.ssePush = ssePush;}
    //接收打包完成的拓扑 -> 更新最近负载(推流线程写、HTTP 线程读,volatile 保证可见性)
    public final Slot1<ObjectNode> onTopologyAccept = node -> update(node.toString());
    private volatile String latest;
    public synchronized void update(String json){this.latest = json;}
    //新页面接入:补一份最近快照;尚无任何一次广播(服务刚起)则不发,页面等本周期内推进
    public void sendLatest(SseEmitter emitter)
    {
        String json = latest;
        if (json != null)
            ssePush.send(emitter, SsePushService.EVENT, json);
    }
}