package com.contrarynr.mesh;
import com.contrarynr.mesh.entity.PeerStatAggregate;
import com.contrarynr.mesh.entity.PeerStatRecord;
import com.contrarynr.mesh.repository.PeerStatAggregateRepository;
import com.contrarynr.mesh.repository.PeerStatRecordRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;
import org.springframework.transaction.annotation.Transactional;

import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedMap;
import java.util.SortedSet;
import java.util.TreeMap;
import java.util.TreeSet;

/*统计仓储 —— 自管理仓库:内存快照(节点心跳 + 边 × 两端 × 通道)、明细落库、分钟聚合、24h 清理,
外加自己的 tick:过期自检 + 脏了就推流。

命名:本类不是 JPA 仓储(repository 包下那批才是),故标 @Component;Repository 在这里指它在数据流里的
位置 —— 上游:被摄入侧写入,再自主把切面推给 packer。

"自管理"落在三处,一处都不外派:
 1. 过期事务:sweep 只由本类 tick 触发(读写分离的写侧),不由任何查询触发 —— 否则"多久清一次"
    会被管理页的轮询频率绑架;摘除后置脏,页面随之更新(客户端全部掉线、再无上报时也照样清理、照样上图)。
 2. 推流决策:dirty 由写入/断连/摘除置位,tick 见脏才推 —— 数据没脏推流没有意义;
    纯心跳(空 edges 且节点已知)不置脏,免得空闲节点把页面刷成心跳刷屏。
 3. tick 节拍 1s:既是"写入到上图"的延迟上限(≤1s),也是推流速率上限(同一拍内多个节点上报只推一次)。
判活 TTL 不在本类,而是每拍向 StatsConfigManager 取(见下),两者同源就不会出现"周期改了、判活口径没改"。

边界:本类只认语义化的摄入项(DirSnap),不认上报报文 —— 报文解析归摄入侧;
锁只保护内存快照(方法自带锁),明细落库与推流都在锁外,不让 SQLite 单写者或 SSE 拖住容器。*/
@Component
public class EdgeSnapRepository {
    private final PeerStatRecordRepository recordRepo;
    private final PeerStatAggregateRepository aggregateRepo;
    private final StatsConfigManager configManager;//判活 TTL 的政策来源
    private final TopologyPacker packer;//下游:把切面打包成可分发的形式
    public EdgeSnapRepository(PeerStatRecordRepository recordRepo, PeerStatAggregateRepository aggregateRepo,
    StatsConfigManager configManager, TopologyPacker packer)
    {this.recordRepo = recordRepo;this.aggregateRepo = aggregateRepo;
        this.configManager = configManager;this.packer = packer;}
    private static final Logger log = LoggerFactory.getLogger(EdgeSnapRepository.class);
    //节点心跳(hostNum → 最近归档时刻) = `在线主机列表`
    private final Map<Integer, Long> nodeLastSeen = new HashMap<>();
    //边快照:edge → 上报方 → 通道 → 方向观测
    private final Map<Edge, Map<Integer, Map<Integer, DirSnap>>> edges = new HashMap<>();
    //链路层级排序:relay(中继降级) > wan(公网) > lan(局域网),取最差值作判据
    private static final Map<String, Integer> NET_PATH_RANK = Map.of("lan", 0, "wan", 1, "relay", 2);
    //脏标记:本仓储关心的变化(写入/断连/摘除)才置位,推完清零。
    //单个布尔标记用 volatile 足够 —— 只要求可见性,不涉及"读-改-写"复合不变量(集合仍必须靠互斥锁)
    //初值为脏:启动后先宣告一次"当前是空的",页面接上就有一个确定的状态,不必干等第一个节点上报
    private volatile boolean dirty = true;

    //边身份(构造即 a<b 规范化):A 报"peer=B"与 B 报"peer=A"因此落进同一条边
    record Edge(int a, int b)
    {Edge{if(a > b){int t = a;a = b;b = t;}}}

