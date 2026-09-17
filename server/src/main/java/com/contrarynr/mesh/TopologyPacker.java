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

只做"呈现"这一件事:节点富化 hostName、把已合并的通道逐字段落笔。取数与过滤在仓储、锁在仓储,
发送在分发器,本类不碰容器也不发消息。

{ts, nodes:[{hostNum,hostName}],
 edges:[{a,b,channels:[{ch,rtt,up,down,buffered,netPath,iceState,candLocal,candRemote}]}]}
只给精确到 channel 的数据 —— 不做任何跨通道汇总(不输出"边的总速率/最差路径/代表通道 RTT"):
汇总对运维不够精确(排查要落到具体通道),对页面也不够准确。
每个通道内的 up/down/rtt 是"同一通道两端两支观测"归一并后的结果,不是跨通道合并。
缺省(该字段组未配置 / 该端未上报)表现为 JSON 里不含该 key,而非 0 —— 与真 0 区分。
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
        //描述当前全部边(edgeState) —— 精确到 channel,不做任何跨通道汇总
        ArrayNode edges = root.putArray("edges");
        for (Map.Entry<EdgeSnapRepository.Edge, SortedMap<Integer, EdgeSnapRepository.MergedCh>> e : snap.edges().entrySet())
        {
            ObjectNode edge = edges.addObject();
            edge.put("a", e.getKey().a());
            edge.put("b", e.getKey().b());
            if (e.getValue().isEmpty())
                continue;//裸边(仅连接数量模式):只有 a/b,前端灰显
            ArrayNode chArr = edge.putArray("channels");//已按通道号升序
            for (EdgeSnapRepository.MergedCh c : e.getValue().values())
            {
                ObjectNode cn = chArr.addObject();
                cn.put("ch", c.ch());
                if (c.rtt() != null)
                    cn.put("rtt", c.rtt());
                if (c.up() != null)
                    cn.put("up", c.up());
                if (c.down() != null)
                    cn.put("down", c.down());
                if (c.buffered() != null)
                    cn.put("buffered", c.buffered());
                if (c.netPath() != null)
                    cn.put("netPath", c.netPath());
                if (c.iceState() != null)
                    cn.put("iceState", c.iceState());
                if (c.candLocal() != null)
                    cn.put("candLocal", c.candLocal());
                if (c.candRemote() != null)
                    cn.put("candRemote", c.candRemote());
            }
        }
        return root;
    }
}
