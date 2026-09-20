#ifndef DCMANAGER_H
#define DCMANAGER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QSet>
#include <QStringList>
#include "dcworker.h"

#define maxWorkerGroupSize 4
//worker index: 0=>主通道(TUN/字符串/协商) 1=>文件传输 2=>音频(暂未实现) 3=>视频通话
//TYPE_*宏仅用于消息协议头和purpose参数,与workerGroup中的index无关

class dcmanager : public QObject
{
    Q_OBJECT
public:
    bool onlineMode;
    QHash<int,QString> nameRoute;
    QHash<int,QVector<dcworker*>> ipRoute;
    QHash<QPair<int,int>, QVector<QPair<QString,QString>>> candidateBuffer;
    //worker(answerER)仅在sdp(offer)到来时创建=>若candidate先于sdp到达则需等待worker创建完毕并setRemoteSdp后方可addCandidate
    std::vector<rtc::binary>& inboundBuffer;
    QTimer* basicTimer{nullptr};
    QTimer* statsTimer{nullptr};
    QHash<QPair<int,int>,QPair<qint64,qint64>> lastBytes;//(hostNum,通道)->上次上报的(bytesSent,bytesReceived)累计基线,用于差分出速率
    QSet<QString> statsFields;//本次要上报的字段组:由 statsScheduler 经 onStatsConfig 中继而来,仅 DC 线程访问
    QMutex* mutex{NULL};
    int busySize=104857;
    int freeSize=32768;
    dcmanager(std::vector<rtc::binary>& inBuffer,QMutex* mtx,bool isOnline)
        :inboundBuffer(inBuffer),mutex(mtx),onlineMode(isOnline)
    {
        basicTimer=new QTimer(this);
        basicTimer->setInterval(1000);
        connect(basicTimer,&QTimer::timeout,this,[this](){
            int ibs=0,obs=0,pr=0;
            QList<fileDownLoadState> allState;
            for(auto workerGroup:ipRoute.values())
                for(dcworker* worker:workerGroup)
                    if(worker)
                    {
                        workerState state=worker->getState();
                        ibs+=state.inBoundSpeed;
                        obs+=state.outBoundSpeed;
                        pr+=state.pressure;
                        allState<<state.fileState;
                    }
            emit workerStatePulse(ibs,obs,pr,allState);
        });        
        //统计上报定时器改为"懒创建":不在构造里按 onlineMode 预建,而是在
        //首个 statsCfg 到达 DC 线程(onStatsConfig)时才创建并 connect ——
        //离线无 server 下发则始终不建,不占用任何资源。见 onStatsConfig 的 if(!statsTimer) 分支。
    }
    dcworker* addPeer(const QString& peerHostName,int peerHostNum,bool isOfferER)
    {
        if(!nameRoute.contains(peerHostNum)&&(!peerHostName.isEmpty()))
        {
            nameRoute.insert(peerHostNum,peerHostName);
            ipRoute.insert(peerHostNum,QVector<dcworker*>());
        }

        int index=ipRoute[peerHostNum].size();
        if(index>=maxWorkerGroupSize)
            return nullptr;

        dcworker* worker=new dcworker(isOfferER,peerHostNum,index,inboundBuffer,mutex,busySize,freeSize);
        ipRoute[peerHostNum].append(worker);

        connect(worker,&dcworker::sendSignalingMsg,this,[this](const QJsonObject& msg){
            emit transferWorkerMsg(msg);
        });
        connect(worker, &dcworker::receiveStringMsg, this,[this](int peerHostNum, const QString& msg){
            emit receiveStringMsg(peerHostNum, msg);
        });
        connect(worker,&dcworker::informFileDownLoadFinish,this,[this](const QString& fileName,int peerHostNum){
            emit informFileDownLoadFinish(fileName,peerHostNum);
        });
        connect(worker,&dcworker::transferDecodedFrame,this,[this](const QImage& frame,int peerHostNum){
            emit transferDecodedFrame(frame,peerHostNum);
        });
        connect(worker,&dcworker::transferDecodedAudio,this,[this](const QByteArray& pcmData,int peerHostNum){
            emit transferDecodedAudio(pcmData,peerHostNum);
        });
        connect(worker,&dcworker::transferDecodedAudioLevel,this,[this](int level,int peerHostNum){
            emit transferDecodedAudioLevel(level,peerHostNum);
        });
        connect(worker,&dcworker::transferRequest,this,[this](uint8_t msgType,uint64_t requestTime,const QJsonObject& callParams,void* voidDCWorker){
            emit transferRequest(msgType,requestTime,callParams,voidDCWorker);
        });
        //统计采集已收敛为 dcManager 的 statsTimer 同步直采,worker 不自行开定时器,
        //故不再需要按配置向新 worker 投递快照
        connect(worker,&dcworker::returnRequestResult,this,[this](uint64_t requestTime,bool result){
            emit returnRequestResult(requestTime,result);
        });
        connect(worker,&dcworker::videoHangupReceived,this,[this](int peerHostNum){
            emit videoHangupReceived(peerHostNum);
        });
        connect(worker,&dcworker::audioHangupReceived,this,[this](int peerHostNum){
            emit audioHangupReceived(peerHostNum);
        });

        connect(worker,&dcworker::dcConnected,this,[this](int peerHostNum){
            emit dcConnected(nameRoute.value(peerHostNum,"未知主机"));
        });

        if(!onlineMode)
            connect(worker,&dcworker::signalingBackUp,this,[this](const QJsonObject& signalingMsg){
                emit onSignalingBackMsg(signalingMsg);
            });

        QThread* trd=new QThread;
        worker->moveToThread(trd);
        QMetaObject::invokeMethod(worker,"initialPendingProcessTimer",Qt::QueuedConnection);
        //统计采集由 dcManager 的 statsTimer 同步直采,worker 无独立上报定时器,无需投递配置快照
        connect(worker,&dcworker::dcFinish,this,[worker,trd,peerHostNum,this,index](){
            trd->quit();
            trd->wait();
            delete(worker);
            if(ipRoute.contains(peerHostNum))
                ipRoute[peerHostNum].removeAll(worker);
            trd->deleteLater();
            QString peerName = nameRoute.value(peerHostNum);
            lastBytes.remove(qMakePair(peerHostNum,index));//各通道各自清基线
            if(index==0)
            {
                ipRoute.remove(peerHostNum);
                nameRoute.remove(peerHostNum);
                emit peerRemoved(peerHostNum);
            }
            else if(ipRoute.contains(peerHostNum))
                emit peerConnectionAmountChanged(peerHostNum,ipRoute[peerHostNum].size());
        });
        trd->start();
        QMetaObject::invokeMethod(worker,"createDc",Qt::QueuedConnection);

        if(index==0)
            emit peerAdded(peerHostNum, peerHostName);
        else
            emit peerConnectionAmountChanged(peerHostNum,ipRoute[peerHostNum].size());
        return worker;
    }
public slots://signalingSlot
    //jsonWorker解析出initialOffer建立初始worker0走createOfferER槽函数
    //settingDialog走getExtraConnection槽函数间接调用addPeer而不是createOfferER
    void createOfferER(const QString& peerHostName,int peerHostNum)//=>只负责初始dc建立(指"已在线host对新加入host建立的worker0)
    {
        if(nameRoute.contains(peerHostNum))
            return;
        addPeer(peerHostName,peerHostNum,true);
    }
    void createAnswerER(const QString& peerHostName,int peerHostNum,const QString& offer,int index)
    {
        dcworker* currentWorker{nullptr};
        //(1)判断是否已加入(基于workerGrpSize) (2)判断字符串是否为空(基于对方index是否为0)
        if(!nameRoute.contains(peerHostNum)&&(!peerHostName.isEmpty()))
            currentWorker=addPeer(peerHostName,peerHostNum,false);
        else
        {
            if(index>=maxWorkerGroupSize)return;
            int currentSize=ipRoute[peerHostNum].size();
            for(int i=0;i<index+1-currentSize;i++)
                //根据对方worker的index创建连续的直到同index的worker
                addPeer(peerHostName,peerHostNum,false);
            if(index < ipRoute[peerHostNum].size())
                currentWorker=ipRoute[peerHostNum][index];
        }
        //初始连接offer/连接补齐完成后当前offer=>对应的worker
        //连接补齐的非投递目标(index不匹配者)的worker与此无关
        if(currentWorker)
            QMetaObject::invokeMethod(currentWorker,"setRemoteSdp",Qt::QueuedConnection,Q_ARG(const QString&,offer),Q_ARG(const QString&,"offer"));
        // flush该 worker 缓冲的 candidate
        QPair<int,int> bufKey(peerHostNum,index);
        if(candidateBuffer.contains(bufKey))
            for(const auto& p : candidateBuffer.take(bufKey))
                QMetaObject::invokeMethod(currentWorker,"receiveCandidate",Qt::QueuedConnection,Q_ARG(const QString&,p.first),Q_ARG(const QString&,p.second));
    }
    void setAnswer(const QString& sdp,int peerHostNum,int index)
    {
        if(index>=ipRoute[peerHostNum].size())return;
        dcworker* worker=ipRoute.value(peerHostNum)[index];
        if(worker)
            QMetaObject::invokeMethod(worker,"setRemoteSdp",Qt::QueuedConnection,Q_ARG(const QString&,sdp),Q_ARG(const QString&,"answer"));
    }
    void setCandidate(const QString& candidate,const QString& mid,int peerHostNum,int index)
    {
        if(!ipRoute.contains(peerHostNum)||index>=ipRoute[peerHostNum].size()||!ipRoute[peerHostNum][index])
        {
            // worker尚未创建,缓冲 candidate 避免candidate无效投递
            candidateBuffer[QPair<int,int>(peerHostNum,index)].append(qMakePair(candidate,mid));
            return;
        }
        dcworker* worker=ipRoute[peerHostNum][index];
        QMetaObject::invokeMethod(worker,"receiveCandidate",Qt::QueuedConnection,Q_ARG(const QString&,candidate),Q_ARG(const QString&,mid));
    }

public slots://connectionExtensionSlot
    void getExtraConnection(int targetAmount,int peerHostNum)
    {
        int currentSize=ipRoute[peerHostNum].size();
        for(int i=0;i<targetAmount-currentSize;i++)
            addPeer(QString(),peerHostNum,true);
    }
    void releaseExtraConnection(int targetAmount,int peerHostNum)
    {
        int offset=ipRoute[peerHostNum].size()-targetAmount;
        for(int i=0;i<offset;i++)
        {
            QMetaObject::invokeMethod(ipRoute[peerHostNum].back(),"shutdown",Qt::QueuedConnection);
            ipRoute[peerHostNum].pop_back();
        }
    }

public slots:
    void updateAllDcWorkerSettings(int bSize, int fSize)
    {
        busySize = bSize;
        freeSize = fSize;
        for (QVector<dcworker*>& workerGroup : ipRoute.values())
            for(dcworker* worker:workerGroup)
            QMetaObject::invokeMethod(worker, "updateSettings", Qt::QueuedConnection,Q_ARG(int, bSize), Q_ARG(int, fSize));
    }
    //用于在通话开始/挂断(主动或被动)时更新指定dcworker的isCalling状态
    void setWorkerCallingState(int peerHostNum,int workerIndex,bool calling)
    {
        if(!ipRoute.contains(peerHostNum))
            return;
        QVector<dcworker*>& workers = ipRoute[peerHostNum];
        if(workers.size() > workerIndex && workers[workerIndex])
        {
            if(workerIndex == 3)
                workers[workerIndex]->isVideoCalling = calling;
            else if(workerIndex == 2)
                workers[workerIndex]->isAudioCalling = calling;
        }
    }
    public slots://statsSlot
    //server 下发的 statsCfg 已由 jsonWorker 解析并经 statsScheduler(独立线程)中继而来;
    //本槽落 DC 线程(QT 信号跨线程自动 Queued),把最新配置落到本地字段组与统计定时器:
    //enabled 起停本类 statsTimer,interval 设周期,fields 落到 statsFields 供直采用。
    void onStatsConfig(bool enabled,int intervalMs,const QStringList& fields)
    {
        statsFields=QSet<QString>(fields.cbegin(),fields.cend());
        //统计上报定时器懒创建:首个 statsCfg 到达 DC 线程时才建(QT 跨线程信号自动 Queued 到本线程)并 connect
        //本类在本线程"承上启下":每次到时直接同步调用各 worker 的 collectStats(statsFields),
        //采集-聚合-上报同在一个调用栈完成;edges 为函数调用期间的局部变量,不再占类成员。
        if(!statsTimer)
        {
            statsTimer=new QTimer(this);
            connect(statsTimer,&QTimer::timeout,this,[this]()
                {
                    QJsonArray pendingEdges;//局部缓冲:本次时点到各 worker 直采到的边
                    for(auto [hostNum,workerGroup] : ipRoute.asKeyValueRange())
                        for(dcworker* worker:workerGroup)
                            if(worker)
                            {
                                QJsonObject edge;
                                //collectStats 内部已按 isShuttingDown 快速返回空;若仍撞上 pc/dc
                                //析构瞬间,参考 tunoutworker 用 try-catch 兜底 —— 下次访问自会
                                //因 isShuttingDown 为真而提前退出,不会越界访问
                                try { edge=worker->collectStats(statsFields); }
                                catch(const std::exception& e){ qWarning()<<"collectStats failed:"<<e.what(); continue; }
                                if(edge.isEmpty())
                                    continue;
                                diffTraffic(edge);//边若带 traffic 累计字节则差分出 up/down
                                pendingEdges.append(edge);
                            }
                    //空edges也上报:作为节点在线心跳(拓扑需要展示尚无连接的孤立节点)
                    QJsonObject statsJson;
                    statsJson["type"]="stats";
                    statsJson["target"]=1;
                    statsJson["edges"]=pendingEdges;
                    emit transferWorkerMsg(statsJson);
                }
            );
        }
        if(enabled)
        {
            statsTimer->setInterval(intervalMs);
            statsTimer->start();
        }
        else
        {
            statsTimer->stop();
            lastBytes.clear();//停报时清空速率基线,重新开启后首条重建基线
        }
    }
    //信令连接断开:立即关闭上报(从上游停掉采集,而不是靠发送点 isValid 兜底丢弃)
    void onSignalingDown()
    {
        if(statsTimer)
        {
            statsTimer->stop();
            lastBytes.clear();
        }
        statsFields.clear();
    }

private:
    //采集边差分:累计字节(bytesSent/bytesReceived)+ 真实采集周期 elapsed(ms) => 速率 up/down(B/s)。
    //仅当边携带 traffic 字段时执行,基线 key 为 (hostNum,通道),存 lastBytes 成员。
    void diffTraffic(QJsonObject& edge)
    {
        if(!edge.contains("bytesSent")||!edge.contains("bytesReceived"))
            return;
        qint64 sent=edge["bytesSent"].toInteger(),received=edge["bytesReceived"].toInteger();
        QPair<int,int> key(edge["peer"].toInt(),edge["ch"].toInt());
        QPair<qint64,qint64> base=lastBytes.value(key,qMakePair((qint64)-1,(qint64)-1));
        if(base.first>=0&&sent>=base.first&&received>=base.second&&edge.contains("elapsed"))
        {
            qint64 elapsed=edge["elapsed"].toInteger();
            if(elapsed>0)
            {
                edge["up"]=(sent-base.first)*1000/elapsed;
                edge["down"]=(received-base.second)*1000/elapsed;
            }
        }
        lastBytes.insert(key,qMakePair(sent,received));
    }
public slots://timerSlot
    void startTimer()
    {
        basicTimer->start();
        if(statsTimer)//离线模式不创建statsTimer(无处上报),空指针防护
            statsTimer->start();
    }
    void stopTimer()
    {
        if(basicTimer->isActive())
            basicTimer->stop();
        if(statsTimer&&statsTimer->isActive())
            statsTimer->stop();
    }
    void cleanQOBJ()
    {
        if(basicTimer->isActive())
            basicTimer->stop();
        basicTimer->deleteLater();
        if(statsTimer)
        {
            if(statsTimer->isActive())
                statsTimer->stop();
            statsTimer->deleteLater();
        }
    }

signals:
    void transferWorkerMsg(const QJsonObject&);
    void pendingBinaryMsgSizeChanged(int pendingSize);
    void peerAdded(int peerHostNum, const QString& peerHostName);
    void peerRemoved(int peerHostNum);
    void receiveStringMsg(int peerHostNum, const QString& msg);
    void informFileDownLoadFinish(const QString& filename,int peerHostNum);
    void transferDecodedFrame(const QImage&,int);
    void transferDecodedAudio(const QByteArray&,int);
    void transferDecodedAudioLevel(int level,int peerHostNum);
    void peerConnectionAmountChanged(int peerHostNum,int currentConnectAmount);
    void workerStatePulse(int ibs,int obs,int pr,const QList<fileDownLoadState>&);
    void transferRequest(uint8_t msgType,uint64_t requestTime,const QJsonObject& callParams,void* voidDCWorker);
    void returnRequestResult(uint64_t requestTime,bool result);
    void videoHangupReceived(int peerHostNum);
    void audioHangupReceived(int peerHostNum);
    void dcConnected(const QString& hostName);
    void onSignalingBackMsg(const QJsonObject&);
};

