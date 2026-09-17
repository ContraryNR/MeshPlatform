#ifndef DCWORKER_H
#define DCWORKER_H

#include <QObject>
#include <QDebug>
#include <QTimer>
#include <string>
#include <variant>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QMutex>
#include <QStack>
#include <QQueue>
#include <QMutexLocker>
#include "rtc/rtc.hpp"
#include "filedownloader.h"
#include "videodecoder.h"
#include "audiodecoder.h"
#include "util.h"//netPath 网段判定(isInLocalSubnet)
#include "reportconfig.h"//统计上报配置(server 下发)

//全局类型标识(统一用于: 二进制消息协议首字节 / worker索引 / purpose参数 / 协商子类型)
#define TYPE_NEGOTIATE  0   //协商(文件/音频/视频请求与响应)
#define TYPE_TUN        1   //TUN隧道数据
#define TYPE_FILE       2   //文件分块传输
#define TYPE_AUDIO      3   //音频(上游暂未实现)
#define TYPE_VIDEO      4   //视频帧(JPEG编码)
#define TYPE_JSON       5

class fileDownLoadState
{
public:
    QString filename;int peerHostNum;qreal progress=0;
    fileDownLoadState(){}
    fileDownLoadState(QString filename, int peerHostNum): filename(std::move(filename)), peerHostNum(peerHostNum){}
    fileDownLoadState(QString filename, int peerHostNum, qreal progress): filename(std::move(filename)), peerHostNum(peerHostNum),progress(progress) {}
    fileDownLoadState(const fileDownLoadState& oldOne):filename(oldOne.filename), peerHostNum(oldOne.peerHostNum),progress(oldOne.progress) {}
};
class workerState
{
public:
    int inBoundSpeed,outBoundSpeed,pressure;QList<fileDownLoadState> fileState;
    workerState(){}
    workerState(int ibs, int obs, int pr): inBoundSpeed(ibs), outBoundSpeed(obs),pressure(pr) {}
    workerState(const workerState& oldOne): inBoundSpeed(oldOne.inBoundSpeed), outBoundSpeed(oldOne.outBoundSpeed),pressure(oldOne.pressure), fileState(oldOne.fileState) {}
    void reset(){inBoundSpeed=outBoundSpeed=pressure=0;}
};

