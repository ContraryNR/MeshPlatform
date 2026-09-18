package com.contrarynr.mesh.statsWorker;
import com.contrarynr.mesh.entity.PeerStatRecord;

//统计相关的无状态工具:摄入项 -> 明细实体(仓库落库用)
public final class statsTool
{
    private statsTool(){}
    public static PeerStatRecord toRecord(int source, EdgeSnapRepository.channelSnap s)
    {
        return new PeerStatRecord(source, s.peer(), s.ch(), s.rtt(), s.up(), s.down(),
                s.buffered(), s.pcState(), s.iceState(), s.netPath(), s.ts());
    }
}