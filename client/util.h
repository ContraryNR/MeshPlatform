#ifndef UTIL_H
#define UTIL_H

#include <QString>
#include <QSet>
#include <QHash>
#include <QCryptographicHash>

// Coordinator 保留为 1
// 其他主机号范围: [2, 254]
inline int hashHostNameToHostNum(const QString& hostName)
{
    QByteArray hash = QCryptographicHash::hash(hostName.toUtf8(), QCryptographicHash::Sha256);
    // 取中间 4 字节 (避开首尾易受前缀碰撞影响的位置)
    quint32 val = (quint8)hash[4] << 24//取一个字节(32位)然后左移24个二进制位
                | (quint8)hash[5] << 16
                | (quint8)hash[6] << 8
                | (quint8)hash[7];     //逐个`|`错位拼接4*32个二进制位
    return (int)(val % 253) + 2; //跳过[0,1]映射到[2,254]
}
//`<<` + `|`是大端序(Big-Endian)逻辑
//memcpy在大小端内存机器上计算结果不同

// 检测并解决哈希冲突: 从 baseHostNum 起自增 1 (跳过 1 和已占用)
// occupied 应包含 Coordinator (1) 和所有已分配主机号
inline int resolveHostNumCollision(int baseHostNum, const QSet<int>& occupied)
{
    int candidate = baseHostNum;
    while(candidate < 2 || candidate > 254 || occupied.contains(candidate))
    {
        candidate++;
        if(candidate > 254)
            candidate = 2;
        // 极端情况下 253 个槽全占,理论上不可能
    }
    return candidate;
}

#endif // UTIL_H
