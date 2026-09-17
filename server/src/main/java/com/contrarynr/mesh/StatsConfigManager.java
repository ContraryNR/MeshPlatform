package com.contrarynr.mesh;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.util.Set;

/*统计行为管控 —— 全部客户端 stats 行为的唯一决策点:汇报开关/周期/字段组(即下发给客户端的 ReportConfig),
以及服务端据此派生的判活口径 TTL。

为什么要单独一个类,而不是让摄入侧顺手管着:这两件事必须同源。TTL = 上报周期 × 倍率,
若周期归 A、TTL 归 B,就留下"改了周期忘了改判活阈值"的静默耦合 —— 客户端被要求 30s 一报、
服务端仍按 15s 判死,拓扑会一直闪。放在一处,"周期变了 TTL 跟着变"就是一行方法的事。

不采用静态全局共享:Spring 容器里的单例 bean 本身就是全局唯一实例,改用 static 字段反而绕开容器 ——
注入不进(只能手写初始化、依赖顺序自己保证)、测试没法替换、生命周期不受容器管;
而显式注入的依赖是画得出来、查得出来的。

边界:本类只管"决策与持有",不管下发动作 —— 下行仍走信令层(SignalingWebSocketHandler 注册时下发、
改配置后由 StatsController 触发 broadcastStatsCfg),本类不认识 WebSocket。*/
@Component
public class StatsConfigManager {
    private final int ttlFactor;
    public StatsConfigManager(
    @Value("${mesh.stats.enabled:true}") boolean statsEnabled,
    @Value("${mesh.stats.interval-ms:5000}") int reportIntervalMs,
    @Value("${mesh.stats.fields:rtt,traffic,buffered,state,ice,path}") String fields,
    @Value("${mesh.stats.ttl-factor:3}") int ttlFactor)
    {this.ttlFactor = ttlFactor;
        this.reportCfg = new ReportConfig(statsEnabled, reportIntervalMs, ReportConfig.parseFields(fields));}
    //当前生效的客户端上报配置 —— 起底于 application.properties,运行时可经 /stats/config 调整;
    //整体替换(ReportConfig 不可变),故读取方拿到的总是一份自洽的配置,不会出现"新周期配旧字段"的撕裂
    private volatile ReportConfig reportCfg;

    public ReportConfig reportConfig()
    {return reportCfg;}
    //运行时改配置(管理接口调用),下发动作由调用方随后触发
    public void setReportConfig(boolean enabled, int intervalMs, Set<String> fields)
    {reportCfg = new ReportConfig(enabled, intervalMs, fields);}
    //判活阈值 = 上报周期 × 倍率(默认 3×5s=15s):容错 3 个周期内丢 2 次上报。
    //用倍率而非固定值,是为了让它跟着周期自动走(理由见类注释)。
    //它是 server"多久没上报算掉线"的判据,不下发给客户端,故不归 ReportConfig。
    public long ttlMs()
    {return (long) reportCfg.intervalMs() * ttlFactor;}
}
