package com.contrarynr.mesh;

import org.springframework.stereotype.Component;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

/**
 * Peer 注册表 —— 对应 C++ 侧 coorjsonworker 的 nameToNumMap + util.h 的哈希逻辑。
 *
 * Spring 语义速查:
 *  - @Component: 把这个类交给 IoC 容器托管(全局单例), 相当于你在 C++ 里
 *    手工保证"全程序只有一份"的那个对象 —— 只是现在由容器替你保证
 *  - 使用方通过构造器注入拿到它, 全工程没有一处 new PeerRegistry() —— 这是 Spring 的核心心智模型
 */
@Component
public class PeerRegistry {

    /** Coordinator 自身保留为 1, 其他主机号范围 [2, 254] —— 与 util.h 保持一致 */
    private static final int HOST_NUM_MIN = 2;
    private static final int HOST_NUM_MAX = 254;

    /** hostName -> hostNum (确定性映射: 同一 hostName 必然得到同一 hostNum) —— 同 nameToNumMap */
    private final Map<String, Integer> nameToNumMap = new ConcurrentHashMap<>();

    /**
     * 与 util.h::hashHostNameToHostNum 语义完全一致:
     * SHA-256 取第 4~7 字节(大端)拼成无符号 32 位整数, mod 253 后 +2, 映射到 [2, 254]
     */
    public static int hashHostNameToHostNum(String hostName) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hash = digest.digest(hostName.getBytes(StandardCharsets.UTF_8));
            int val = (hash[4] & 0xFF) << 24
                    | (hash[5] & 0xFF) << 16
                    | (hash[6] & 0xFF) << 8
                    | (hash[7] & 0xFF);
            // Java 的 int 是有符号的, 用无符号语义取模, 对齐 C++ quint32 的行为
            return (int) (Integer.toUnsignedLong(val) % 253) + 2;
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("JDK 竟然没有 SHA-256", e);
        }
    }

    /** 与 util.h::resolveHostNumCollision 一致: 从 base 起自增 1, 跳过 1 和已占用的号 */
    private static int resolveHostNumCollision(int baseHostNum, Set<Integer> occupied) {
        int candidate = baseHostNum;
        while (candidate < HOST_NUM_MIN || candidate > HOST_NUM_MAX || occupied.contains(candidate)) {
            candidate++;
            if (candidate > HOST_NUM_MAX) {
                candidate = HOST_NUM_MIN;
            }
        }
        return candidate;
    }

    /**
     * 注册一个 peer —— 对应 coorjsonworker::onExternalMsg 中 type=="hostname" 分支:
     * 算基号 -> 解决冲突 -> 入表, 返回分配到的主机号
     */
    public synchronized int register(String hostName) {
        int base = hashHostNameToHostNum(hostName);
        Set<Integer> occupied = nameToNumMap.values().stream().collect(Collectors.toSet());
        occupied.add(1); // Coordinator 自身
        int assigned = resolveHostNumCollision(base, occupied);
        nameToNumMap.put(hostName, assigned);
        return assigned;
    }

    /** 按主机号反查主机名 —— 对应 coorjsonworker::hostNameFor */
    public String hostNameFor(int hostNum) {
        return nameToNumMap.entrySet().stream()
                .filter(e -> e.getValue() == hostNum)
                .map(Map.Entry::getKey)
                .findFirst()
                .orElse(null);
    }

    /** 当前注册表快照(只读视图) */
    public Map<String, Integer> snapshot() {
        return Map.copyOf(nameToNumMap);
    }
}