    /*单端一次上报的通道观测(不可变):由摄入侧翻译好交来,本类只归档。
    ts 是归档用的到达时刻(过期判定只看它);缺省字段一律 null(未配置的字段组 / 客户端未上报),
    与"空闲连接的 0 速率/0 积压"这类真值区分。peer/ch 是它自己的定位(落在哪条边的哪个通道上)。*/
    record DirSnap(long ts, int peer, int ch, Integer rtt, Long up, Long down, Long buffered,
        String pcState, String iceState, String netPath, String candLocal, String candRemote)
    {
        boolean expired(long now, long ttlMs){return now - ts > ttlMs;}
        //有任一质量字段即非裸边(全缺省只表达拓扑存在性,无内容可入本地库)
        boolean hasAnyAttr()
        {return rtt != null || up != null || down != null || buffered != null
                || pcState != null || iceState != null || netPath != null;}
    }

    /*合并后的通道视图:同一通道两端两支观测归一后的结果。
    合并规则属于"数据聚合"而不是"呈现",故归本层,消费端拿到的就是可直接落笔的数据。
    不含 pcState —— 该字段只入库、没有拓扑展示位,带进来只会多出一个恒缺省的列。*/
    record MergedCh(int ch, Integer rtt, Long up, Long down, Long buffered,
        String netPath, String iceState, String candLocal, String candRemote)
    {
        //该通道是否有可展示内容:全缺省(如字段组未配置)则整条通道不进入切面
        boolean hasAnyData()
        {return rtt != null || up != null || down != null || buffered != null
                || iceState != null || netPath != null;}
    }

    //一致性切面:同一时刻的在线主机(升序) + 存活边(通道升序),一次加锁取齐,避免"边引用了未知节点"
    record Snapshot(long ts, List<Integer> nodes, Map<Edge, SortedMap<Integer, MergedCh>> edges) {}

    //摄入写:归档一批通道观测(锁内只动内存,明细落库在锁外)
    public void write(int source, List<DirSnap> channels)
    {
        List<PeerStatRecord> records = apply(source, channels, System.currentTimeMillis());
        if (!records.isEmpty())
            recordRepo.saveAll(records);//锁外落库:SQLite 单写者,不占容器锁
        log.debug("[Stats] hostNum={} 上报 {} 条通道", source, channels.size());
    }

    //节点下线(webSession 断连):节点即时消失(其对端报告留待 TTL 自然过期)
    public synchronized void removeNode(int hostNum)
    {
        boolean changed = nodeLastSeen.remove(hostNum) != null;
        for (Iterator<Map<Integer, Map<Integer, DirSnap>>> it = edges.values().iterator(); it.hasNext(); )
        {
            Map<Integer, Map<Integer, DirSnap>> sides = it.next();
            Map<Integer, DirSnap> gone = sides.remove(hostNum);//该端的 <通道, 观测> 表
            if (gone != null && !gone.isEmpty())
                changed = true;//那端的观测确实在图上,撤掉就是呈现变化
            if (sides.isEmpty())
                it.remove();
        }
        //"什么都没撤掉"就不该算脏:一个早已因 TTL 从图上消失的节点这时断连,内容并没有变化
        if (changed)
            dirty = true;
        log.info("[Stats] 节点 hostNum={} 下线,快照已清理", hostNum);
    }

    /*自有时钟(唯一的推流触发点):先自检过期,再在"脏"时把切面推给 packer。
    顺序讲究:先清脏标记再取切面 —— 清与取之间落进来的写入会重新置脏、由下一拍兜住,不会丢;
    若反过来(先取后清),那段时间的写入就可能被这一拍的清标记吞掉。*/
    @Scheduled(fixedDelay = 1000)
    public void tick()
    {
        sweep(System.currentTimeMillis());
        if (!dirty)
            return;//没脏就不推
        dirty = false;
        packer.deliver(snapshot());
    }

