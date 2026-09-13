#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QJsonObject>
#include <QMediaDevices>
#include "audiocapture.h"

//1.发起音频通话请求
void MainWindow::onAppealAudioCallRequest()
{
    if(currentPeerHostNum == 0)
    {
        QMessageBox::warning(this, "提示", "请先选择一个连接节点");
        return;
    }
    if(!isWorkerReady(currentPeerHostNum, TYPE_AUDIO, ipRoute))
    {
        ui->stateMsg->appendPlainText("向该Peer的连接数不足 无法进行语音通话 请尝试设置向该Peer的对外连接数为不小于3的数");
        return;
    }
    int asr=48000,acc=1,noiseGate=2;
    QAudioDevice audioDev;
    InitiativeSessionRequestDialog dlg("音频通话参数设置", false, 0, 0, 0, asr, acc, noiseGate, this);
    if(dlg.exec() != QDialog::Accepted)
        return;
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
    dcworker* worker = getDcWorker(ipRoute, currentPeerHostNum, TYPE_AUDIO);
    if(worker)
    {
        uint64_t requestTime=getRunningTime();
        QJsonObject requestJson;
        requestJson["explain"]            = QString("对方(%1)发起了语音通话请求").arg(localHostName);
        requestJson["audioSampleRate"]    = actualAsr;
        requestJson["audioChannelCount"]  = actualAcc;
        QMetaObject::invokeMethod(worker, "sendAudioMsg", Qt::QueuedConnection,
                                  Q_ARG(const QByteArray&, createRequest(TYPE_AUDIO, requestTime, requestJson)));
        mission.emplace(requestTime,this,
            [this,planedCallingHostNum=currentPeerHostNum,actualAsr,actualAcc,audioDev,noiseGate]{
                if(ipRoute)
                    if(ipRoute->contains(planedCallingHostNum))
                        initialAudioChatRoute(planedCallingHostNum,actualAsr,actualAcc,audioDev,noiseGate);
            });
        ui->stateMsg->appendPlainText(QString("已发送语音通话请求 => 待对方确认 音频:%1Hz/%2ch")
                                      .arg(actualAsr).arg(actualAcc));
    }
}
//2.1初始化音频编码器
void MainWindow::initialAudioEncoder(int callingHostNum,int asr,int acc,const QAudioDevice& audioDev,int noiseGate)
{
    if(audioCapture && audioEnCoder)
        return;
    (audioCapture=new audiocapture(asr, acc, 20, audioDev, noiseGate))
        ->moveToThread(trd[AC]=new QThread);
    connect(trd[AC],&QThread::finished,trd[AC],&QThread::deleteLater);
    int actualSr = audioCapture->sampleRate;
    int actualCh = audioCapture->channelCount;
    (audioEnCoder=new audioencoder(actualSr, actualCh, 32000))
        ->moveToThread(trd[AE]=new QThread);
    connect(trd[AE],&QThread::finished,trd[AE],&QThread::deleteLater);
    trd[AC]->start();
    trd[AE]->start();
    QMetaObject::invokeMethod(audioCapture,"startFloodTimer",Qt::QueuedConnection);
    connect(audioCapture, &audiocapture::sendPcmFrame, audioEnCoder, &audioencoder::encodeFlood);
    connect(audioCapture, &audiocapture::sendPcmLevel, this,
            [this](int level){
                for(AudioChatWindow* w : audioChatSessions)
                    if(w)
                        w->pushLocalLevel(level);
            });
    dcworker* audioWorker = (ipRoute && ipRoute->contains(callingHostNum))
                                ? getDcWorker(ipRoute, callingHostNum, TYPE_AUDIO) : nullptr;
    if(audioWorker)
    {
        audioEncoderConnections[callingHostNum] = connect(
            audioEnCoder, &audioencoder::sendEncodedAudio, this,
            [audioWorker](const QByteArray& encodedData){
                QMetaObject::invokeMethod(audioWorker, "sendAudioMsg", Qt::QueuedConnection,
                                          Q_ARG(const QByteArray&, encodedData));
            });
    }
}
//2.2初始化音频通话窗口
void MainWindow::initialAudioChatWindow(int peerHostNum,int asr,int acc)
{
    if(AudioChatWindow* existingWindow = audioChatSessions.value(peerHostNum, nullptr))
    {
        existingWindow->show();
        existingWindow->activateWindow();
        return;
    }
    AudioChatWindow* audioWindow = new AudioChatWindow(asr, acc, this);
    connect(audioWindow, &AudioChatWindow::hangUpClicked, this, [this, peerHostNum](){
        onEndAudioChat(peerHostNum);
    });
    connect(audioWindow, &AudioChatWindow::muteToggled, this, [this, peerHostNum](bool muted){
        onAudioChatMuteToggled(peerHostNum, muted);
    });
    connect(audioWindow, &AudioChatWindow::topMuteToggled, this, [this](bool muted){
        onTopMuteToggled(muted);
    });
    audioChatSessions.insert(peerHostNum, audioWindow);
    audioWindow->setPeerName(peerNames.value(peerHostNum, QString("Host_%1").arg(peerHostNum)));
    audioWindow->setMuteState(false);
    audioWindow->setTopMuteState(false);
    audioWindow->startDurationTimer();
    audioWindow->show();
}
//2.0初始化音频通话的多个模块
void MainWindow::initialAudioChatRoute(int callingHostNum,int asr,int acc,const QAudioDevice& audioDev,int noiseGate)
{
    updateDcWorkerCallingState(true, true, callingHostNum);
    initialAudioEncoder(callingHostNum,asr,acc,audioDev,noiseGate);
    initialAudioChatWindow(callingHostNum,asr,acc);
    ui->stateMsg->appendPlainText(QString("已与 %1 建立音频通话 音频:%2Hz/%3ch")
                                  .arg(peerNames.value(callingHostNum,"未知"))
                                  .arg(asr).arg(acc));
    updateCallButtonState();
}
//3.1接收中转自dcmanager的音频推流
void MainWindow::onDecodedAudioFlood(const QByteArray& pcmData,int peerHostNum)
{
    if(AudioChatWindow* audioWindow = audioChatSessions.value(peerHostNum, nullptr))
        audioWindow->playFlood(pcmData);
    else if(VideoChatWindow* videoWindow = videoChatSessions.value(peerHostNum, nullptr))
        videoWindow->playFlood(pcmData);
}
//3.2接收对端PCMLevel推流
void MainWindow::onRemoteAudioLevel(int level,int peerHostNum)
{
    if(!audioChatSessions.contains(peerHostNum))
        return;
    AudioChatWindow* audioWindow = audioChatSessions.value(peerHostNum);
    if(audioWindow)
        audioWindow->pushRemoteLevel(level);
}
//4.1顶层audioCapture向audioEncoder的推流总开关切换
void MainWindow::onAudioChatMuteToggled(int callingPeerHostNum,bool muted)
{
    togglePeerAudioSend(callingPeerHostNum, muted);
    ui->stateMsg->appendPlainText(muted ? "[静音] 本端停止向对端发送音频" : "[取消静音] 恢复发送音频");
}
void MainWindow::onTopMuteToggled(bool muted)
{
    if(!audioCapture)
        return;
    QMetaObject::invokeMethod(audioCapture, "setMuted", Qt::QueuedConnection, Q_ARG(bool, muted));
    syncTopMuteStateToAllWindows(muted);
    ui->stateMsg->appendPlainText(muted ? "[关闭麦克风] 本端麦克风已关闭" : "[打开麦克风] 本端麦克风已恢复");
}
void MainWindow::syncTopMuteStateToAllWindows(bool muted)
{
    for(AudioChatWindow* w : audioChatSessions)
        if(w) w->setTopMuteState(muted);
    for(VideoChatWindow* w : videoChatSessions)
        if(w) w->setTopMuteState(muted);
}
//4.2顶层audioEncoder向下层dcworker的子推流开关切换(基于'连接'容器)
void MainWindow::togglePeerAudioSend(int peerHostNum, bool muted)
{
    if(!audioEncoderConnections.contains(peerHostNum))
        return;
    if(muted)
        disconnect(audioEncoderConnections[peerHostNum]);
    else
    {
        dcworker* audioWorker = (ipRoute && ipRoute->contains(peerHostNum))
                                    ? getDcWorker(ipRoute, peerHostNum, TYPE_AUDIO) : nullptr;
        if(audioWorker && audioEnCoder)
            audioEncoderConnections[peerHostNum] = connect(
                audioEnCoder, &audioencoder::sendEncodedAudio, this,
                [audioWorker](const QByteArray& encodedData){
                    QMetaObject::invokeMethod(audioWorker, "sendAudioMsg", Qt::QueuedConnection,
                                              Q_ARG(const QByteArray&, encodedData));
                });
    }
}
//5.1释放指定session的audioChatWindow及编码器连接
void MainWindow::cleanupAudioChatSession(int peerHostNum)
{
    if(audioChatSessions.contains(peerHostNum))
    {
        AudioChatWindow* audioWindow = audioChatSessions.take(peerHostNum);
        if(audioWindow)
        {
            audioWindow->close();
            audioWindow->deleteLater();
        }
        QString peerName = peerNames.value(peerHostNum,"未知");
        ui->stateMsg->appendPlainText(QString("与 %1 的音频通话已结束").arg(peerName));
    }
    if(audioEncoderConnections.contains(peerHostNum))
    {
        disconnect(audioEncoderConnections[peerHostNum]);
        audioEncoderConnections.remove(peerHostNum);
    }
}
//5.2释放全部音频通话资源
void MainWindow::cleanupAudioSessionPipeline()
{
    for(const auto& conn:audioEncoderConnections)
        disconnect(conn);
    audioEncoderConnections.clear();
    if(audioCapture)
        QMetaObject::invokeMethod(audioCapture, "shutdown", Qt::BlockingQueuedConnection);
    if(audioEnCoder)
        QMetaObject::invokeMethod(audioEnCoder, "shutdown", Qt::BlockingQueuedConnection);
    if(trd[AC])
    {
        trd[AC]->quit();
        trd[AC]->wait();
        trd[AC] = nullptr;
    }
    if(trd[AE])
    {
        trd[AE]->quit();
        trd[AE]->wait();
        trd[AE] = nullptr;
    }
    if(audioCapture)
    {
        delete audioCapture;
        audioCapture = nullptr;
    }
    if(audioEnCoder)
    {
        delete audioEnCoder;
        audioEnCoder = nullptr;
    }
}
//6.释放指定音频session
void MainWindow::shutAudioSession(int peerHostNum)
{
    updateDcWorkerCallingState(true, false, peerHostNum);
    cleanupAudioChatSession(peerHostNum);
    if(!checkAeNecessity())
        if(audioCapture)//audioEncoder可判可不判
            cleanupAudioSessionPipeline();
    updateCallButtonState();
}
//7.1本端主动挂断音频会话(发信令+释放)
void MainWindow::onEndAudioChat(int peerHostNum)
{
    sendHangupMsg(peerHostNum,2,TYPE_AUDIO,"sendAudioMsg");
    shutAudioSession(peerHostNum);
}
//7.2对端挂断音频通话(仅释放)
void MainWindow::onRemoteAudioHangup(int peerHostNum)
{
    shutAudioSession(peerHostNum);
}
//7.3关闭软件时释放全部音频session
void MainWindow::shutAllAudioSession()
{
    if(audioChatSessions.isEmpty()&&!audioCapture&&!audioEnCoder)
        return;
    for(int peerHostNum : audioChatSessions.keys())
        onEndAudioChat(peerHostNum);
    audioChatSessions.clear();
}