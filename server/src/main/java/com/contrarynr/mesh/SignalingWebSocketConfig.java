package com.contrarynr.mesh;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * WebSocket 端点注册 —— 对应 C++ 侧 coornetworker::startTcpServer(ip, port)
 * 那一步"把服务器立起来"的动作。
 *
 * Spring 语义速查:
 *  - @Configuration: 声明"这个类里放的是配置代码", 容器启动时会执行它
 *  - addHandler: 把 /ws 路径交给 SignalingWebSocketHandler 处理
 *    (Qt 客户端将来连 ws://host:8080/ws)
 */
@Configuration
@EnableWebSocket
public class SignalingWebSocketConfig implements WebSocketConfigurer {

    private final SignalingWebSocketHandler handler;

    /** 老朋友: 构造器注入 */
    public SignalingWebSocketConfig(SignalingWebSocketHandler handler) {
        this.handler = handler;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(handler, "/ws");
    }
}