package com.contrarynr.mesh.statsWorker;
import com.contrarynr.mesh.SignalingWebSocketHandler;
import com.github.msteinbeck.sig4j.Type;
import jakarta.annotation.PostConstruct;
import org.springframework.stereotype.Component;

@Component
public class StatsCoordinator
{
    private final SignalingWebSocketHandler signalingHandler;
    private final StatsMsgTranslator translator;
    private final EdgeSnapRepository repo;
    private final TopologyTransformer transformer;
    private final TopologyBuffer buffer;
    private final SsePushService ssePush;
    public StatsCoordinator(SignalingWebSocketHandler signalingHandler, StatsMsgTranslator translator,
            EdgeSnapRepository repo, TopologyTransformer transformer, TopologyBuffer buffer, SsePushService ssePush)
    {this.signalingHandler = signalingHandler;this.translator = translator;this.repo = repo;
        this.transformer = transformer;this.buffer = buffer;this.ssePush = ssePush;}
    @PostConstruct
    void wire()
    {
        //信令层判合法 → 翻译层 → 仓储增量更新 / 节点下线摘除
        signalingHandler.statsMsgReceived.connect(translator.onRawStats, Type.DIRECT);
        translator.channelSnapReady.connect(repo.onWrite, Type.DIRECT);
        signalingHandler.peerDisconnected.connect(repo.onRemoveNode, Type.DIRECT);
            //这里`收信处`-Coor中继->`仓库`
                //绕过了预期的`信息处理层`(原本预期大致叫`msgHandler`,Agent实际命名为`translator`) 但是问题不大
        //切面就绪 → 打包 → 缓存 + 广播
        repo.snapshotReady.connect(transformer.onSnapshotAccept, Type.DIRECT);
        transformer.topologyReady.connect(buffer.onTopologyAccept, Type.DIRECT);
        transformer.topologyReady.connect(ssePush.onTopologyAccept, Type.DIRECT);
    }
}