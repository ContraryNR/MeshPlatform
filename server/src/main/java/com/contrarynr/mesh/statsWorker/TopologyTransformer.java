package com.contrarynr.mesh.statsWorker;
import com.contrarynr.mesh.PeerRegistry;
import com.github.msteinbeck.sig4j.signal.Signal1;
import com.github.msteinbeck.sig4j.slot.Slot1;
import org.springframework.stereotype.Component;
import tools.jackson.databind.ObjectMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;

import java.util.Map;
import java.util.SortedMap;

//切面 -> 分发给浏览器的内容:节点富化 hostName、两端观测各自原样落笔,不碰容器、不做任何取舍。
//推流路径走"切面就绪信号 -> onSnapshot 槽"(打包后经 topologyReady 信号向上转发);
//查询路径(/stats/latest)则由 Controller 直接调 pack() 现场取,两条路径共用同一套打包规则。
@Component
public class TopologyTransformer
{
    private final PeerRegistry registry;
    private final ObjectMapper mapper;
    public TopologyTransformer(PeerRegistry registry, ObjectMapper mapper)
    {this.registry = registry;this.mapper = mapper;}
    //推流链上的槽/信号:切面进来 -> 打包 -> 发出 topologyReady(由协调者连到缓存与广播)
    public final Slot1<EdgeSnapRepository.Snapshot> onSnapshotAccept = this::deliver;
    public final Signal1<ObjectNode> topologyReady = new Signal1<>();
    public void deliver(EdgeSnapRepository.Snapshot snap)
    {topologyReady.emit(pack(snap));}

    public ObjectNode pack(EdgeSnapRepository.Snapshot snap)
    {
        ObjectNode root = mapper.createObjectNode();
        //描述拓扑更新时间(即切面的取样时刻)
        root.put("ts", snap.ts());
        //描述当前在线的全部主机(已按 hostNum 升序,顺序稳定,前端布局不抖)
        ArrayNode nodes = root.putArray("nodes");
        for (int hostNum : snap.nodes())
        {
            ObjectNode n = nodes.addObject();
            n.put("hostNum", hostNum);
            String name = registry.hostNameFor(hostNum);
            n.put("hostName", name != null ? name : "未知主机");
        }
        //描述当前全部边(edgeState) —— 精确到 channel,通道内两端观测并列,不做任何取舍/汇总
        ArrayNode edges = root.putArray("edges");
        for (Map.Entry<EdgeSnapRepository.edgeSymbol, SortedMap<Integer, Map<Integer, EdgeSnapRepository.channelSnap>>> e
                : snap.edges().entrySet())
        {
            int a = e.getKey().a(), b = e.getKey().b();
            ObjectNode edge = edges.addObject();
            edge.put("a", a);
            edge.put("b", b);
            if (e.getValue().isEmpty())
                continue;//裸边(仅连接数量模式):只有 a/b,前端灰显
            ArrayNode chArr = edge.putArray("channels");//已按通道号升序
            for (Map.Entry<Integer, Map<Integer, EdgeSnapRepository.channelSnap>> ce : e.getValue().entrySet())
            {
                ObjectNode cn = chArr.addObject();
                cn.put("ch", ce.getKey());
                //键就是这条边的两端 hostNum,前端一眼知道哪份观测属于哪个节点
                writeSide(cn, "a", ce.getValue().get(a));
                writeSide(cn, "b", ce.getValue().get(b));
            }
        }
        return root;
    }

    //单端观测落笔:缺省字段不写该 key(与真 0 区分)
    private static void writeSide(ObjectNode chObj, String name, EdgeSnapRepository.channelSnap s)
    {
        if (s == null)
            return;//这一端没上报该通道:整个对象不出现,前端显示"—"
        ObjectNode side = chObj.putObject(name);
        if (s.rtt() != null)
            side.put("rtt", s.rtt());
        if (s.up() != null)
            side.put("up", s.up());
        if (s.down() != null)
            side.put("down", s.down());
        if (s.buffered() != null)
            side.put("buffered", s.buffered());
        if (s.pcState() != null)
            side.put("pcState", s.pcState());
        if (s.iceState() != null)
            side.put("iceState", s.iceState());
        if (s.netPath() != null)
            side.put("netPath", s.netPath());
        if (s.candLocal() != null)
            side.put("candLocal", s.candLocal());
        if (s.candRemote() != null)
            side.put("candRemote", s.candRemote());
    }
}