class dcworker : public QObject
{
    Q_OBJECT
public://Flags
    std::atomic<bool> peerAlive{false},dcValid{false},
    isShuttingDown{false},isBufferBusy{false},newEventNow{false},isVideoCalling{false},isAudioCalling{false};
    bool isOfferER,remoteDescSet{false};int peerHostNum,index;
    std::chrono::time_point<std::chrono::steady_clock> lastUpdateTime;
    qint64 lastStatsMs{0};//上次统计采集时刻(steady_clock毫秒),用于计算真实采集周期elapsed
    reportconfig reportCfg;//本通道的上报配置(server 下发,由 dcmanager 经 QueuedConnection 投递)
    int audioCallSampleRate{0};
    int audioCallChannelCount{0};
    QVector<QPair<QString,QString>> pendingCandidates;
    workerState state;
    workerState getState(){
        workerState cpy=state;
        state.reset();
        return cpy;}

public://Sources
    std::shared_ptr<rtc::PeerConnection> pc{NULL};
    std::shared_ptr<rtc::DataChannel> dc{NULL};
    QHash<QString,QPair<filedownloader*,QThread*>> fileContainer;

public://QTimer
    QTimer* sendTimer{NULL},*detectTimer{NULL};
    QTimer* processPendingTimer{NULL};
    QTimer* statsTimer{NULL};//统计自报定时器:由 dcmanager 经 applyReportConfig 按服务器下发的配置创建/启停

public://Buffer
    QMutex* inboundBufferMutex{NULL};
    std::vector<rtc::binary>& inboundBuffer;
    QStack<QByteArray>* pendingTun{nullptr};
    QQueue<QString>* pendingString{nullptr};
    QQueue<QByteArray>* pendingFile{nullptr};
    QQueue<QByteArray>* pendingFrame{nullptr};
    QQueue<QByteArray>* pendingAudio{nullptr};
    QByteArray* tempByteArrayPtr{nullptr};

public://Setting
    std::atomic<int> busySize=104857;
    std::atomic<int> freeSize=32768;

public://video/audioDecoder
    videodecoder* videoDeCoder{nullptr};
    QThread* deCoderTrd{nullptr};
    audiodecoder* audioDeCoder{nullptr};
    QThread* audioDeCoderTrd{nullptr};

public:
    dcworker(bool identity,int peerNum,int vecIndex,std::vector<rtc::binary>& inBuffer,QMutex* mtx,int bSize=104857,int fSize=32768)
        :isOfferER(identity),peerHostNum(peerNum),inboundBuffer(inBuffer),inboundBufferMutex(mtx),
        busySize(bSize),freeSize(fSize),
        tempByteArrayPtr(new QByteArray())
    {
        lastUpdateTime=std::chrono::steady_clock::now();
        index=vecIndex;
        switch(index)
        {
            case 0://主通道(tun/string/协商)
            {
                pendingTun=new QStack<QByteArray>;
                pendingString=new QQueue<QString>;
                pendingFile=new QQueue<QByteArray>;
                pendingAudio=new QQueue<QByteArray>;
                break;
            }
            case 1://文件传输
            {
                pendingFile=new QQueue<QByteArray>;
                break;
            }
            case 2://音频
            {
                pendingAudio=new QQueue<QByteArray>;
                break;
            }
            case 3://视频通话
            {
                pendingFrame=new QQueue<QByteArray>;
                break;
            }
        }
    }

public://toolFunction
    QJsonObject offerPacker(const QString& sdp)
    {
        QJsonObject offer;
        offer["type"]="sdp";
        offer["sdpType"]="offer";
        offer["sdp"]=sdp;
        offer["target"]=peerHostNum;
        offer["index"]=index;
        offer["initialOffer"]=((index==0)?1:0);
        return offer;
    }
    QJsonObject answerPacker(const QString& sdp)
    {
        QJsonObject answer;
        answer["type"]="sdp";
        answer["sdpType"]="answer";
        answer["sdp"]=sdp;
        answer["target"]=peerHostNum;
        answer["index"]=index;
        return answer;
    }
    QJsonObject candidatePacker(const QString& candidate,const QString& mid)
    {
        QJsonObject candidateJson;
        candidateJson["type"]="candidate";
        candidateJson["candidateItem"]=candidate;
        candidateJson["candidateMid"]=mid;
        candidateJson["target"]=peerHostNum;
        candidateJson["index"]=index;
        return candidateJson;
    }

public://dc.OnMsg.CALLBACK
    void onDcMsg(std::variant<rtc::binary, rtc::string> message)
    {
        peerAlive.store(true, std::memory_order_relaxed);
        if (std::holds_alternative<std::string>(message))
        {
            QString msg=QString::fromStdString(std::get<std::string>(message));
            if(msg!=QString("hb"))
                emit receiveStringMsg(peerHostNum, msg);
        }
        else
        {
            std::vector<std::byte> binaryMsg = std::get<rtc::binary>(message);
            int size=binaryMsg.size();
            if (!size) return;
            state.inBoundSpeed += size;
            const std::byte* bp=binaryMsg.data();
            switch((uint8_t)(*(bp++)))
            {
                case TYPE_NEGOTIATE://协商(请求/响应)
                {
                    uint8_t msgType;
                    //TYPE_FILE/TYPE_AUDIO/TYPE_VIDEO
                    //后续用于确认调用哪个函数来返回信息
                    //(例如如果是文件请求就用sendFileMsg返回结果)
                    std::memcpy(&msgType,bp,1);bp+=1;
                    uint8_t neogotiateSate;
                    std::memcpy(&neogotiateSate,bp,1);bp+=1;
                    //0拒绝/1同意(response)/10询问(request)/20中断(hangup)
                    if(neogotiateSate==10)
                    {
                        //msgType(1) + neogotieteType(1) + neogotiateSate(1) + requestTime(8) + json(N)
                        uint64_t requestTime;
                        std::memcpy(&requestTime,bp,8);bp+=8;
                        QJsonObject callParams;
                        int remainLength=binaryMsg.size()-(bp-binaryMsg.data());
                        if(remainLength>0)
                        {
                            QJsonDocument doc=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bp),remainLength));
                            if(doc.isObject())
                                callParams=doc.object();
                        }
                        emit transferRequest(msgType,requestTime,callParams,this);
                    }
                    else if(neogotiateSate==20)
                    {
                        if(msgType==TYPE_AUDIO)
                        {
                            isAudioCalling=false;
                            emit audioHangupReceived(peerHostNum);
                        }
                        else
                        {
                            isVideoCalling=false;
                            emit videoHangupReceived(peerHostNum);
                        }
                    }
                    else
                    {
                        uint64_t requestTime;
                        std::memcpy(&requestTime,bp,8);bp+=8;
                        emit returnRequestResult(requestTime,neogotiateSate);
                    }
                    break;
                }
                case TYPE_TUN://TUN隧道数据
                {
                    QMutexLocker locker(inboundBufferMutex);
                    inboundBuffer.emplace_back(binaryMsg.begin()+1,binaryMsg.end());
                    break;
                }
                case TYPE_FILE://文件分块传输
                {
                    const std::byte* bp=binaryMsg.data()+1;
                    uint64_t chunkIndex;
                    std::memcpy(&chunkIndex,bp,8);bp+=8;
                    uint64_t chunkAmount;
                    std::memcpy(&chunkAmount,bp,8);bp+=8;
                    uint8_t fileNameLength;
                    std::memcpy(&fileNameLength,bp,1);bp+=1;
                    std::byte* filename=(std::byte*)malloc(fileNameLength);
                    std::memcpy(filename,bp,fileNameLength);bp+=fileNameLength;
                    QString fileName=QString::fromUtf8((const char*)filename,fileNameLength);
                    if(!fileContainer.contains(fileName))
                    {
                        QMetaObject::invokeMethod(this,"startSingleFileDownLoader",Qt::BlockingQueuedConnection,Q_ARG(const QString&,fileName),Q_ARG(uint64_t,chunkAmount));
                        state.fileState.emplace_back(fileName,peerHostNum);
                    }
                    filedownloader* dl=fileContainer.value(fileName).first;
                    QMetaObject::invokeMethod(dl,"writeToChunkIndex",Qt::QueuedConnection,
                                              Q_ARG(uint64_t,chunkIndex),
                                              Q_ARG(const QByteArray&,QByteArray(
                                                                            (const char*)bp,binaryMsg.size()-(bp-binaryMsg.data())
                                                                            ))) ;
                    free(filename);
                    break;
                }
                case TYPE_VIDEO://视频帧
                {
                    if(!isShuttingDown && isVideoCalling)
                    {
                        if(!videoDeCoder)
                            QMetaObject::invokeMethod(this,"startVideoDeCoder",Qt::BlockingQueuedConnection);
                        int bytes=binaryMsg.size()-1;
                        std::byte* singleFrameData=(std::byte*)malloc(bytes);
                        std::memcpy(singleFrameData,bp,bytes);
                        QMetaObject::invokeMethod(videoDeCoder,"decodedFlood",Qt::QueuedConnection,Q_ARG(void*,(void*)singleFrameData),Q_ARG(int,bytes));
                    }
                    break;
                }
                case TYPE_AUDIO://音频帧
                {
                    if(!isShuttingDown && isAudioCalling)
                    {
                        //TYPE_AUDIO(1B) + sr_code(2B) + ch(1B) + opus_payload(NB)
                        if(binaryMsg.size() < 4)break;//帧太短=>丢弃

                        uint16_t srCode;
                        std::memcpy(&srCode, bp, 2); bp+=2;
                        uint8_t ch;
                        std::memcpy(&ch, bp, 1); bp+=1;
                        int remoteSr = srCode * 1000;
                        int remoteCh = ch;
                        int payloadBytes = binaryMsg.size() - (bp - binaryMsg.data());

                        //首帧=>创建 decoder
                        //后续帧=>仅当格式变化时重建(按现有代码逻辑(通话中途不可切换audioDevice)不可能发生)
                        if(!audioDeCoder || audioCallSampleRate != remoteSr || audioCallChannelCount != remoteCh)
                        {
                            if(audioDeCoder)
                            {
                                QMetaObject::invokeMethod(audioDeCoder,"shutdown",Qt::BlockingQueuedConnection);
                                audioDeCoder->deleteLater();
                                audioDeCoderTrd->quit();
                                audioDeCoder = nullptr;
                                audioDeCoderTrd = nullptr;
                            }
                            QMetaObject::invokeMethod(this,"startAudioDeCoder",Qt::BlockingQueuedConnection,
                                                      Q_ARG(int,remoteSr),Q_ARG(int,remoteCh));
                        }
                        std::byte* singleFrameData=(std::byte*)malloc(payloadBytes);
                        std::memcpy(singleFrameData,bp,payloadBytes);
                        QMetaObject::invokeMethod(audioDeCoder,"decodedFlood",Qt::QueuedConnection,
                                                  Q_ARG(void*,(void*)singleFrameData),Q_ARG(int,payloadBytes));
                    }
                    break;
                }
                case TYPE_JSON:
                {
                    QJsonDocument doc=QJsonDocument::fromJson(QByteArray((char*)(bp),binaryMsg.size()-1));
                    if(doc.isObject())
                        emit signalingBackUp(doc.object());
                        //针对两Peer间已经存在初始连接(dcworkerIndex=0)情况下复用tcpSignaling逻辑到dc(仅限离线状态,在线状态仍然走tcp)
                            //即dc0收到的信息转发到jsonworker复用networker的解析后逻辑
                        //发信
                            //SettingsDialog::applyConnectionCount=>dcmanager=>dcworker::createDc
                            //dcworker::sendSignalingMsg=>dcmanager=>mainwindow=>jsonworker::onInternalMsg(执行打包)+backSignaling=>回到dcworker(index0)
                            //dcworker(index0)::sendTunMsg(作为网卡数据发出)=>对端接收并解析为json
                        //收信解析
                            //dcworker::signalingBackUp=>dcmanager=>mainwindow=>jsonworker::onExternalMsg
                            //复用原tcpSignaling下游使用的goCreateAnswerER+goSetAnswer+goSetCandidate
                    break;
                }
            }
        }
    }

