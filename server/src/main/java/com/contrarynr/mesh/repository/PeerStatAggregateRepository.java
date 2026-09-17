package com.contrarynr.mesh.repository;

import com.contrarynr.mesh.entity.PeerStatAggregate;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;

public interface PeerStatAggregateRepository extends JpaRepository<PeerStatAggregate, Long> 
{
    Optional<PeerStatAggregate> findBySourceHostNumAndPeerHostNumAndChannelAndWindowStart(
            int sourceHostNum, int peerHostNum, int channel, long windowStart);
    List<PeerStatAggregate> findByWindowStartGreaterThanEqualOrderByWindowStartAsc(long since);
}
