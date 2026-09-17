#ifndef UTIL_H
#define UTIL_H

#include <QString>
#include <QSet>
#include <QHash>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>

// 主机号范围: [1, 254] —— 1 不再为 Coordinator 保留
// (Java 端只做信令中介, 不参与组网, 没有自身主机号)
// 与 PeerRegistry.hashHostNameToHostNum 必须逐位一致
inline int hashHostNameToHostNum(const QString& hostName)
{
    QByteArray hash = QCryptographicHash::hash(hostName.toUtf8(), QCryptographicHash::Sha256);
    // 取中间 4 字节 (避开首尾易受前缀碰撞影响的位置)
    quint32 val = (quint8)hash[4] << 24//取一个字节(32位)然后左移24个二进制位
                | (quint8)hash[5] << 16
                | (quint8)hash[6] << 8
                | (quint8)hash[7];     //逐个`|`错位拼接4*32个二进制位
    return (int)(val % 254) + 1; //映射到[1,254]
}
//`<<` + `|`是大端序(Big-Endian)逻辑
//memcpy在大小端内存机器上计算结果不同

// 检测并解决哈希冲突: 从 baseHostNum 起自增 1 (跳过已占用)
// occupied 应包含所有已分配主机号
inline int resolveHostNumCollision(int baseHostNum, const QSet<int>& occupied)
{
    int candidate = baseHostNum;
    while(candidate < 1 || candidate > 254 || occupied.contains(candidate))
    {
        candidate++;
        if(candidate > 254)
            candidate = 1;
        // 极端情况下 254 个槽全占,理论上不可能
    }
    return candidate;
}

// 地址是否落在本机任一网卡的网段内(IPv4, 掩码做前缀比较)
// 用途:netPath 判定 —— 只有"对端地址与本机同网段"才算真正的局域网直连;
// 单看候选类型(host/srflx)会把"同机或同网段互通"误判成公网:
// 只要 ICE 选中了 srflx 候选, 即使对端就在本机, 类型也是"非 host"
inline bool isInLocalSubnet(const QString& addr)
{
    QHostAddress target(addr);
    if(target.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    for(const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
        for(const QNetworkAddressEntry& entry : iface.addressEntries())
        {
            if(entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            quint32 mask = entry.netmask().toIPv4Address();
            if(!mask)
                continue;
            if((target.toIPv4Address() & mask) == (entry.ip().toIPv4Address() & mask))
                return true;
        }
    return false;
}

#endif // UTIL_H
