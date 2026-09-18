package com.contrarynr.mesh;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
public class SignalingWebSocketConfig implements WebSocketConfigurer {
    private final SignalingWebSocketHandler handler;
    public SignalingWebSocketConfig(SignalingWebSocketHandler handler) {this.handler = handler;}
    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry)
    {
        registry.addHandler(handler, "/ws");
    }
}
