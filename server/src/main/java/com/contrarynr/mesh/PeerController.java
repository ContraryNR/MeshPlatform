package com.contrarynr.mesh;

import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;

import java.util.List;

/*REST 查询接口 —— C++ 侧没有直接对应物(原来全部走 TCP 自定义 JSON 消息)。
这是给"外部管理界面 / AI Agent function calling"准备的 HTTP 入口。

Spring 语义速查:
 - @RestController: 类中每个方法的返回值直接序列化为 JSON 写进 HTTP 响应体
 - PeerRegistry 通过构造器注入 —— 注意整个文件没有 new PeerRegistry()
 - record: Java 的不可变数据载体, 天然可序列化为 JSON, 相当于你手拼 QJsonObject 的类型安全版*/
@RestController
@RequestMapping("/peers")
public class PeerController {
    private final PeerRegistry registry;
    //构造器注入: 容器看到这个构造器, 自动把 PeerRegistry 的单例塞进来
    public PeerController(PeerRegistry registry) {this.registry = registry;}
    public record PeerInfo(String hostName, int hostNum) {}
    public record RegisterRequest(String hostName) {}
    //查询全部已注册 peer: GET /peers
    @GetMapping
    public List<PeerInfo> list()
    {
        return registry.snapshot().entrySet().stream()
                .map(e -> new PeerInfo(e.getKey(), e.getValue()))
                .toList();
    }
    /*注册 peer: POST /peers, body: {"hostName":"alice"}
    行为等同原协议中 type=="hostname" 消息的处理: 哈希 -> 冲突解决 -> 入表*/
    @PostMapping
    public PeerInfo register(@RequestBody RegisterRequest req)
    {
        if (req.hostName() == null || req.hostName().isBlank())
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "hostName 不能为空");
        int num = registry.register(req.hostName());
        return new PeerInfo(req.hostName(), num);
    }
    //按主机号反查: GET /peers/3, 查不到返回 404
    @GetMapping("/{hostNum}")
    public PeerInfo byNum(@PathVariable int hostNum)
    {
        String name = registry.hostNameFor(hostNum);
        if (name == null)
            throw new ResponseStatusException(HttpStatus.NOT_FOUND, "hostNum " + hostNum + " 未注册");
        return new PeerInfo(name, hostNum);
    }
}
