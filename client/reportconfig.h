#ifndef REPORTCONFIG_H
#define REPORTCONFIG_H

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QString>

//统计上报配置 —— 唯一来源是 server 经 statsCfg 下发的配置, 客户端不提供本地调控入口。
//
//为什么不做成"客户端本地选几个上报档位":
//  同一条 edge 由两端各自上报, 若一端报质量字段、另一端不报, 拓扑上就出现信息不对等
//  (一条边一半有 RTT/路径、一半只剩边存在性)。把"周期 + 内容"的裁决权收归 server,
//  所有客户端拿到同一份配置, 才是真正的数据统一。
//
//开关语义:
//  未收到 server 下发前 = 关闭(不产生任何上报);
//  信令连接断开时立即置为关闭 —— 从上游停掉不必要的行为,
//  而不是让上报继续跑、靠发送点的 wsSocket->isValid() 兜底丢弃。
class reportconfig
{
public:
    bool enabled{false};   //上报开关
    int intervalMs{5000};  //上报周期(ms)
    //要上报的字段组(空集合 = 裸边, 只表达边的存在性):
    //  rtt      -> rtt
    //  traffic  -> bytesSent/bytesReceived(由 dcmanager 差分出 up/down)
    //  buffered -> buffered
    //  pcState  -> pcState(PeerConnection 连接状态)
    //  iceState -> iceState
    //  path     -> netPath + candLocal/candRemote
    //(字段组名与 JSON key、server 侧入库列名 pc_state/ice_state 保持一致:2026-09-17 由 state/ice 改名而来)
    QSet<QString> fields;

    //从 server 下发的 statsCfg 装载
    void load(const QJsonObject& cfg)
    {
        enabled=cfg.value("enabled").toBool(false);
        int iv=cfg.value("interval").toInt(0);
        if(iv>0)
            intervalMs=iv;
        fields.clear();
        for(const QJsonValue& v : cfg.value("fields").toArray())
            fields.insert(v.toString());
    }
    //信令断开:立即关闭上报(上游停止,不依赖发送点的兜底判断)
    void disable()
    {enabled=false;fields.clear();}
    //该字段组是否上报
    bool hasField(const QString& f) const
    {return fields.contains(f);}
    //裸边判定:未配置任何字段组 —— 只表达边的存在性
    //(server 侧可导出同一结论:一条边"所有质量字段皆缺省"即裸边,故报文里不必额外声明)
    bool bare() const
    {return fields.isEmpty();}
};

#endif // REPORTCONFIG_H
