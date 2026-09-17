package com.contrarynr.mesh.repository;

import com.contrarynr.mesh.entity.PeerStatRecord;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.util.List;

//明细表访问接口 —— Spring Data JPA 按方法名派生 SQL,聚合/清理用 JPQL 手写。
public interface PeerStatRecordRepository extends JpaRepository<PeerStatRecord, Long> {
    /*按窗口聚合:每条有向边(source->peer->通道)一行,返回 [source, peer, channel, avgRtt, count]。
    avg(rtt) 自动忽略 null 样本(建连早期无 RTT)。
    不聚合 up/down:它们是 B/s 瞬时速率,窗口内求和既不是字节量、也不随配置稳定(见 PeerStatAggregate 类注释)。*/
    @Query("select r.sourceHostNum, r.peerHostNum, r.channel, avg(r.rtt), count(r) "
            + "from PeerStatRecord r where r.ts >= :start and r.ts < :end "
            + "group by r.sourceHostNum, r.peerHostNum, r.channel")
    List<Object[]> aggregateWindow(@Param("start") long start, @Param("end") long end);
    //删除过期明细(保留 24h) —— @Modifying 需在事务内调用
    @Modifying
    @Query("delete from PeerStatRecord r where r.ts < :deadline")
    int deleteOlderThan(@Param("deadline") long deadline);
}
