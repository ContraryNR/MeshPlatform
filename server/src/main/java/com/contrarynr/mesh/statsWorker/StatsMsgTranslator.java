package com.contrarynr.mesh.statsWorker;
import com.github.msteinbeck.sig4j.signal.Signal2;
import com.github.msteinbeck.sig4j.slot.Slot2;
import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;

import java.util.ArrayList;
import java.util.List;

//协议翻译 worker:把信令层转来的原始 stats 报文,译成仓储认得的摄入项 List<channelSnap>。
//只做 JSON → 摄入项 的映射(改协议只动本类),不碰容器;翻译好经 channelSnapReady 信号往下发。
@Component
public class StatsMsgTranslator
{
    public final Slot2<Integer, JsonNode> onRawStats = this::translate;
    public final Signal2<Integer, List<EdgeSnapRepository.channelSnap>> channelSnapReady = new Signal2<>();
    private void translate(int sourceHostNum, JsonNode msg)
    {
        long now = System.currentTimeMillis();
        List<EdgeSnapRepository.channelSnap> channels = new ArrayList<>();
        for (JsonNode channelReport : msg.path("edges"))
        {
            int peerHostNum = channelReport.path("peer").asInt();
            if (peerHostNum < 1)
                continue;
            channels.add(new EdgeSnapRepository.channelSnap(now, peerHostNum,
                    channelReport.path("ch").asInt(0),
                    intOrNull(channelReport, "rtt"), longOrNull(channelReport, "up"),
                    longOrNull(channelReport, "down"), longOrNull(channelReport, "buffered"),
                    textOrNull(channelReport, "pcState"), textOrNull(channelReport, "iceState"),
                    textOrNull(channelReport, "netPath"),
                    textOrNull(channelReport, "candLocal"), textOrNull(channelReport, "candRemote")));
        }
        channelSnapReady.emit(sourceHostNum, channels);
    }
    private static Integer intOrNull(JsonNode node, String field)
    {return node.hasNonNull(field) ? node.path(field).asInt() : null;}
    private static Long longOrNull(JsonNode node, String field)
    {return node.hasNonNull(field) ? node.path(field).asLong() : null;}
    private static String textOrNull(JsonNode node, String field)
    {
        JsonNode v = node.path(field);
        return v.isTextual() ? v.asText() : null;
    }
}