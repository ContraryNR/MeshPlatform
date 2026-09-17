package com.contrarynr.mesh;
import org.springframework.stereotype.Component;
import tools.jackson.databind.ObjectMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;

import java.util.Map;
import java.util.SortedMap;

/*拓扑打包器 —— 把仓储交出的切面(Snapshot)打成分发给浏览器的内容。

链上的位置:仓储 → 本类 → 分发器。仓储自主推流时把切面递到 deliver,本类打包后交给分发器;
查询路径(/stats/latest)则直接用纯变换 pack —— 两条路径共用同一份打包规则,页面看到的形态永远一致。

只做"呈现"这一件事:节点富化 hostName、把两端各自的观测原样落笔。取数与过滤在仓储,
发送在分发器,本类不碰容器;更关键的是**不做任何取舍** —— 不取平均、不镜像兜底、不取最差,
谁报的就是谁的。

{ts, nodes:[{hostNum,hostName}],
 edges:[{a,b,channels:[{ch, a:{rtt,up,down,buffered,netPath,iceState,candLocal,candRemote},
                            b:{同左}}]}]}
同一通道的两端数据并列:up/down 是**该端自己的方向读数**(我发/我收),rtt 是该端自己测的,
buffered 是该端自己的发送队列,iceState/候选对是该端自己选中的 —— 两端各一份,天然可与对端比对。
某端没上报该通道、或该端字段全缺省(字段组未配置):整个 "a"/"b" 对象不出现(前端显示"—")。
字段缺省表现为对象里不含该 key,而非 0 —— 与真 0 区分。
裸边(仅连接数量模式)只有 a/b,无 channels,前端灰显。*/
@Component
public class TopologyPacker {
    private final PeerRegistry registry;
    private final ObjectMapper mapper;
    private final TopologyDistributor distributor;//链上下一环:最终发送
    public TopologyPacker(PeerRegistry registry, ObjectMapper mapper, TopologyDistributor distributor)
    {this.registry = registry;this.mapper = mapper;this.distributor = distributor;}

    //推流路径:仓储 tick 交来的切面 —— 打包后交给分发器
    public void deliver(EdgeSnapRepository.Snapshot snap)
    {distributor.distribute(pack(snap));}

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
        for (Map.Entry<EdgeSnapRepository.Edge, SortedMap<Integer, Map<Integer, EdgeSnapRepository.DirSnap>>> e
                : snap.edges().entrySet())
        {
            int a = e.getKey().a(), b = e.getKey().b();
            ObjectNode edge = edges.addObject();
            edge.put("a", a);
            edge.put("b", b);
            if (e.getValue().isEmpty())
                continue;//裸边(仅连接数量模式):只有 a/b,前端灰显
            ArrayNode chArr = edge.putArray("channels");//已按通道号升序
            for (Map.Entry<Integer, Map<Integer, EdgeSnapRepository.DirSnap>> ce : e.getValue().entrySet())
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

    //单端观测落笔(不含 pcState:它只入库、没有展示位)
    private static void writeSide(ObjectNode chObj, String name, EdgeSnapRepository.DirSnap s)
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
        if (s.netPath() != null)
            side.put("netPath", s.netPath());
        if (s.iceState() != null)
            side.put("iceState", s.iceState());
        if (s.candLocal() != null)
            side.put("candLocal", s.candLocal());
        if (s.candRemote() != null)
            side.put("candRemote", s.candRemote());
    }
}
