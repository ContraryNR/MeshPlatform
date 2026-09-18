package com.contrarynr.mesh.statsWorker;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;
import tools.jackson.databind.ObjectMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

//下发给客户端的统计上报配置(开关/周期/字段组)的唯一载体,与 C++ 端 reportconfig.h 严格一一对应。
//本类是 stats 域**全局单例**:既持有一份上报契约,也持有同源的服务端判活阈值 TTL(见 ttlMs) ——
//全局单例容器里的 @Component 本身就全局唯一,任何人要用都从这里取,不让 TTL 散落到别的 worker 自己算。
//带状态的可变 bean,运行时改配置直接改字段,读取方拿到的是当前值(自洽,无跨字段撕裂)。
//未加 @Configuration:它是"持有状态的单例组件",不是"定义 bean 的配置类",@Component 语义更贴切。
@Component
public class statsConfig
{
    //字段组白名单;顺序即管理页复选框渲染顺序,必须是有序不可变集合(Set.of 会随 JVM 随机盐每次启动乱序)
    public static final Set<String> ALLOWED_FIELDS =
            Collections.unmodifiableSet(new LinkedHashSet<>(List.of("rtt", "traffic", "buffered", "pcState", "iceState", "path")));
    private final ObjectMapper mapper;//打包 config JSON 用(见 configMsgPacker)
    private final int ttlFactor;//判活倍数:上报周期 × 该倍数 = 判活阈值(全局同源,改周期 TTL 自动跟随)
    private volatile boolean enabled;
    private volatile int intervalMs;
    private volatile Set<String> fields;
    public statsConfig(@Value("${mesh.stats.enabled:true}") boolean enabled,
            @Value("${mesh.stats.interval-ms:5000}") int intervalMs,
            @Value("${mesh.stats.fields:rtt,traffic,buffered,pcState,iceState,path}") String fields,
            @Value("${mesh.stats.ttl-factor:3}") int ttlFactor,
            ObjectMapper mapper)
    {this.enabled = enabled;this.intervalMs = intervalMs;this.fields = parseFields(fields);this.ttlFactor = ttlFactor;this.mapper = mapper;}

    public boolean enabled(){return enabled;}
    public int intervalMs(){return intervalMs;}
    public Set<String> fields(){return fields;}
    //判活阈值 = 上报周期 × ttl-factor:全局唯一出口,判活/展示都从这取
    public long ttlMs(){return (long) intervalMs * ttlFactor;}
    public synchronized void set(boolean enabled, int intervalMs, Set<String> fields)
    {this.enabled = enabled;this.intervalMs = intervalMs;this.fields = Collections.unmodifiableSet(new LinkedHashSet<>(fields));}

    //配置打包:把 config 相关字段封进 json(不含 type/target,由信令层信封补) —— 下发/回显共用
    public ObjectNode configMsgPacker()
    {
        ObjectNode cfg = mapper.createObjectNode();
        cfg.put("enabled", enabled);
        cfg.put("interval", intervalMs);
        ArrayNode flds = cfg.putArray("fields");
        for (String f : fields)
            flds.add(f);
        return cfg;
    }

    //文本 -> 有序集合(容忍空白与重复;只服务 application.properties 那条路,HTTP 请求体已是 JSON 数组)
    public static Set<String> parseFields(String s)
    {
        Set<String> set = new LinkedHashSet<>();
        for (String p : s.split(","))
            if (!p.isBlank())
                set.add(p.trim());
        return set;
    }
}