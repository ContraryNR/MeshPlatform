#ifndef PEERJSONWORKER_H
#define PEERJSONWORKER_H

#include "basejsonworker.h"
#include <QJsonArray>
#include <QDebug>

class peerjsonworker : public basejsonworker
{
    Q_OBJECT
public:
    QHash<int,QString>& nameRoute;
    peerjsonworker(QString& localhostname,int& localhostnum,QHash<int,QString>& nameRouteFromDcManager,bool onlineRunning,void* voidIpRoute)
        :basejsonworker(localhostname,localhostnum,onlineRunning,voidIpRoute),nameRoute(nameRouteFromDcManager){}

public slots:
    void onInternalMsg(const QJsonObject& msg)
    {
        QByteArray byteArr=QJsonDocument(getFinalJson(msg)).toJson(QJsonDocument::Compact);
        if(onlineMode)
            emit sendToNetWorker(byteArr+'\n');
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
                saveOrientedFile(targetName,byteArr,mType);
            }
        }
    }
    void onReadySendHostName()
    {
        QJsonObject hostNameJson;
        hostNameJson["type"]="hostname";
        QByteArray byteArr=QJsonDocument(getFinalJson(hostNameJson)).toJson(QJsonDocument::Compact);
        if(onlineMode)
            emit sendToNetWorker(byteArr+'\n');
        else
            saveOrientedFile("Coordinator",byteArr,"hostname");
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
    }
signals:
    void sendToNetWorker(const QByteArray&);
    void goCreateOfferER(const QString& peerHostName,int peerHostNum);
    void goCreateAnswerER(const QString& peerHostName,int peerHostNum,const QString& offer,int index);
    void goSetAnswer(const QString& sdp,int peerHostNum,int);
    void goSetCandidate(const QString&,const QString&,int,int);
    void hostNumAssigned(int);
};

#endif // PEERJSONWORKER_H
