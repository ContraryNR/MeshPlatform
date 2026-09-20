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

@Component
public class statsConfig
{
    //字段组白名单;顺序即管理页复选框渲染顺序,必须是有序不可变集合(Set.of 会随 JVM 随机盐每次启动乱序)
    public static final Set<String> ALLOWED_FIELDS =
            Collections.unmodifiableSet(new LinkedHashSet<>(List.of("rtt", "traffic", "buffered", "pcState", "iceState", "path")));
    private final ObjectMapper mapper;//打包 config JSON 用(见 configMsgPacker)
    private volatile boolean enabled;
    private volatile int intervalMs;
    private volatile Set<String> fields;
    public statsConfig(@Value("${mesh.stats.enabled:true}") boolean enabled,
            @Value("${mesh.stats.interval-ms:5000}") int intervalMs,
            @Value("${mesh.stats.fields:rtt,traffic,buffered,pcState,iceState,path}") String fields,
            ObjectMapper mapper)
    {this.enabled = enabled;this.intervalMs = intervalMs;this.fields = parseFields(fields);this.mapper = mapper;}

    public boolean enabled(){return enabled;}
    public int intervalMs(){return intervalMs;}
    public Set<String> fields(){return fields;}
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