public slots://startDeCoderSlot
    void startVideoDeCoder()
    {
        (videoDeCoder=new videodecoder)->moveToThread(deCoderTrd=new QThread);
        connect(videoDeCoder,&videodecoder::sendDecodedFrame,this,[this](const QImage& frameImg){
            if(isVideoCalling)
                emit transferDecodedFrame(frameImg,peerHostNum);
        });
        deCoderTrd->start();
    }
    void startAudioDeCoder(int sr, int ch)
    {
        audioCallSampleRate = sr;
        audioCallChannelCount = ch;
        (audioDeCoder=new audiodecoder(sr,ch))->moveToThread(audioDeCoderTrd=new QThread);
        connect(audioDeCoder,&audiodecoder::sendDecodedAudio,this,[this](const QByteArray& pcmData){
            if(isAudioCalling)
                emit transferDecodedAudio(pcmData,peerHostNum);
        });
        connect(audioDeCoder, &audiodecoder::sendDecodedAudioLevel, this,
                [this](int level){
            if(isAudioCalling)
                emit transferDecodedAudioLevel(level, peerHostNum);
        });
        connect(audioDeCoderTrd,&QThread::finished,audioDeCoderTrd,&QThread::deleteLater);
        audioDeCoderTrd->start();
    }

public slots://startFileDownLoadSlot
    void startSingleFileDownLoader(const QString& fileName,uint64_t chunkAmount)
    {
        filedownloader* worker;QThread* trd;
        fileContainer.insert(fileName,QPair<filedownloader*,QThread*>(worker=new filedownloader(fileName,chunkAmount),trd=new QThread));
        connect(worker,&filedownloader::fileWriteFinished,this,[this,trd](const QString& fileName){
            emit informFileDownLoadFinish(fileName,peerHostNum);
            fileContainer.remove(fileName);
            trd->quit();
        });
        connect(trd,&QThread::finished,this,[worker,trd](){
            delete(worker);
            trd->deleteLater();
        });
        connect(worker,&filedownloader::fileWriteProgress,this,[this](const QString& fileName,qreal progress){
            QList<fileDownLoadState>& stateList=state.fileState;
            for(auto beg=stateList.begin();beg!=stateList.end();beg++)
                if(beg->filename==fileName)
                {
                    beg->progress=progress;
                    break;
                }
        });
        worker->moveToThread(trd);
        trd->start();
        worker->running = true;
    }
