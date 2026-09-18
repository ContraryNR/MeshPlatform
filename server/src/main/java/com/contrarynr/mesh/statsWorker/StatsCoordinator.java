package com.contrarynr.mesh.statsWorker;
import com.contrarynr.mesh.SignalingWebSocketHandler;
import com.github.msteinbeck.sig4j.Type;
import jakarta.annotation.PostConstruct;
import org.springframework.stereotype.Component;

//stats 域的"统领全部信息流转链路"的独立类(类比客户端 MainWindow):本身**不处理任何数据**,只做中转/接线。
//全部 worker 通过"信号 → 槽"互连,连线统一下在本类;谁触发谁、触发后往哪流,只看 wire() 一处。
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
    //全部用直连同步,与 Qt 的 DirectConnection 对应:触发方同线程顺序执行。本类只 connect,不发信号、不加工数据。
    @PostConstruct
    void wire()
    {
        //信令层判合法 → 翻译层 → 仓储增量更新 / 节点下线摘除
        signalingHandler.statsMsgReceived.connect(translator.onRawStats, Type.DIRECT);
        translator.channelSnapReady.connect(repo.onWrite, Type.DIRECT);
        signalingHandler.peerDisconnected.connect(repo.onRemoveNode, Type.DIRECT);
        //切面就绪 → 打包 → 缓存 + 广播
        repo.snapshotReady.connect(transformer.onSnapshotAccept, Type.DIRECT);
        transformer.topologyReady.connect(buffer.onTopologyAccept, Type.DIRECT);
        transformer.topologyReady.connect(ssePush.onTopologyAccept, Type.DIRECT);
    }
}