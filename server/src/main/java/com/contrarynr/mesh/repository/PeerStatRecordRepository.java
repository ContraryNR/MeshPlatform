package com.contrarynr.mesh.repository;

import com.contrarynr.mesh.entity.PeerStatRecord;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.util.List;

public interface PeerStatRecordRepository extends JpaRepository<PeerStatRecord, Long> 
{
    @Query("select r.sourceHostNum, r.peerHostNum, r.channel, avg(r.rtt), count(r) "
            + "from PeerStatRecord r where r.ts >= :start and r.ts < :end "
            + "group by r.sourceHostNum, r.peerHostNum, r.channel")
    List<Object[]> aggregateWindow(@Param("start") long start, @Param("end") long end);
    @Modifying
    @Query("delete from PeerStatRecord r where r.ts < :deadline")
    int deleteOlderThan(@Param("deadline") long deadline);
}