public slots://timerInitialSlot
    void initialPendingProcessTimer()
    {
        processPendingTimer=new QTimer(this);
        processPendingTimer->setInterval(100);
        connect(processPendingTimer,&QTimer::timeout,this,&dcworker::processPenddingMsg);
    }
public://sendFunctionRefactor
    void sendMsg(const QString& Msg)
    {
        if(dc&&dc->isOpen()&&dcValid)
        {
            try
            {
                dc->send(Msg.toStdString());
                if(dc->bufferedAmount()>busySize)
                {
                    isBufferBusy=true;
                    //仅当忙时开启自动消费pendingMsg
                    if(!processPendingTimer->isActive())
                        processPendingTimer->start();
                }
            } catch (const std::exception& e)
            {
                qWarning() << "dc string send failed:" << e.what();
                dcValid = false;
            }
        }
    }
    void sendMsg(const QByteArray& Msg)
    {
        if((std::chrono::duration_cast<std::chrono::milliseconds>(lastUpdateTime-std::chrono::steady_clock::now())).count()>=900)
        {
            if(index==0)
                state.pressure=pendingTun->size()+pendingFile->size()+pendingFrame->size();
            else if(index==1)
                state.pressure=pendingFile->size();
            lastUpdateTime=std::chrono::steady_clock::now();
        }
        if(dc&&dc->isOpen()&&dcValid)
        {
            try
            {
                dc->send((const rtc::byte*)Msg.data(), Msg.size());
                state.outBoundSpeed += Msg.size();
                if(dc->bufferedAmount()>busySize)
                {
                    isBufferBusy=true;
                    if(!processPendingTimer->isActive())
                        processPendingTimer->start();
                }
            } catch (const std::exception& e)
            {
                qWarning() << "dc binary send failed:" << e.what();
                dcValid = false;
            }
        }
    }

