package com.contrarynr.mesh.archive;
import com.contrarynr.mesh.PeerRegistry;
import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;

import java.util.List;

//已归档:register(POST)测试接口已移除 —— 主机号分配改由信令通道的 hostname 报文完成(见 SignalingWebSocketHandler.handleRegister)。
//仅保留 list/byNum 两类只读查询供调试查看;若不再需要,整个类可连同 @RestController 一并删除。
@RestController
@RequestMapping("/peers")
public class PeerController
{
    private final PeerRegistry registry;
    public PeerController(PeerRegistry registry) {this.registry = registry;}
    public record PeerInfo(String hostName, int hostNum) {}

    //onlineHostsSnapshot
    @GetMapping
    public List<PeerInfo> list()
    {
        return registry.snapshot().entrySet().stream()
                .map(e -> new PeerInfo(e.getKey(), e.getValue()))
                .toList();
    }

    //hostNum -> hostName
    @GetMapping("/{hostNum}")
    public PeerInfo byNum(@PathVariable int hostNum)
    {
        String name = registry.hostNameFor(hostNum);
        if (name == null)
            throw new ResponseStatusException(HttpStatus.NOT_FOUND, "hostNum " + hostNum + " 未注册");
        return new PeerInfo(name, hostNum);
    }
}