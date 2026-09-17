package com.contrarynr.mesh;
import org.springframework.stereotype.Component;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;
import tools.jackson.databind.node.ObjectNode;

/*拓扑分发器 —— 打包后的拓扑 JSON 的唯一出口。

与 SsePushService 的分工:后者只管"一批 SseEmitter 的连接生命周期与广播机制"(传输层),
本类管"这个业务的负载"(业务层):定事件名、决定序列化时机、并留住最近一份负载,
好让新接入的页面立刻出图 —— 不必为一次首推专门再打包一遍。
将来多一个出口(如推给 AI Agent 的 WebSocket)时,扩在这里。*/
@Component
public class TopologyDistributor {
    //SSE 事件名:前端 EventSource 按此订阅
    public static final String EVENT = "topology";
    private final SsePushService ssePush;
    public TopologyDistributor(SsePushService ssePush)
    {this.ssePush = ssePush;}
    //最近一次广播的负载(推流线程写、HTTP 线程读)
    private volatile String latest;

    //广播一份切面:序列化一次,发给全部在线页面
    public void distribute(ObjectNode topology)
    {
        //数据用 toString() 发字符串,绕开 SseEmitter 与 Jackson 3 的转换问题
        String json = topology.toString();
        latest = json;
        ssePush.broadcast(EVENT, json);
    }

    //新页面接入:补一份最近快照(可能落后一个推流周期,其内含的 ts 就是这份数据的取样时刻);
    //尚无任何一次广播(服务刚起)则不发,页面等本周期内的 tick
    public void sendLatest(SseEmitter emitter)
    {
        String json = latest;
        if (json != null)
            ssePush.send(emitter, EVENT, json);
    }
}