public://pendingDataFetcher
    bool getBinaryMsg()
    {//固定优先级: audio > video > tun > file
        if(pendingAudio&&!(pendingAudio->isEmpty()))
        {
            *tempByteArrayPtr=pendingAudio->dequeue();
            return true;
        }
        if(pendingFrame&&!(pendingFrame->isEmpty()))
        {
            *tempByteArrayPtr=pendingFrame->dequeue();
            return true;
        }
        if(pendingTun&&!(pendingTun->isEmpty()))
        {
            *tempByteArrayPtr=pendingTun->top();
            pendingTun->pop();
            return true;
        }
        if(pendingFile&&!(pendingFile->isEmpty()))
        {
            *tempByteArrayPtr=pendingFile->dequeue();
            return true;
        }
        return false;
    }

public slots://sendSlot
    void sendStringMsg(const QString& Msg)
    {
        if(isBufferBusy&&pendingString)
            pendingString->enqueue(Msg);
        else
            sendMsg(Msg);
    }
    void sendTunMsg(const QByteArray& Msg,bool predictNextEvent)
    {
        if(isBufferBusy)
            pendingTun->push(Msg);
        else
            sendMsg(Msg);
        if(!predictNextEvent)
            processPenddingMsg();
    }
    void sendFileMsg(const QByteArray& Msg,bool predictNextEvent)
    {
        if(isBufferBusy)
        {
            if(index==1)
                pendingFile->enqueue(Msg);
            else if(index==0)
                pendingTun->push(Msg);
        }
        else
            sendMsg(Msg);
        if (!predictNextEvent)
            processPenddingMsg();
    }
    void sendVideoMsg(const QByteArray& frame)
    {
        if(isBufferBusy)
            pendingFrame->enqueue(frame);
        else
            sendMsg(frame);
    }
    void sendAudioMsg(const QByteArray& audio)
    {
        if(isBufferBusy)
            pendingAudio->enqueue(audio);
        else
            sendMsg(audio);
    }
    void processPenddingMsg()
    {
        newEventNow=false;
        int binaryPktSize=0;
        bool msgIsValid=false;
        do
        {
            if(!isBufferBusy)
                if(dc&&dc->isOpen()&&dcValid)
                {
                    if(pendingString&&!pendingString->isEmpty())
                        sendMsg(pendingString->dequeue());
                    else
                    {
                        if(msgIsValid=getBinaryMsg())
                        {
                            sendMsg(*tempByteArrayPtr);
                            if(!processPendingTimer->isActive())
                                processPendingTimer->start();
                        }
                        else
                            if(processPendingTimer->isActive())
                                processPendingTimer->stop();//避免不必要空转
                        state.pressure=(pendingTun?pendingTun->size():0)+(pendingFile?pendingFile->size():0)
                        +(pendingAudio?pendingAudio->size():0)+(pendingFrame?pendingFrame->size():0);
                    }
                }
        }
        while(!newEventNow&&!isBufferBusy&&msgIsValid);
    }