    /*读路径:过滤过期 + 合并两端观测,交出一致性切面。只读不改容器(摘除归 tick 的 sweep),
    交出的 List/Map 皆为新建的不可变副本,锁外可安全使用。*/
    public synchronized Snapshot snapshot()
    {
        long now = System.currentTimeMillis();
        long ttlMs = configManager.ttlMs();
        List<Integer> nodes = new ArrayList<>();
        for (Map.Entry<Integer, Long> e : new TreeMap<>(nodeLastSeen).entrySet())
            if (now - e.getValue() <= ttlMs)
                nodes.add(e.getKey());//已超 TTL 未上报:视为离线,不入切面
        Map<Edge, SortedMap<Integer, MergedCh>> alive = new HashMap<>();
        for (Map.Entry<Edge, Map<Integer, Map<Integer, DirSnap>>> entry : edges.entrySet())
        {
            Edge k = entry.getKey();
            Map<Integer, Map<Integer, DirSnap>> sides = entry.getValue();
            //两端各自的通道观测(任一端可能是空表:该端没上报或已离线)
            Map<Integer, DirSnap> fromA = fresh(sides.getOrDefault(k.a(), Map.of()), now, ttlMs);
            Map<Integer, DirSnap> fromB = fresh(sides.getOrDefault(k.b(), Map.of()), now, ttlMs);
            if (fromA.isEmpty() && fromB.isEmpty())
                continue;//仅当两个节点的通道均全部过期时才过滤掉整条边
            SortedMap<Integer, MergedCh> chs = new TreeMap<>();
            for (int ch : channelsOf(fromA.keySet(), fromB.keySet()))//两端任一上报过即算,升序
            {
                MergedCh c = merge(ch, fromA.get(ch), fromB.get(ch));
                if (c.hasAnyData())//该通道没有任何可展示内容(如字段组未配置)则整条通道不输出
                    chs.put(ch, c);
            }
            alive.put(k, Collections.unmodifiableSortedMap(chs));
        }
        return new Snapshot(now, List.copyOf(nodes), Map.copyOf(alive));
    }

    /*过期自检(唯一的摘除时机,只由 tick 调用):节点心跳与通道观测一次摘净。
    每次摘除都是呈现变化,故顺手置脏 —— "客户端全部掉线"本身就是需要推给页面的事件。*/
    private synchronized void sweep(long now)
    {
        long ttlMs = configManager.ttlMs();
        if (nodeLastSeen.entrySet().removeIf(e -> now - e.getValue() > ttlMs))
            dirty = true;
        for (Iterator<Map.Entry<Edge, Map<Integer, Map<Integer, DirSnap>>>> it = edges.entrySet().iterator(); it.hasNext(); )
        {
            Map<Integer, Map<Integer, DirSnap>> sides = it.next().getValue();
            if (sides.values().removeIf(chs ->
            {
                chs.values().removeIf(d -> d.expired(now, ttlMs));
                return chs.isEmpty();
            }))
                dirty = true;
            if (sides.isEmpty())
                it.remove();
        }
    }

    //锁内:刷新节点心跳 + 逐通道覆盖写入;顺带把"有内容"的观测转成明细行交回(供锁外落库)
    private synchronized List<PeerStatRecord> apply(int source, List<DirSnap> channels, long now)
    {
        if (nodeLastSeen.put(source, now) == null)
            dirty = true;//新节点进入在线列表:呈现内容变了
        List<PeerStatRecord> records = new ArrayList<>();
        for (DirSnap snap : channels)
        {
            edges.computeIfAbsent(new Edge(source, snap.peer()), unused -> new HashMap<>())
                    .computeIfAbsent(source, unused -> new HashMap<>())
                    .put(snap.ch(), snap);
            dirty = true;//报告本身是新采样(ts 与速率都在动),页面该跟着走
            if (snap.hasAnyAttr())
                records.add(toRecord(source, snap));
        }
        return records;
    }

    //观测 → 明细行(缺省字段原样落 null,与真 0 区分;ts 用观测的到达时刻)
    private static PeerStatRecord toRecord(int source, DirSnap s)
    {
        PeerStatRecord rec = new PeerStatRecord();
        rec.setSourceHostNum(source);
        rec.setPeerHostNum(s.peer());
        rec.setChannel(s.ch());
        rec.setRtt(s.rtt());
        rec.setUp(s.up());
        rec.setDown(s.down());
        rec.setBuffered(s.buffered());
        rec.setPcState(s.pcState());
        rec.setIceState(s.iceState());
        rec.setNetPath(s.netPath());
        rec.setTs(s.ts());
        return rec;
    }

    //单端观测过滤(getOrDefault 兜底:该端从未上报时视作空表)
    private static Map<Integer, DirSnap> fresh(Map<Integer, DirSnap> side, long now, long ttlMs)
    {
        Map<Integer, DirSnap> out = new HashMap<>();
        for (Map.Entry<Integer, DirSnap> c : side.entrySet())
            if (!c.getValue().expired(now, ttlMs))
                out.put(c.getKey(), c.getValue());
        return out;
    }

