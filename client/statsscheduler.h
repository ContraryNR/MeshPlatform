#ifndef STATSSCHEDULER_H
#define STATSSCHEDULER_H

#include <QObject>
#include <QStringList>

//统计上报配置的权威存储与"变更中继":全客户端唯一的配置归属方。
//jsonWorker(收到 server 下发的 statsCfg)解析后经 applyStatsCfg 入槽写入本对象,
//再经 statsConfigChanged 转发给真正消费并采样的 dcManager。
//本对象运行在独立线程,applyStatsCfg / statsConfigChanged 均在 scheduler 线程内完成,
//三个成员 enabled/intervalMs/fields 全程只在本线程访问 —— 天然串行,无需锁/原子。
class statsScheduler : public QObject
{
    Q_OBJECT
public:
    statsScheduler(QObject* parent=nullptr):QObject(parent){}

public slots:
    void applyStatsCfg(bool enabled,int intervalMs,const QStringList& fields)
    {
        this->enabled=enabled;
        this->intervalMs=(intervalMs>0?intervalMs:5000);
        this->fields=fields;
        emit statsConfigChanged(this->enabled,this->intervalMs,this->fields);
    }

signals:
    void statsConfigChanged(bool enabled,int intervalMs,const QStringList& fields);

private:
    bool enabled=false;
    int intervalMs=5000;
    QStringList fields;
};

#endif // STATSSCHEDULER_H