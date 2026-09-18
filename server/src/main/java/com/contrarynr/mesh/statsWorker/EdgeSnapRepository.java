package com.contrarynr.mesh.statsWorker;
import com.contrarynr.mesh.entity.PeerStatAggregate;
import com.contrarynr.mesh.entity.PeerStatRecord;
import com.contrarynr.mesh.repository.PeerStatAggregateRepository;
import com.contrarynr.mesh.repository.PeerStatRecordRepository;
import com.github.msteinbeck.sig4j.signal.Signal1;
import com.github.msteinbeck.sig4j.slot.Slot1;
import com.github.msteinbeck.sig4j.slot.Slot2;
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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NavigableSet;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.TreeSet;

//自管理仓库:内存快照(节点心跳 + 边 × 两端 × 通道)、明细落库、分钟聚合、24h 清理,外加自己的 tick。
//读切面不直接推给下游 —— 就绪后发 snapshotReady 信号,由协调者经信号槽连到打包链(本类不再认识下游)。
@Component
public class EdgeSnapRepository
{
    //Repository
    private final PeerStatRecordRepository recordRepo;
    private final PeerStatAggregateRepository aggregateRepo;
    //ttlProvider:判活阈值不用自己算,全局单例 statsConfig 统一给(上报周期 × ttl-factor)
    private final statsConfig config;
    public EdgeSnapRepository(PeerStatRecordRepository recordRepo, PeerStatAggregateRepository aggregateRepo,
            statsConfig config)
    {this.recordRepo = recordRepo;this.aggregateRepo = aggregateRepo;this.config = config;}
    //上游摄入槽:协议翻译在 StatsMsgTranslator,仓库只认"摄入项 List<channelSnap>"(根本不认识 JSON)
    public final Slot2<Integer, List<channelSnap>> onWrite = this::deltaUpdate;
    public final Slot1<Integer> onRemoveNode = this::removeNode;

    //切面就绪:推流由协调者经信号连到打包链,仓库不再认识下游
    public final Signal1<Snapshot> snapshotReady = new Signal1<>();

    //updateSymbol
    private volatile boolean dirty = true;
    //edgeSymbol
    record edgeSymbol(int a, int b)
    {edgeSymbol{if(a > b){int t = a;a = b;b = t;}}}

    //onlineHosts
    private final Map<Integer, Long> nodeLastSeen = new HashMap<>();
    //onlineEdges
    private final Map<edgeSymbol, Map<Integer, Map<Integer, channelSnap>>> edges = new HashMap<>();
    //TwoInOne Snapshot
    record Snapshot(long ts,
                    //onlineHosts
                    List<Integer> nodes,
                    //onlineEdges
                    Map<edgeSymbol, SortedMap<Integer, Map<Integer, channelSnap>>> edges) {}

    //Other
    private static final Logger log = LoggerFactory.getLogger(EdgeSnapRepository.class);

    //channelSnap:摄入项(不可变;hasAnyAttr 判裸边 —— 全质量字段皆缺省即裸边,同时服务"要不要入库"与"要不要进拓扑")
    record channelSnap(long ts, int peer, int ch,
        //channelAttr
        Integer rtt, Long up, Long down, Long buffered,
        String pcState, String iceState, String netPath,
        //candidate(noStorage)
        String candLocal, String candRemote)
    {
        boolean expired(long now, long ttlMs){return now - ts > ttlMs;}
        boolean hasAnyAttr()
        {return rtt != null || up != null || down != null || buffered != null
                || pcState != null || iceState != null || netPath != null;}
    }

    //periodicChecker(checkTTL+dirty)
    @Scheduled(fixedDelay = 1000)
    public void tick()
    {
        sweep(System.currentTimeMillis());
        if (!dirty)
            return;//没脏就不推
        dirty = false;
        snapshotReady.emit(snapshot());
    }
    private synchronized void sweep(long now)
    {
        long ttlMs = config.ttlMs();
        if (nodeLastSeen.entrySet().removeIf(e -> now - e.getValue() > ttlMs))
            dirty = true;
        for (Iterator<Map.Entry<edgeSymbol, Map<Integer, Map<Integer, channelSnap>>>> it = edges.entrySet().iterator(); it.hasNext(); )
        {
            Map<Integer, Map<Integer, channelSnap>> channelsPeerGroup = it.next().getValue();
            if (channelsPeerGroup.values().removeIf(chs ->
            {
                chs.values().removeIf(d -> d.expired(now, ttlMs));
                return chs.isEmpty();
            }))
                dirty = true;
            if (channelsPeerGroup.isEmpty())
                it.remove();
        }
    }

