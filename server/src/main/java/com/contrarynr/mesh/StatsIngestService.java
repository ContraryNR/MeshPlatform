package com.contrarynr.mesh;
import org.springframework.stereotype.Service;
import tools.jackson.databind.JsonNode;

import java.util.ArrayList;
import java.util.List;

/*统计摄入侧 —— 信令层与仓储之间的翻译官:接收 SignalingWebSocketHandler 转来的原始 stats 报文,
翻译成仓储认得的摄入项(DirSnap),再一次性下达给仓储。

翻译为什么归这里而不归仓储:仓储只管"存"与"自管理",不该认识上报报文的字段名与缺省约定 ——
报文长什么样是协议层的事,改协议只动本类;仓储收到的每一项都是已解析、已定位(边+通道)、
已带到达时刻的观测,是个纯粹的存数据动作。

字段缺省语义(与 C++ 端 reportconfig 的字段组一一对应):未配置的字段组在报文里根本不出现,
故这里绝不能用 asLong(0) 兜底 —— 缺省必须是 null,与"空闲连接的 0 速率/0 积压"这类真值区分。
报文里 edges 数组的一项描述的是一个通道,而非一条边(线上的键名沿袭历史,未动)。*/
@Service
public class StatsIngestService {
    private final EdgeSnapRepository repo;
    public StatsIngestService(EdgeSnapRepository repo)
    {this.repo = repo;}

    //客户端上报一条 stats:整条报文翻译成摄入项 → 下达给仓储
    public void ingest(int sourceHostNum, JsonNode msg)
    {
        long now = System.currentTimeMillis();
        List<EdgeSnapRepository.DirSnap> channels = new ArrayList<>();
        for (JsonNode channelReport : msg.path("edges"))
        {
            int peerHostNum = channelReport.path("peer").asInt();
            if (peerHostNum < 1) continue;//0 为未分配号,忽略(1 已是可分配主机号)
            channels.add(new EdgeSnapRepository.DirSnap(now, peerHostNum,
                    channelReport.path("ch").asInt(0),
                    intOrNull(channelReport, "rtt"), longOrNull(channelReport, "up"),
                    longOrNull(channelReport, "down"), longOrNull(channelReport, "buffered"),
                    textOrNull(channelReport, "state"), textOrNull(channelReport, "iceState"),
                    textOrNull(channelReport, "netPath"),
                    textOrNull(channelReport, "candLocal"), textOrNull(channelReport, "candRemote")));
        }
        repo.write(sourceHostNum, channels);//空 edges(心跳)也要下达:节点在线与否是仓储要维护的事
    }

    //webSession 断连:让节点即时下线(其对端报告留待 TTL 自然过期)
    public void removeNode(int hostNum)
    {repo.removeNode(hostNum);}

    //缺省即 null:字段缺失 / 显式 null / 类型不符 一律视为客户端没报
    private static Integer intOrNull(JsonNode node, String field)
    {return node.hasNonNull(field) ? node.path(field).asInt() : null;}
    private static Long longOrNull(JsonNode node, String field)
    {return node.hasNonNull(field) ? node.path(field).asLong() : null;}
    //取 JSON 字段的文本值:非文本(缺失 / 显式 null / 数字 / 布尔)一律返回 null ——
    //状态与路径这类字段本来就该是字符串,不是字符串即视为客户端没报
    private static String textOrNull(JsonNode node, String field)
    {
        JsonNode v = node.path(field);
        return v.isTextual() ? v.asText() : null;
    }
}
