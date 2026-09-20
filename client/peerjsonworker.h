#ifndef PEERJSONWORKER_H
#define PEERJSONWORKER_H

#include "basejsonworker.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QDebug>

class peerjsonworker : public basejsonworker
{
    Q_OBJECT
public:
    QHash<int,QString>& nameRoute;
    peerjsonworker(QString& localhostname,int& localhostnum,QHash<int,QString>& nameRouteFromDcManager,
                   bool onlineRunning,void* voidIpRoute)
        :basejsonworker(localhostname,localhostnum,onlineRunning,voidIpRoute),
         nameRoute(nameRouteFromDcManager){}

public slots:
    void onInternalMsg(const QJsonObject& msg)
    {
        if(onlineMode)
            emit sendToNetWorker(getFinalJson(msg));//结构化JSON贯穿信号链,序列化只在各IO边界发生
        else
        {
            if(msg["index"].toInt()!=0)
                backSignaling(getFinalJson(msg),getDcWorker(ipRoute,msg["target"].toInt(),TYPE_TUN));
            else
            {
                int targetHostNum=msg["target"].toInt();
                QString targetName=(targetHostNum==1)?QString("Coordinator"):nameRoute[targetHostNum];
                QString type=msg["type"].toString();
                QString mType=(type=="sdp")?msg["sdpType"].toString():type;
                saveOrientedFile(targetName,QJsonDocument(getFinalJson(msg)).toJson(QJsonDocument::Compact),mType);//文件IO边界
            }
        }
    }
    void onReadySendHostName()
    {
        QJsonObject hostNameJson;
        hostNameJson["type"]="hostname";
        if(onlineMode)
            emit sendToNetWorker(getFinalJson(hostNameJson));
        else
            saveOrientedFile("Coordinator",QJsonDocument(getFinalJson(hostNameJson)).toJson(QJsonDocument::Compact),"hostname");//文件IO边界
    }
    void onExternalMsg(const QJsonObject& msg)
    {
        QString type = msg["type"].toString();
        if(type=="distributedHostNum")
        {
            int hostNum=msg["hostNum"].toInt();
            localHostNum=hostNum;
            emit hostNumAssigned(hostNum);
        }
        else if(type=="newPeer")
        {
            int peerHostNum=msg["hostNum"].toInt();
            QString peerHostName=msg["hostName"].toString();
            emit goCreateOfferER(peerHostName,peerHostNum);
        }
        else if(type=="sdp")
        {
            if(msg["sdpType"].toString()=="offer")
                emit goCreateAnswerER((msg["initialOffer"].toInt()?msg["hostname"].toString():QString()),msg["source"].toInt(),msg["sdp"].toString(),msg["index"].toInt());
            else if(msg["sdpType"].toString()=="answer")
                emit goSetAnswer(msg["sdp"].toString(),msg["source"].toInt(),msg["index"].toInt());
        }
        else if(type=="candidate")
            emit goSetCandidate(msg["candidateItem"].toString(),msg["candidateMid"].toString(),msg["source"].toInt(),msg["index"].toInt());
        //服务器下发 statsCfg:jsonWorker 只做解析(先拆成员再逐字段发语义化信号,不透传整包)
        //enabled/intervalMs/fields 交给 statsScheduler(独立线程)作为权威配置存储与中继
        else if(type=="statsCfg")
        {
            bool en=msg.value("enabled").toBool(false);
            int iv=msg.value("interval").toInt(5000);
            QStringList flds;
            for(const QJsonValue& v : msg.value("fields").toArray())
                flds<<v.toString();
            emit statsCfgParsed(en,iv,flds);
        }
    }
signals:
    void sendToNetWorker(const QJsonObject&);
    void goCreateOfferER(const QString& peerHostName,int peerHostNum);
    void goCreateAnswerER(const QString& peerHostName,int peerHostNum,const QString& offer,int index);
    void goSetAnswer(const QString& sdp,int peerHostNum,int);
    void goSetCandidate(const QString&,const QString&,int,int);
    void hostNumAssigned(int);
    void statsCfgParsed(bool enabled,int intervalMs,const QStringList& fields);//解析后的统计上报配置(开关/周期/字段组)
};

#endif // PEERJSONWORKER_H
