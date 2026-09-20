package com.contrarynr.mesh.statsWorker;
import com.github.msteinbeck.sig4j.slot.Slot1;
import org.springframework.stereotype.Component;
import tools.jackson.databind.node.ObjectNode;

@Component
public class TopologyBuffer
{
    private volatile String latest;
    //Setter / jsonNode->jsonString->topologyBuffer
    public final Slot1<ObjectNode> onTopologyAccept = node -> latest = node.toString();
    //Getter
    public String latest(){return latest;}
}