package com.contrarynr.mesh;

import com.contrarynr.mesh.repository.PeerStatAggregateRepository;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;
import tools.jackson.databind.JsonNode;

import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/*管理面接口 —— 拓扑/连接质量的查询与实时推送,给管理页面(及未来的 AI Agent)使用。

Spring 语义速查:
 - SseEmitter 返回值:告诉 MVC"这个响应是异步流",方法返回后连接保持,由 SsePushService 续写
 - produces 文本流:text/event-stream 是 SSE 的媒体类型(不写也能协商出来,写明更直白)*/
@RestController
@RequestMapping("/stats")
public class StatsController {
    private final StatsConfigManager configManager;//配置的查询与调整
    private final SsePushService ssePush;
    private final PeerStatAggregateRepository aggregateRepo;
    private final SignalingWebSocketHandler signalingHandler;//改配置后向各在线客户端重新下发的通道
    private final EdgeSnapRepository repo;//查询路径:现场取切面
    private final TopologyPacker packer;//切面 → 拓扑 JSON
    private final TopologyDistributor distributor;//拓扑负载的出口
    public StatsController(StatsConfigManager configManager, SsePushService ssePush,
    PeerStatAggregateRepository aggregateRepo, SignalingWebSocketHandler signalingHandler,
    EdgeSnapRepository repo, TopologyPacker packer, TopologyDistributor distributor)
    {this.configManager = configManager;this.ssePush = ssePush;this.aggregateRepo = aggregateRepo;
        this.signalingHandler = signalingHandler;this.repo = repo;this.packer = packer;this.distributor = distributor;}
    //聚合行 DTO(窗口内每条有向边每通道一行)—— 只给 RTT 与样本数,不给区间流量(理由见 PeerStatAggregate 类注释)
    public record AggregateInfo(int sourceHostNum, int peerHostNum, int channel, long windowStart,
        double avgRtt, int sampleCount) {}
    //当前拓扑+质量快照: GET /stats/latest —— 查询要"此刻",故现场取切面现打(与推流共用同一套打包规则)
    @GetMapping("/latest")
    public JsonNode latest() {
        return packer.pack(repo.snapshot());
    }
    //历史聚合查询: GET /stats/history?minutes=5(默认近 5 分钟)
    @GetMapping("/history")
    public List<AggregateInfo> history(@RequestParam(defaultValue = "5") int minutes)
    {
        long since = System.currentTimeMillis() - minutes * 60_000L;
        return aggregateRepo.findByWindowStartGreaterThanEqualOrderByWindowStartAsc(since)
                .stream()
                .map(a -> new AggregateInfo(a.getSourceHostNum(), a.getPeerHostNum(), a.getChannel(),
                        a.getWindowStart(), a.getAvgRtt(), a.getSampleCount()))
                .toList();
    }
    //SSE 实时推送: GET /stats/stream —— 事件名 topology,数据为快照 JSON 字符串
    @GetMapping(value = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter stream()
    {
        SseEmitter emitter = ssePush.register();
        //连接建立即补一份最近快照,新页面不用干等下个推流周期
        distributor.sendLatest(emitter);
        return emitter;
    }
    //当前统计上报配置: GET /stats/config —— server 为唯一配置源,客户端上报开关/周期/通道/字段由它统一下发
    @GetMapping("/config")
    public Map<String, Object> config()
    {
        ReportConfig rc = configManager.reportConfig();
        Map<String, Object> cfg = new LinkedHashMap<>();//有序:前端/调试时字段顺序稳定
        cfg.put("enabled", rc.enabled());
        cfg.put("interval", rc.intervalMs());
        cfg.put("fields", rc.fields());
        cfg.put("ttl", configManager.ttlMs());
        cfg.put("allowedFields", ReportConfig.ALLOWED_FIELDS);
        return cfg;
    }
    //配置更新请求体(JSON):fields 就是真正的字符串数组,不必再让人拼逗号串、也不会有"逗号后面多个空格"这类噪声
    //  {"enabled":true,"interval":5000,"fields":["rtt","traffic"]}
    //fields 缺省(null)= 保持原值;给空数组 = 清空(表示"裸边",只报边的存在性)
    //fields 声明成 LinkedHashSet 而不是 Set:Jackson 对 Set 默认绑成 HashSet(哈希序),回显顺序会莫名其妙地跳;
    //LinkedHashSet 才能保住书写顺序(与原先 parseFields 的行为一致)
    public record ConfigUpdate(Boolean enabled, Integer interval, LinkedHashSet<String> fields) {}
    //运行时调整统计上报配置并立即重新下发
    @PostMapping("/config")
    public Map<String, Object> updateConfig(@RequestBody ConfigUpdate req)
    {
        //请求体里漏写这两个必填项时,给一句能看懂的话,而不是让 boolean/int 静默变成 false/0
        if (req.enabled() == null || req.interval() == null)
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "请求体需含 enabled 与 interval");
        //下限防刷(1000ms),上限防"拓扑看起来像死了"(60000ms)
        if (req.interval() < 1000 || req.interval() > 60000)
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "interval 需在 [1000,60000] ms");
        Set<String> fSet = req.fields() != null ? req.fields() : configManager.reportConfig().fields();
        for (String f : fSet)
            if (!ReportConfig.ALLOWED_FIELDS.contains(f))
                throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "不支持的字段组: " + f);
        configManager.setReportConfig(req.enabled(), req.interval(), fSet);
        signalingHandler.broadcastStatsCfg();
        return config();
    }
}