    //hostRemover
    public synchronized void removeNode(int hostNum)
    {
        boolean changed = (nodeLastSeen.remove(hostNum) != null);
        for (Iterator<Map<Integer, Map<Integer, channelSnap>>> it = edges.values().iterator(); it.hasNext(); )
        {
            Map<Integer, Map<Integer, channelSnap>> channelsPeerGroup = it.next();
            Map<Integer, channelSnap> singleChannel = channelsPeerGroup.remove(hostNum);
            if (singleChannel != null && !singleChannel.isEmpty())
                changed = true;
            if (channelsPeerGroup.isEmpty())//若该边双边均被移除
                it.remove();//则移除该边edgeSymbol
        }
        if (changed)
            dirty = true;
        log.info("[Stats] 节点 hostNum={} 下线,快照已清理", hostNum);
    }

    //deltaUpdate:上游一条通道报告进来即"增量更新" —— 刷新内存快照(心跳/边/两端通道),裸边也占位,
    //有质量属性的才顺便落明细;然后由 onWrite 槽挂到协调者连线。原 write+apply 已并入此处。
    public synchronized void deltaUpdate(int source, List<channelSnap> channels)
    {
        long now = System.currentTimeMillis();
        if (nodeLastSeen.put(source, now) == null)
            dirty = true;//新节点进入在线列表:呈现内容变了
        List<PeerStatRecord> records = new ArrayList<>();
        for (channelSnap snap : channels)
        {
            edges.computeIfAbsent(new edgeSymbol(source, snap.peer()), unused -> new HashMap<>())
                    .computeIfAbsent(source, unused -> new HashMap<>())
                    .put(snap.ch(), snap);
            dirty = true;//报告本身是新采样(ts 与速率都在动),页面该跟着走
            if (snap.hasAnyAttr())
                records.add(statsTool.toRecord(source, snap));
        }
        if (!records.isEmpty())
            recordRepo.saveAll(records);
        log.debug("[Stats] hostNum={} 上报 {} 条通道", source, channels.size());
    }

    //readOnly EdgesSnapshot Getter
    public synchronized Snapshot snapshot()
    {
        long now = System.currentTimeMillis();
        long ttlMs = config.ttlMs();
        //onlineNodes
        List<Integer> nodes = new ArrayList<>();
        for (Map.Entry<Integer, Long> e : new TreeMap<>(nodeLastSeen).entrySet())
            if (now - e.getValue() <= ttlMs)
                nodes.add(e.getKey());//已超 TTL 未上报:视为离线,不入切面
        //onlineChannels(全部peer*全部channel->双端完整数据)
        Map<edgeSymbol, SortedMap<Integer, Map<Integer, channelSnap>>> alive = new HashMap<>();
        for (Map.Entry<edgeSymbol, Map<Integer, Map<Integer, channelSnap>>> entry : edges.entrySet())
        {
            edgeSymbol k = entry.getKey();
            Map<Integer, Map<Integer, channelSnap>> channelsPeerGroup = entry.getValue();
            //两端各自的存活观测(任一端可能是空表:该端没上报或已离线)
            Map<Integer, channelSnap> fromA = new HashMap<>();
            for (Map.Entry<Integer, channelSnap> c : channelsPeerGroup.getOrDefault(k.a(), Map.of()).entrySet())
                if (!c.getValue().expired(now, ttlMs))
                    fromA.put(c.getKey(), c.getValue());
            Map<Integer, channelSnap> fromB = new HashMap<>();
            for (Map.Entry<Integer, channelSnap> c : channelsPeerGroup.getOrDefault(k.b(), Map.of()).entrySet())
                if (!c.getValue().expired(now, ttlMs))
                    fromB.put(c.getKey(), c.getValue());
            if (fromA.isEmpty() && fromB.isEmpty())
                continue;//仅当两个节点的通道均全部过期时才过滤掉整条边
            //通道号 = 两端任一上报过即算,升序合并去重(某端独报的通道也要保留,另一端缺省落"—")
            SortedMap<Integer, Map<Integer, channelSnap>> chs = new TreeMap<>();
            NavigableSet<Integer> chNums = new TreeSet<>(fromA.keySet());
            chNums.addAll(fromB.keySet());
            for (int ch : chNums)
            {
                Map<Integer, channelSnap> pair = new LinkedHashMap<>();
                channelSnap sa = fromA.get(ch);
                if (sa != null && sa.hasAnyAttr())
                    pair.put(k.a(), sa);
                channelSnap sb = fromB.get(ch);
                if (sb != null && sb.hasAnyAttr())
                    pair.put(k.b(), sb);
                if (!pair.isEmpty())//两端都没内容(如字段组未配置)则整条通道不输出
                    chs.put(ch, Collections.unmodifiableMap(pair));
            }
            alive.put(k, Collections.unmodifiableSortedMap(chs));
        }
        return new Snapshot(now, List.copyOf(nodes), Map.copyOf(alive));
    }

    //periodicAggregater - sql
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
    //periodicOldRangeCleaner - sql
    @Scheduled(cron = "0 0 * * * *")
    @Transactional //@Modifying 删除必须包在事务里
    public void cleanup()
    {
        int removed = recordRepo.deleteOlderThan(System.currentTimeMillis() - 24 * 3600_000L);
        if (removed > 0)
            log.info("[Stats] 清理过期明细 {} 条", removed);
    }
}