public slots://bootSlot
    void createDc()
    {
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        pc= std::make_shared<rtc::PeerConnection>(config);
        pc->onLocalDescription([this](rtc::Description sdp) {
            if(isOfferER)
                emit sendSignalingMsg(offerPacker(QString::fromStdString(std::string(sdp))));
            else
                emit sendSignalingMsg(answerPacker(QString::fromStdString(std::string(sdp))));
        });
        pc->onLocalCandidate([this](rtc::Candidate candidate) {
            emit sendSignalingMsg(candidatePacker(QString::fromStdString(std::string(candidate)),QString::fromStdString(candidate.mid())));
        });
        if(isOfferER)
        {
            dc=pc->createDataChannel("P2PConnection");
            dc->onOpen([this](){
                emit dcConnected(peerHostNum);
                QMetaObject::invokeMethod(this,"vade",Qt::QueuedConnection);
                dcValid=true;
                dc->setBufferedAmountLowThreshold(freeSize);
                dc->onBufferedAmountLow([this]() {
                    isBufferBusy=false;
                });
                dc->onMessage([this](std::variant<rtc::binary, rtc::string> message){
                    onDcMsg(message);
                });
                dc->onClosed([this](){
                    dcValid=false;
                    QMetaObject::invokeMethod(this, "shutdown", Qt::QueuedConnection);
                });
            });
        }
        else
        {
            pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> incoming) {
                dc=incoming;
                dc->onOpen([this](){
                    QMetaObject::invokeMethod(this,"vade",Qt::QueuedConnection);
                    dcValid=true;
                    dc->setBufferedAmountLowThreshold(freeSize);
                    dc->onBufferedAmountLow([this](){
                        isBufferBusy=false;
                    });
                    dc->onMessage([this](std::variant<rtc::binary, rtc::string> message){
                        onDcMsg(message);
                    });
                    dc->onClosed([this](){
                        dcValid=false;
                        QMetaObject::invokeMethod(this, "shutdown", Qt::QueuedConnection);
                    });
                });
            });
        }
    }

public slots://runningTimeSlot
    void setRemoteSdp(const QString& sdp,const QString& sdpType) {
        if (pc && !sdp.isEmpty())
        {
            auto currentState = pc->signalingState();
            if((sdpType == "answer" && currentState == rtc::PeerConnection::SignalingState::HaveLocalOffer)
                ||(
                sdpType == "offer" && currentState == rtc::PeerConnection::SignalingState::Stable))
            {
                rtc::Description description=rtc::Description(sdp.toStdString(),((sdpType == "offer") ?rtc::Description::Type::Offer:rtc::Description::Type::Answer));
                pc->setRemoteDescription(description);
                remoteDescSet=true;
                // 刷新缓冲的 candidate
                for(const auto& c : pendingCandidates)
                    pc->addRemoteCandidate(rtc::Candidate(c.first.toStdString(),c.second.toStdString()));
                pendingCandidates.clear();
                if(description.type()==rtc::Description::Type::Offer)
                    pc->setLocalDescription();
            }
            else
                qWarning() << "[DC] State check FAILED!";
        }
        else
            qWarning() << "[DC] setRemoteSdp: pc is null or sdp is empty!";
    }
    void receiveCandidate(const QString &sdp, const QString &mediaType){
        if(pc&&!sdp.isNull()&&!mediaType.isNull())
        {
            //远端offerER传递offer/candidate=>本地dcManager收到offer/candidate=>addPeer创建answerER并返回dcworker*
            //=>异步投递setRemoteSdp事件
            if(remoteDescSet)
                pc->addRemoteCandidate(rtc::Candidate(sdp.toStdString(), mediaType.toStdString()));
            else
                pendingCandidates.append(qMakePair(sdp,mediaType));
        }
        else
            qWarning() << "[DC] receiveCandidate: pc is null or params are null!";
    };

public slots://heartbeatSlot
    void vade()
    {
        if(dc&&dc->isOpen())
        {
            sendTimer=new QTimer(this);
            sendTimer->setInterval(1500);
            connect(sendTimer,&QTimer::timeout,[this](){
                sendStringMsg("hb");
            });
            sendTimer->start();
            detectTimer=new QTimer(this);
            detectTimer->setInterval(3000);
            connect(detectTimer,&QTimer::timeout,[this](){
                if(!peerAlive)
                {
                    dcValid=false;
                    dc->onMessage(nullptr);
                    sendTimer->stop();
                    detectTimer->stop();
                    shutdown();
                }
                else
                    peerAlive=false;
            });
            detectTimer->start();
        }
    }

