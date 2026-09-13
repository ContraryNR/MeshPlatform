#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QJsonObject>
#include <QMediaDevices>
#include "audiocapture.h"

//1.发起视频通话请求=>协商通话参数并保存到misson(std::functional)
void MainWindow::onAppealVideoCallRequest()
{
    if (currentPeerHostNum == 0)
    {
        QMessageBox::warning(this, "提示", "请先选择一个连接节点");
        return;
    }
    if(!isWorkerReady(currentPeerHostNum,TYPE_VIDEO,ipRoute))
    {
        ui->stateMsg->appendPlainText("向该Peer的连接数不足 无法进行视频通话 请尝试设置向该Peer的对外连接数为不小于3的数");
        return;
    }
    int vw=640,vh=480,vf=15,asr=48000,acc=1,noiseGate=2;
    QAudioDevice audioDev;
    InitiativeSessionRequestDialog dlg("视频通话参数设置", true, vw, vh, vf, asr, acc, noiseGate, this);
    if(dlg.exec() != QDialog::Accepted)
        return;
    vw=dlg.videoWidth(); vh=dlg.videoHeight(); vf=dlg.videoFps();
    asr=dlg.audioSampleRate(); acc=dlg.audioChannelCount();
    noiseGate=dlg.noiseGate(); audioDev=dlg.audioDevice();
    QAudioDevice dev = audioDev.isNull() ? QMediaDevices::defaultAudioInput() : audioDev;
    QAudioFormat probeFmt;
    probeFmt.setSampleRate(asr);
    probeFmt.setChannelCount(acc);
    probeFmt.setSampleFormat(QAudioFormat::Int16);
    if(!audiocapture::negotiateAudio(dev, probeFmt))
        return;
    int actualAsr = probeFmt.sampleRate();
    int actualAcc = probeFmt.channelCount();
    dcworker* worker=getDcWorker(ipRoute,currentPeerHostNum,TYPE_VIDEO);
    if(worker)
    {
        uint64_t requestTime=getRunningTime();
        QJsonObject requestJson;
        requestJson["explain"]            = QString("对方(%1)发起了视频通话请求").arg(localHostName);
        requestJson["videoWidth"]         = vw;
        requestJson["videoHeight"]        = vh;
        requestJson["videoFps"]           = vf;
        requestJson["audioSampleRate"]    = actualAsr;
        requestJson["audioChannelCount"]  = actualAcc;
        QMetaObject::invokeMethod(worker,"sendVideoMsg",Qt::QueuedConnection,
                                  Q_ARG(const QByteArray&,createRequest(TYPE_VIDEO, requestTime, requestJson)));
        mission.emplace(requestTime,this,
                        [this,planedCallingHostNum=currentPeerHostNum,vw,vh,vf,actualAsr,actualAcc,audioDev,noiseGate]{
                            if(ipRoute)
                                if(ipRoute->contains(planedCallingHostNum))
                                    initialVideoChatRoute(planedCallingHostNum,vw,vh,vf,actualAsr,actualAcc,audioDev,noiseGate);
                        });
        ui->stateMsg->appendPlainText(QString("已发送视频通话请求 => 待对方确认 视频:%1x%2@%3fps 音频:%4Hz/%5ch")
                                          .arg(vw).arg(vh).arg(vf).arg(actualAsr).arg(actualAcc));
    }
    else
        ui->stateMsg->appendPlainText("向该Peer的连接数不足 无法进行视频通话 请尝试设置向该Peer的对外连接数为不小于3的数");
}
//2.1初始化视频编码器
void MainWindow::initialVideoEncoder()
{
    if(videoEnCoder)
        return;
    (videoEnCoder=new videoencoder)->moveToThread(trd[VE]=new QThread);
    connect(trd[VE],&QThread::finished,trd[VE],&QThread::deleteLater);
    trd[VE]->start();
    QMetaObject::invokeMethod(videoEnCoder,"startFloodTimer",Qt::QueuedConnection);
    connect(videoEnCoder, &videoencoder::sendEncodedFrame, this, [this](const QImage& localFrame, const QByteArray& encodedData){
        for(VideoChatWindow* w : videoChatSessions)
            if(w)
                w->showLocalVideo(localFrame);
        if(ipRoute)
            for(dcworker* worker:getVideoCallingPeerWorkers(ipRoute,videoChatSessions.keys()))
                QMetaObject::invokeMethod(worker, "sendVideoMsg", Qt::QueuedConnection,Q_ARG(const QByteArray&, encodedData));
    });
}
//2.2初始化视频通话窗口
void MainWindow::initialVideoChatWindow(int peerHostNum,int asr,int acc)
{
    if(VideoChatWindow* existingWindow=videoChatSessions.value(peerHostNum,nullptr))
    {
        existingWindow->show();
        existingWindow->activateWindow();
        return;
    }
    VideoChatWindow* videoWindow = new VideoChatWindow(asr, acc, this);
    connect(videoWindow, &VideoChatWindow::hangUpClicked, this, [this,peerHostNum]() {
        onEndVideoChat(peerHostNum);
    });
    connect(videoWindow, &VideoChatWindow::muteToggled, this, [this,peerHostNum](bool muted){
        onAudioChatMuteToggled(peerHostNum, muted);
    });
    connect(videoWindow, &VideoChatWindow::topMuteToggled, this, [this](bool muted){
        onTopMuteToggled(muted);
    });
    videoChatSessions.insert(peerHostNum, videoWindow);
    updateCallButtonState();
    videoWindow->setPeerName(peerNames.value(peerHostNum, "未知"));
    videoWindow->setMuteState(false);
    videoWindow->setTopMuteState(false);
    videoWindow->startDurationTimer();
    videoWindow->show();
}
//2.0连起来初始化多个模块
void MainWindow::initialVideoChatRoute(int callingHostNum,int vw,int vh,int vf,int asr,int acc,const QAudioDevice& audioDev,int noiseGate)
{
    initialVideoEncoder();
    initialAudioEncoder(callingHostNum,asr,acc,audioDev,noiseGate);
    initialVideoChatWindow(callingHostNum,asr,acc);
    updateDcWorkerCallingState(false,true,callingHostNum);
    ui->stateMsg->appendPlainText(QString("已启动与 %1 的视频通话 视频:%2x%3@%4fps 音频:%5Hz/%6ch")
                                      .arg(peerNames.value(callingHostNum,"未知"))
                                      .arg(vw).arg(vh).arg(vf).arg(asr).arg(acc));
}
//3.接收到转发自dcworker的videoFrame
void MainWindow::onDecodedFrame(const QImage & frameImg, int peerHostNum)
{
    if (!videoChatSessions.contains(peerHostNum))
    {
        initialVideoEncoder();
        initialVideoChatWindow(peerHostNum);
    }
    if (VideoChatWindow* videoWindow=videoChatSessions.value(peerHostNum,nullptr))
        videoWindow->showRemoteVideo(frameImg, peerHostNum);
}
//4.释放指定session的videoChatWindow
void MainWindow::cleanupVideoChatSession(int peerHostNum)
{
    if (!videoChatSessions.contains(peerHostNum))
        return;
    VideoChatWindow* videoWindow = videoChatSessions.take(peerHostNum);
    if (videoWindow)
    {
        videoWindow->close();
        videoWindow->deleteLater();
    }
    QString peerName = peerNames.value(peerHostNum, "未知");
    ui->stateMsg->appendPlainText(QString("与 %1 的视频通话已结束").arg(peerName));
}
//5.当生产者(videoEncoder+audioCapture/audioEncoder)无下游消费者时销毁相应管线
void MainWindow::cleanupVideoSessionPipeline()
{
    if(videoEnCoder)
        QMetaObject::invokeMethod(videoEnCoder, "shutdown", Qt::BlockingQueuedConnection);
    if(trd[VE])
    {
        trd[VE]->quit();
        trd[VE]->wait();
        trd[VE] = nullptr;
    }
    if(videoEnCoder)
    {
        delete videoEnCoder;
        videoEnCoder = nullptr;
    }
}
//6.抽象videoSession窗口资源释放接口(必要时释放encoder/pipeline)
void MainWindow::shutVideoSession(int peerHostNum)
{
    updateDcWorkerCallingState(false, false, peerHostNum);
    cleanupVideoChatSession(peerHostNum);
    if(!checkVeNecessity())
        if(videoEnCoder)
            cleanupVideoSessionPipeline();
    if(!checkAeNecessity())
        if(audioCapture)
            cleanupAudioSessionPipeline();
    updateCallButtonState();
}
//7.1本端主动结束视频通话
void MainWindow::onEndVideoChat(int peerHostNum)
{
    sendHangupMsg(peerHostNum,3,TYPE_VIDEO,"sendVideoMsg");
    shutVideoSession(peerHostNum);
}
//7.2对端挂断通话
void MainWindow::onRemoteVideoHangup(int peerHostNum)
{
    shutVideoSession(peerHostNum);
}
//7.3关闭软件时释放全部session
void MainWindow::shutAllVideoSession()
{
    if(videoChatSessions.isEmpty()&&!videoEnCoder)
        return;
    for(int peerHostNum : videoChatSessions.keys())
        onEndVideoChat(peerHostNum);
    videoChatSessions.clear();
}