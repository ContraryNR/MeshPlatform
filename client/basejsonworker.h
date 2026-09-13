#ifndef BASEJSONWORKER_H
#define BASEJSONWORKER_H

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QHash>
#include "dcmanager.h"

class basejsonworker : public QObject
{
    Q_OBJECT
public:
    bool onlineMode;
    QString& localHostName;int& localHostNum;
    QHash<int,QVector<dcworker*>>* ipRoute{nullptr};
    QHash<QString,int> fileCounters; // key = "orientation/msgType", value = 当前序号
    basejsonworker(QString& localhostname,int& localhostnum,bool onlineRunning,void* voidIpRoute)
        :localHostName(localhostname),localHostNum(localhostnum),onlineMode(onlineRunning),ipRoute((QHash<int,QVector<dcworker*>>*)voidIpRoute){};

public:
    QJsonObject getFinalJson(QJsonObject msg)
    {
        if(msg["type"].toString()=="hostname")
        {
            msg["hostname"]=localHostName;
            msg["target"]=1;
        }
        else
        {
            msg["source"]=localHostNum;
            if(msg["type"].toString()=="sdp"
               &&msg["sdpType"].toString()=="offer"
               &&msg["initialOffer"].toInt())
                msg["hostname"]=localHostName;
        }
        return msg;
    }
    void saveOrientedFile(const QString& orientation,const QByteArray& content,const QString& msgType)
    {
        QString dirPath=QDir::currentPath()+"/to"+orientation;
        QDir().mkpath(dirPath);
        QString counterKey=orientation+"/"+msgType;
        int seq=fileCounters.value(counterKey,0);
        fileCounters[counterKey]=seq+1;
        QString filePath=dirPath+"/"+msgType+"_"+QString::number(seq)+".json";
        QFile jsonFile(filePath);
        if(!jsonFile.open(QIODevice::WriteOnly))
            if(!jsonFile.open(QIODevice::WriteOnly))
                return;
        jsonFile.write(content);
        jsonFile.close();
        emit offlineFileSaved(QString("离线模式: 消息已保存到 to%1/%2_%3.json，请发送给%1").arg(orientation,msgType,QString::number(seq)));
    }
    void backSignaling(const QJsonObject& signalingMsg,void* wayBackWorker)
    {
        QByteArray jsonMsg;
        //Qt 3 遗留的接口
        //append(int count, char ch)
        //jsonMsg.append((uint8_t)(0x05),1);//=>被对端解释为五个TYPE_TUN(case 1)
        //如果能确定要写入的内容长度仅为'一个字节' 那就直接用char类型 不要用uint8_t然后指定字节数为1
        //根本上来讲还是用法错误 应该传参(char*,size_t) 结果误传成了(uint8_t,size_t)
        //导致被解释为"将size_t值的char追加uint8_t次"
        jsonMsg.append((char)TYPE_JSON);
        jsonMsg.append(QJsonDocument(signalingMsg).toJson());
        QMetaObject::invokeMethod((dcworker*)wayBackWorker,"sendTunMsg",Qt::QueuedConnection,
                                  Q_ARG(const QByteArray&,jsonMsg),Q_ARG(bool,false));
    }

signals:
    void transferDcManagerMsgToNetWorker(const QByteArray&);
    void goCreateLocalAnswerER(const QString& peerHostName,int peerHostNum,const QString& offer,int index);
    void goSetCandidate(const QString& candidate,const QString& mid,int peerHostNum,int index);
    void offlineFileSaved(const QString& hintMsg);
};

#endif // BASEJSONWORKER_H
