package com.contrarynr.mesh;

import jakarta.servlet.http.HttpServletResponse;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.HttpMethod;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configuration.EnableWebSecurity;
import org.springframework.security.core.userdetails.User;
import org.springframework.security.core.userdetails.UserDetails;
import org.springframework.security.core.userdetails.UserDetailsService;
import org.springframework.security.provisioning.InMemoryUserDetailsManager;
import org.springframework.security.web.SecurityFilterChain;

/*管理面权限 —— 把"能看"和"能改"分开:
 - 拓扑/历史/SSE/管理页,以及信令握手(/ws)全部放行:任何端都能看网络状态;
 - 只有"改上报配置"这个写接口 POST /stats/config 要求管理员身份(HTTP Basic)。
 注意 /ws 必须显式放行 —— 它被拦会直接导致 C++ 客户端连不上信令服务器、整个组网失效。
 关闭 CSRF:SSE/WebSocket/前端 fetch 都不带 token;本服务面向内网部署,写接口另有 Basic 认证兜底。*/
@Configuration
@EnableWebSecurity
public class SecurityConfig {
    @Bean
    public SecurityFilterChain filterChain(HttpSecurity http) throws Exception
    {
        http
            .csrf(csrf -> csrf.disable())
            .authorizeHttpRequests(auth -> auth
                .requestMatchers(HttpMethod.POST, "/stats/config").authenticated()//调控接口:仅管理员
                .anyRequest().permitAll())//信令握手/拓扑查询/SSE/管理页:放行
            //401 只回状态码、不写 WWW-Authenticate 头 —— 避免浏览器弹原生认证框,由前端自行提示
            .httpBasic(basic -> basic.authenticationEntryPoint(
                (request, response, ex) -> response.sendError(HttpServletResponse.SC_UNAUTHORIZED)));
        return http.build();
    }
    //管理员账号:内存用户,账号密码取自 application.properties(内网部署,改密码只改配置)
    @Bean
    public UserDetailsService userDetailsService(
        @Value("${mesh.admin.user:admin}") String user,
        @Value("${mesh.admin.password:mesh-admin}") String password)
    {
        UserDetails admin = User.withUsername(user).password("{noop}" + password).roles("ADMIN").build();
        return new InMemoryUserDetailsManager(admin);
    }
}
