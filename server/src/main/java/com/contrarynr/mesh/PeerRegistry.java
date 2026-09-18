package com.contrarynr.mesh;
import org.springframework.stereotype.Component;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

@Component
public class PeerRegistry
{
    private static final int HOST_NUM_MIN = 1;
    private static final int HOST_NUM_MAX = 254;
    private final Map<String, Integer> nameToNumMap = new ConcurrentHashMap<>();
    //主机号的分配
    public static int hashHostNameToHostNum(String hostName)
    {
        try
        {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hash = digest.digest(hostName.getBytes(StandardCharsets.UTF_8));
            int val = (hash[4] & 0xFF) << 24
                    | (hash[5] & 0xFF) << 16
                    | (hash[6] & 0xFF) << 8
                    | (hash[7] & 0xFF);
            //Java 的 int 是有符号的, 用无符号语义取模, 对齐 C++ quint32 的行为
            return (int) (Integer.toUnsignedLong(val) % 254) + 1;
        }
        catch (NoSuchAlgorithmException e)
        {
            throw new IllegalStateException("JDK 竟然没有 SHA-256", e);
        }
    }
    private static int resolveHostNumCollision(int baseHostNum, Set<Integer> occupied)
    {
        int candidate = baseHostNum;
        while (candidate < HOST_NUM_MIN || candidate > HOST_NUM_MAX || occupied.contains(candidate))
        {
            candidate++;
            if (candidate > HOST_NUM_MAX)
                candidate = HOST_NUM_MIN;
        }
        return candidate;
    }
    public synchronized int register(String hostName)
    {
        int base = hashHostNameToHostNum(hostName);
        Set<Integer> occupied = nameToNumMap.values().stream().collect(Collectors.toSet());
        int assigned = resolveHostNumCollision(base, occupied);
        nameToNumMap.put(hostName, assigned);
        return assigned;
    }
    //其他服务的响应体构建
    public String hostNameFor(int hostNum)
    {
        return nameToNumMap.entrySet().stream()
                .filter(e -> e.getValue() == hostNum)
                .map(Map.Entry::getKey)
                .findFirst()
                .orElse(null);
    }
    public Map<String, Integer> snapshot()
    {
        return Map.copyOf(nameToNumMap);
    }
}