public slots://shutSlot
    void shutdown()
    {
        if (isShuttingDown.exchange(true)) return;
        if(videoDeCoder)
        {
            videoDeCoder->deleteLater();
            deCoderTrd->quit();
            deCoderTrd->wait();
            deCoderTrd->deleteLater();
            videoDeCoder=nullptr;
            deCoderTrd=nullptr;
        }
        if(audioDeCoder)
        {
            audioDeCoder->deleteLater();
            audioDeCoderTrd->quit();
            audioDeCoderTrd->wait();
            audioDeCoderTrd->deleteLater();
            audioDeCoder=nullptr;
            audioDeCoderTrd=nullptr;
        }
        if(!fileContainer.isEmpty())
            for(auto& pr:fileContainer.values())
            {
                pr.first->running=false;
                pr.first->deleteLater();
                pr.second->quit();
                pr.second->wait();
            }
        if(sendTimer)
        {
            if(sendTimer->isActive())
                sendTimer->stop();
            sendTimer->deleteLater();
            sendTimer=nullptr;
        }
        if(detectTimer)
        {
            if(detectTimer->isActive())
                detectTimer->stop();
            detectTimer->deleteLater();
            detectTimer=nullptr;
        }
        if(processPendingTimer)
        {
            if(processPendingTimer->isActive())
                processPendingTimer->stop();
            processPendingTimer->deleteLater();
            processPendingTimer=nullptr;
        }
        if(dc)
        {
            dc->onMessage(nullptr);
            dc->onOpen(nullptr);
            dc->onClosed(nullptr);
        }
        if (dc)
        {
            dc->close();
            dc.reset();
        }
        if (pc)
        {
            pc->close();
            pc.reset();
        }
        emit dcFinish();
    }

public slots://settingSlot
    void updateSettings(int bSize, int fSize)
    {busySize = bSize;
    freeSize = fSize;}