    //将两端通道号汇聚为一组(升序合并去重)
    private static SortedSet<Integer> channelsOf(Set<Integer> fromA, Set<Integer> fromB)
    {
        SortedSet<Integer> chs = new TreeSet<>(fromA);
        chs.addAll(fromB);
        return chs;
    }

    /*同一通道两端是对"同一物理量"的两支观测:x 为主、y 缺项时兜底;两端皆缺省则该项不输出(保持 null)。
    方向速率要跨端取镜像(我发 = 对端收)。*/
    private static MergedCh merge(int ch, DirSnap x, DirSnap y)
    {
        return new MergedCh(ch, mergeRtt(x, y),
                pickRate(x != null ? x.up() : null, y != null ? y.down() : null),
                pickRate(y != null ? y.up() : null, x != null ? x.down() : null),
                maxLong(x != null ? x.buffered() : null, y != null ? y.buffered() : null),
                worstNetPath(x != null ? x.netPath() : null, y != null ? y.netPath() : null),
                x != null ? x.iceState() : (y != null ? y.iceState() : null),
                x != null ? x.candLocal() : (y != null ? y.candLocal() : null),
                x != null ? x.candRemote() : (y != null ? y.candRemote() : null));
    }

    //RTT 合并:两端都有取平均,仅一端取该端,均无则 null
    private static Integer mergeRtt(DirSnap x, DirSnap y)
    {
        if (x != null && x.rtt() != null && y != null && y.rtt() != null)
            return (x.rtt() + y.rtt()) / 2;
        if (x != null && x.rtt() != null)
            return x.rtt();
        if (y != null && y.rtt() != null)
            return y.rtt();
        return null;
    }

    //取更差的链路层级(relay>wan>lan),null 视为无信息不参与
    private static String worstNetPath(String x, String y)
    {
        if (x == null) return y;
        if (y == null) return x;
        return NET_PATH_RANK.getOrDefault(x, 0) >= NET_PATH_RANK.getOrDefault(y, 0) ? x : y;
    }

    //方向速率的镜像回退:主观测缺省(null)时取对端的等价读数(我发=对端收),仍缺省则保持 null
    private static Long pickRate(Long primary, Long mirror)
    {return primary != null ? primary : mirror;}

    //可空求大:任一端缺省则取另一端,两端皆缺省才为 null(避免 0 冒充缺省)
    private static Long maxLong(Long x, Long y)
    {
        if (x == null) return y;
        if (y == null) return x;
        return x >= y ? x : y;
    }

    //每分钟第 10 秒:聚合上一分钟窗口的明细(错峰,避开整分前后的上报写入)
    @Scheduled(cron = "10 * * * * *")
    public void aggregate()
    {
        long minute = 60_000L;
        long windowEnd = System.currentTimeMillis() / minute * minute; //当前分钟起点 = 窗口结束
        long windowStart = windowEnd - minute;
        List<Object[]> rows = recordRepo.aggregateWindow(windowStart, windowEnd);
        for (Object[] row : rows)
        {
            int source = (int) row[0], peer = (int) row[1], channel = (int) row[2];
            Double avgRtt = (Double) row[3];//样本全为 null 时 avg 为 null
            Long count = (Long) row[4];
            //upsert:同(边,通道,窗口)重跑覆盖,聚合幂等
            PeerStatAggregate agg = aggregateRepo
                    .findBySourceHostNumAndPeerHostNumAndChannelAndWindowStart(source, peer, channel, windowStart)
                    .orElseGet(PeerStatAggregate::new);
            agg.setSourceHostNum(source);
            agg.setPeerHostNum(peer);
            agg.setChannel(channel);
            agg.setWindowStart(windowStart);
            agg.setAvgRtt(avgRtt != null ? avgRtt : 0);
            agg.setSampleCount(count != null ? count.intValue() : 0);
            aggregateRepo.save(agg);
        }
        if (!rows.isEmpty())
            log.info("[Stats] 聚合窗口 {} 完成,{} 条通道", Instant.ofEpochMilli(windowStart), rows.size());
    }

    //每小时:清理 24h 前的明细(聚合表体量小,长期保留)
    @Scheduled(cron = "0 0 * * * *")
    @Transactional //@Modifying 删除必须包在事务里
    public void cleanup()
    {
        int removed = recordRepo.deleteOlderThan(System.currentTimeMillis() - 24 * 3600_000L);
        if (removed > 0)
            log.info("[Stats] 清理过期明细 {} 条", removed);
    }
}
