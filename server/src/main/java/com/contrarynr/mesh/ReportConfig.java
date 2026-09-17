package com.contrarynr.mesh;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;

/*统计上报配置 —— server 是唯一配置源,客户端不保留任何本地调控入口
(见 agents.md 的"避免同一 edge 两端信息不对等")。
边界:本类只装"要下发、用于约束客户端上报行为"的东西,与 C++ 端 reportconfig.h 的成员一一对应;
server 自己的判活口径(ttl)不属于这里 —— 它不下发给客户端,混进来会让人误以为改它能左右客户端行为,
故归 StatsConfigManager.ttlMs。

不可变:运行时改配置是整体替换引用(StatsConfigManager 持有 volatile 引用),
故读取方拿到的永远是一份自洽的配置,不会出现"新周期配旧字段"这种跨字段撕裂。

组件含义:
  enabled    上报总开关
  intervalMs 上报周期(ms) —— 客户端的采集定时器按它触发
  fields     要上报的字段组集合(空集 = 裸边,只报边的存在性)

客户端始终上报全部通道(0主/1文件/2音频/3视频):"按通道裁剪"这一层已移除 ——
指标采集本身已被 fields 逐个门控,再叠一层"报哪几条通道"收益太低,只增加配置面。

record 会自动生成:构造器、与组件同名的 3 个访问器(如 enabled())、equals/hashCode/toString。*/
public record ReportConfig(boolean enabled, int intervalMs, Set<String> fields)
{
    public static final Set<String> ALLOWED_FIELDS = Set.of("rtt", "traffic", "buffered", "state", "ice", "path");
    public ReportConfig
    {
        fields = Collections.unmodifiableSet(new LinkedHashSet<>(fields));
    }
    public static Set<String> parseFields(String s)
    {
        Set<String> set = new LinkedHashSet<>();
        for (String p : s.split(","))
            if (!p.isBlank())
                set.add(p.trim());
        return set;
    }
    //Question:所以解析上报的边和字段的意义是什么呢?关键难道不应该在于上报的k/v的v吗?
}