public slots://statsSlot
    //pc+dc状态包装到Json->statsCollected信号(本worker线程内的statsTimer直调,无跨线程调用)
    //产出内容完全由 reportCfg 决定:fields 为空即裸边,只报 {peer,ch}
    //缺省语义:未配置的字段组根本不写这个 key,由 server 依"所有质量字段皆缺省"判定裸边
    QJsonObject collectStats()
    {
        if(isShuttingDown||!pc||!dc)
            return {};
        if(!reportCfg.enabled)
            return {};//未下发配置(或已被 server 关闭):不产生任何数据
        QJsonObject edge;
        edge["peer"]=peerHostNum;
        edge["ch"]=index;//worker index:0主通道/1文件/2音频/3视频
        if(reportCfg.bare())
            return edge;//裸边:只表达拓扑存在性
        qint64 nowMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(lastStatsMs>0)
            edge["elapsed"]=nowMs-lastStatsMs;//真实采集周期(ms):供manager差分速率,不依赖定时器名义间隔
        lastStatsMs=nowMs;
        if(reportCfg.hasField("rtt"))
            if(auto rtt=pc->rtt())
                edge["rtt"]=static_cast<int>(rtt->count());
        if(reportCfg.hasField("traffic"))
        {
            edge["bytesSent"]=static_cast<qint64>(pc->bytesSent());
            edge["bytesReceived"]=static_cast<qint64>(pc->bytesReceived());
        }
        if(reportCfg.hasField("buffered"))
            edge["buffered"]=static_cast<qint64>(dc->bufferedAmount());
        if(reportCfg.hasField("state"))
        {
            switch(pc->state())
            {
                case rtc::PeerConnection::State::New:
                 edge["state"]="new";break;
                case rtc::PeerConnection::State::Connecting:
                 edge["state"]="connecting";break;
                case rtc::PeerConnection::State::Connected:
                 edge["state"]="connected";break;
                case rtc::PeerConnection::State::Disconnected:
                 edge["state"]="disconnected";break;
                case rtc::PeerConnection::State::Failed:
                 edge["state"]="failed";break;
                case rtc::PeerConnection::State::Closed:
                 edge["state"]="closed";break;
            }
        }
        if(reportCfg.hasField("ice"))
        {
            switch(pc->iceState())
            {
                case rtc::PeerConnection::IceState::New: edge["iceState"]="new";break;
                case rtc::PeerConnection::IceState::Checking: edge["iceState"]="checking";break;
                case rtc::PeerConnection::IceState::Connected: edge["iceState"]="connected";break;
                case rtc::PeerConnection::IceState::Completed: edge["iceState"]="completed";break;
                case rtc::PeerConnection::IceState::Failed: edge["iceState"]="failed";break;
                case rtc::PeerConnection::IceState::Disconnected: edge["iceState"]="disconnected";break;
                case rtc::PeerConnection::IceState::Closed: edge["iceState"]="closed";break;
            }
        }
        if(reportCfg.hasField("path"))
        {
            //链路层级判定:以"对端候选地址是否落在本机网卡网段"为准,而非候选类型
            //(单看类型会误判:同机或同网段互通一旦选中 srflx 候选,类型就不是 host,可实际根本没走公网)
            //relay=中继(打洞失败降级,当前无TURN不会出现,留作告警位)
            rtc::Candidate localCandidate,remoteCandidate;
            if(pc->getSelectedCandidatePair(&localCandidate,&remoteCandidate))
            {
                if(localCandidate.type()==rtc::Candidate::Type::Relayed||remoteCandidate.type()==rtc::Candidate::Type::Relayed)
                    edge["netPath"]="relay";
                else if(isInLocalSubnet(candidateAddress(remoteCandidate)))
                    edge["netPath"]="lan";
                else
                    edge["netPath"]="wan";
                //诊断:选中候选对的实际地址与类型 —— 用于排查"单机双开却显示公网"这类判定异常
                edge["candLocal"]=candidateBrief(localCandidate);
                edge["candRemote"]=candidateBrief(remoteCandidate);
            }
        }
        return edge;
    }
    //候选地址(libdatachannel 的 address() 是 optional,未解析时返回空串)
    static QString candidateAddress(const rtc::Candidate& c)
    {
        if(auto addr=c.address())
            return QString::fromStdString(*addr);
        return QString();
    }
    //候选摘要:"地址:端口/类型",类型映射 host/srflx/prflx/relay
    static QString candidateBrief(const rtc::Candidate& c)
    {
        QString type="unknown";
        switch(c.type())
        {
            case rtc::Candidate::Type::Host: type="host";break;
            case rtc::Candidate::Type::ServerReflexive: type="srflx";break;
            case rtc::Candidate::Type::PeerReflexive: type="prflx";break;
            case rtc::Candidate::Type::Relayed: type="relay";break;
            default: break;
        }
        QString addr=candidateAddress(c);
        auto port=c.port();
        return QString("%1:%2/%3").arg(addr.isEmpty()?QString("?"):addr)
                                 .arg(port?QString::number(*port):QString("?")).arg(type);
    }
    //应用 server 下发的上报配置(由 dcmanager 经 QueuedConnection 投递,在本线程创建/启停statsTimer)
    //开关、周期、字段全部来自配置 —— 客户端不提供本地调控入口,保证同一 edge 两端口径一致
    void applyReportConfig(const QJsonObject& cfg)
    {
        reportCfg.load(cfg);
        if(!reportCfg.enabled)
        {
            if(statsTimer)
            {
                statsTimer->stop();
                lastStatsMs=0;//停报后重新开启时首条无elapsed,重建速率基线
            }
            return;
        }
        if(!statsTimer)
        {
            statsTimer=new QTimer(this);
            connect(statsTimer,&QTimer::timeout,this,[this](){
                QJsonObject edge=collectStats();
                if(!edge.isEmpty())
                    emit statsCollected(edge);
            });
        }
        statsTimer->setInterval(reportCfg.intervalMs);
        statsTimer->start();
    }
signals:
    void dcFinish();
    void statsCollected(const QJsonObject&);//本worker的统计快照(推模型:worker自采自报,dcmanager聚合)
    void receiveStringMsg(int peerHostNum, const QString& msg);
    void informFileDownLoadFinish(const QString& filename,int peerHostNum);
    void transferDecodedFrame(const QImage&,int);
    void transferDecodedAudio(const QByteArray& pcmData,int peerHostNum);
    void transferDecodedAudioLevel(int level,int peerHostNum);
    void transferRequest(uint8_t msgType,uint64_t requestTime,const QJsonObject& callParams,void* voidDCWorker);
    void returnRequestResult(uint64_t requestTime,bool result);
    void videoHangupReceived(int peerHostNum);
    void audioHangupReceived(int peerHostNum);
    void dcConnected(int peerHostNum);

    void sendSignalingMsg(const QJsonObject&);
    void signalingBackUp(const QJsonObject&);
};
#endif
