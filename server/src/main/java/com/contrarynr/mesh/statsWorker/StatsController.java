package com.contrarynr.mesh.statsWorker;
import com.contrarynr.mesh.SignalingWebSocketHandler;
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

@RestController
@RequestMapping("/stats")
public class StatsController
{
    private final statsConfig config;//配置的查询与调整(statsConfig bean 本体)
    private final SsePushService ssePush;
    private final PeerStatAggregateRepository aggregateRepo;
    private final SignalingWebSocketHandler signalingHandler;//改配置后向各在线客户端重新下发的通道
    private final EdgeSnapRepository repo;//查询路径:现场取切面
    private final TopologyTransformer transformer;//切面 → 拓扑 JSON
    private final TopologyBuffer buffer;//拓扑负载的出口(补发最近快照)
    public StatsController(statsConfig config, SsePushService ssePush,
    PeerStatAggregateRepository aggregateRepo, SignalingWebSocketHandler signalingHandler,
    EdgeSnapRepository repo, TopologyTransformer transformer, TopologyBuffer buffer)
    {this.config = config;this.ssePush = ssePush;this.aggregateRepo = aggregateRepo;
        this.signalingHandler = signalingHandler;this.repo = repo;this.transformer = transformer;this.buffer = buffer;}
    public record AggregateInfo(int sourceHostNum, int peerHostNum, int channel, long windowStart,
        double avgRtt, int sampleCount) {}
    //fields 缺省(null)= 保持原值;给空数组 = 清空(表示"裸边",只报边的存在性)
    public record ConfigUpdate(Boolean enabled, Integer interval, LinkedHashSet<String> fields) {}

    //当前详细edges快照查询
    @GetMapping("/latest")
    public JsonNode latest() {return transformer.pack(repo.snapshot());}

    //历史聚合查询 GET /stats/history?minutes=5(默认近 5 分钟)
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

    //SSE实时推送 GET /stats/stream
    @GetMapping(value = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter stream()
    {
        SseEmitter emitter = ssePush.register();
        buffer.sendLatest(emitter);//发生绑定(返回)前会作为Emitter`待办`(暂存内部队列)
        return emitter;//返回时Spring注意到返回类型为`SseEmitter`
                            //且返回值满足`未绑定过`的前提->绑定到Sse长连接Session
    }

    @GetMapping("/config")
    public Map<String, Object> config()
    {
        Map<String, Object> cfg = new LinkedHashMap<>();
        cfg.put("enabled", config.enabled());
        cfg.put("interval", config.intervalMs());
        cfg.put("fields", config.fields());
        cfg.put("ttl", config.ttlMs());
        cfg.put("allowedFields", statsConfig.ALLOWED_FIELDS);
        return cfg;
    }

    @PostMapping("/config")
    public Map<String, Object> updateConfig(@RequestBody ConfigUpdate req)
    {
        //请求体里漏写这两个必填项时,给一句能看懂的话,而不是让 boolean/int 静默变成 false/0
        if (req.enabled() == null || req.interval() == null)
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "请求体需含 enabled 与 interval");
        //下限防刷(1000ms),上限防"拓扑看起来像死了"(60000ms)
        if (req.interval() < 1000 || req.interval() > 60000)
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "interval 需在 [1000,60000] ms");
        Set<String> fSet = req.fields() != null ? req.fields() : config.fields();
        for (String f : fSet)
            if (!statsConfig.ALLOWED_FIELDS.contains(f))
                throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "不支持的字段组: " + f);
        config.set(req.enabled(), req.interval(), fSet);
        signalingHandler.broadcastStatsCfg();
        return config();
    }
}