package com.contrarynr.mesh;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@EnableScheduling//管理面:开启定时任务(明细聚合/过期清理)
public class MeshBackendApplication {
    public static void main(String[] args) {
        SpringApplication.run(MeshBackendApplication.class, args);
    }
}