inline dcworker* getDcWorker(void* voidIpRoute,int peerHostNum,int purpose)
{
    if(!voidIpRoute)return nullptr;
    QHash<int,QVector<dcworker*>>* ipRoute=(QHash<int,QVector<dcworker*>>*)voidIpRoute;
    if(ipRoute->isEmpty()||!ipRoute->contains(peerHostNum))return nullptr;
    QVector<dcworker*> workerGroup=ipRoute->value(peerHostNum);
    dcworker* worker=nullptr;
    int idx=-1;
    switch(purpose)
    {
        case TYPE_TUN:  idx=0;break;
        case TYPE_FILE: idx=workerGroup.size()>1?1:0;break;
        case TYPE_AUDIO:idx=2;break;
        case TYPE_VIDEO:idx=3;break;
    }
    if(idx>=0&&idx<workerGroup.size())
    {
        worker=workerGroup[idx];
        if(idx==0)worker->newEventNow=true;
    }
    return worker;
}
inline QList<dcworker*> getBroundCastWorkers(void* voidIpRoute)
{
    if(!voidIpRoute)return QList<dcworker*>();
    QHash<int,QVector<dcworker*>>* ipRoute=(QHash<int,QVector<dcworker*>>*)voidIpRoute;
    if(ipRoute->isEmpty())return QList<dcworker*>();
    QList<dcworker*> workers;
    for(auto workerGroup:ipRoute->values())
        workers.append(workerGroup[0]);
    return workers;
}
inline QList<dcworker*> getVideoCallingPeerWorkers(void* voidIpRoute,QList<int> peerHostNumList)
{
    if(!voidIpRoute)return QList<dcworker*>();
    QHash<int,QVector<dcworker*>>* ipRoute=(QHash<int,QVector<dcworker*>>*)voidIpRoute;
    if(ipRoute->isEmpty())return QList<dcworker*>();
    QList<dcworker*> workers;
    QVector<dcworker*> tempWorkerGrp;
    for(int hostNum:peerHostNumList)
        if(ipRoute->contains(hostNum))
        {
            tempWorkerGrp=ipRoute->value(hostNum);
            if(tempWorkerGrp.size()>=4)
                workers.append(tempWorkerGrp[3]);
        }
    return workers;
}
inline QList<dcworker*> getAudioCallingPeerWorkers(void* voidIpRoute,QList<int> peerHostNumList)
{
    if(!voidIpRoute)return QList<dcworker*>();
    QHash<int,QVector<dcworker*>>* ipRoute=(QHash<int,QVector<dcworker*>>*)voidIpRoute;
    if(ipRoute->isEmpty())return QList<dcworker*>();
    QList<dcworker*> workers;
    QVector<dcworker*> tempWorkerGrp;
    for(int hostNum:peerHostNumList)
        if(ipRoute->contains(hostNum))
        {
            tempWorkerGrp=ipRoute->value(hostNum);
            if(tempWorkerGrp.size()>=3)
                workers.append(tempWorkerGrp[2]);
        }
    return workers;
}
inline bool isWorkerReady(int peerHostNum,int purpose,void* voidIpRoute)
{
    if(!voidIpRoute)return false;
    QHash<int,QVector<dcworker*>>* ipRoute=(QHash<int,QVector<dcworker*>>*)voidIpRoute;
    if(ipRoute->isEmpty())return false;
    int idx=-1;
    switch(purpose)
    {
        case TYPE_TUN:  idx=0;break;
        case TYPE_FILE: idx=1;break;
        case TYPE_AUDIO:idx=2;break;
        case TYPE_VIDEO:idx=3;break;
    }
    if(idx>=0&&ipRoute->contains(peerHostNum)&&ipRoute->value(peerHostNum).size()>idx)
        return true;
    return false;
}
//注意 独立inline函数定义在#endif上面而不是下面
#endif // DCMANAGER_H
