#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QInputDialog>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QMessageBox>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFrame>
#include <QTime>
#include <QJsonObject>
#include <QJsonDocument>
#include "peernetworker.h"
#include "coornetworker.h"
#include "dcmanager.h"
#include "tunloader.h"
#include "tunmanager.h"
#include "tuninworker.h"
#include "tunoutworker.h"
#include "ui_mainwindow.h"
#include "filesender.h"
#include "videochatwindow.h"
#include "audiochatwindow.h"
#include "coorjsonworker.h"
#include "peerjsonworker.h"
#include "jsonloader.h"
#include "videoencoder.h"
#include "videodecoder.h"
#include "audiocapture.h"
#include "audioencoder.h"
#include "audiodecoder.h"
#include "netconfig.h"
#include <QTime>
#include "topbasedialog.h"
#include "sessionbasedialog.h"
#include "passivesessionrequestdialog.h"
#include "initiativesessionrequestdialog.h"
#include "filerequestdialog.h"

#define trdAmount 8
#define NET 0//TcpNetWorker
#define DC 1//LibDataChannelWorker
#define TIN 2//WinTunInboundWorker
#define TOUT 3//WinTunOutboundWorker
#define JW 4//JsonWorker
#define VE 5//VideoEncoder
#define AC 6//AudioCapture
#define AE 7//AudioEncoder

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public://Flags
    bool isCoordinator;
    bool onlineMode;
    bool isClosing=false;
    int currentPeerHostNum=0;
    int currentRow=-1;
    uint64_t startTime;
    netConfig currentNetConf;
    QString localHostName;
    int localHostNum;
public://MainThreadWorker
    tunloader* tunLoader{NULL};
    tunmanager* tunManager{NULL};
    jsonloader* jsonLoader{NULL};
public://QThreadWorker
    coornetworker* serverNetWorker{NULL};
    peernetworker* clientNetWorker{NULL};
    coorjsonworker* coorJsonWorker{NULL};
    peerjsonworker* peerJsonWorker{NULL};
    dcmanager* dcManager{NULL};
    tuninworker* tunInWorker{NULL};
    tunoutworker* tunOutWorker{NULL};
    videoencoder* videoEnCoder{nullptr};
    audiocapture*  audioCapture{nullptr};
    audioencoder*  audioEnCoder{nullptr};
public://ResourceContainer
    QThread* trd[8]{nullptr};
    QHash<filesender*,QThread*> fileSenderContanier;
    QHash<int, QString> peerNames;
    QHash<int, QStringList> peerChatHistory;
    QHash<int,QVector<dcworker*>>* ipRoute{nullptr};
    QHash<int, VideoChatWindow*> videoChatSessions;
    QHash<int, AudioChatWindow*> audioChatSessions;
    QHash<int, QMetaObject::Connection> audioEncoderConnections;
public://TunReSources
    WINTUN_ADAPTER_HANDLE adapter{NULL};
    WINTUN_SESSION_HANDLE session{NULL};
public://inboundBuffer
    QMutex* mutex;
    std::vector<rtc::binary> inboundBuffer;

public://Tool & UI
    QString getCurrentPeerName();
    QString formatFileSize(qint64 bytes);
    void addChatBubble(QListWidget* listWidget, const QString& text, bool isSelf, bool isBroadcast);
    void reloadChatHistory();
    void initialUI();
    void updateFileReceiveTable();
    void updateFileTransferTable();

public://Dialogs
    bool requestDialog(const QString&,const QString&,const QString&,const QString&);
    bool requestDialogRich(const QString& title,const QString& question,const QString& paramsText,
                           const QString& btnText1,const QString& btnText0);
    netConfig getCoordinateNetConfigFromUI();
    netConfig getPeerNetConfigFromUI();

public://Init & Shutdown
    void initialSignaling();
    void initialTun();void getTun();void startTun();
    void closeEvent(QCloseEvent*);
    void cleanUp(bool isShutDown);
    void releaseTunResource();

public://Call state
    void updateCallButtonState();
    void sendHangupMsg(int peerHostNum,int workerIndex,uint8_t type,const char* sendSlot)
    {
        if(!ipRoute||!ipRoute->contains(peerHostNum))
            return;
        QVector<dcworker*>& workers=(*ipRoute)[peerHostNum];
        if(workers.size()>workerIndex&&workers[workerIndex])
        {
            uint64_t dummyTime=0;
            QByteArray hangupMsg=createResponse(type,false,dummyTime);
            hangupMsg[2]=(char)20;
            QMetaObject::invokeMethod(workers[workerIndex],sendSlot,Qt::QueuedConnection,
                                      Q_ARG(const QByteArray&,hangupMsg));
        }
    }
    //通过dcManager更新指定dcWorker的isCalling状态
    void updateDcWorkerCallingState(bool isAudioSession,bool isCalling,int sessionHostNum)
    {
        QMetaObject::invokeMethod(dcManager,"setWorkerCallingState",Qt::QueuedConnection,
                                  Q_ARG(int,sessionHostNum),Q_ARG(int,2),Q_ARG(bool,isCalling));
        if(!isAudioSession)
            QMetaObject::invokeMethod(dcManager,"setWorkerCallingState",Qt::QueuedConnection,
                                      Q_ARG(int,sessionHostNum),Q_ARG(int,3),Q_ARG(bool,isCalling));
    }

public slots://Worker pulse
    void onWorkerPulse(int ibs, int obs, int pr, const QList<fileDownLoadState>& fileState);

public slots://Peers
    void onPeerAdded(int peerHostNum, const QString& peerHostName);
    void onPeerRemoved(int peerHostNum);
    void onPeerMsgReceived(int peerHostNum,const QString& msg);
    void onPeerTableClicked(QTableWidgetItem* item);
    void onPeerConnectionAmountChanged(int peerHostNum,int currentConnectAmount);

public slots://Messaging
    void goSendUnicastMsg();
    void goSendBroadcastMsg();

public slots://File transfer
    void onAttachFile();
    void onFileDownLoadFinish(const QString&,int);
    void onFileDownLoadState(const QList<fileDownLoadState>&);

public slots://Video
    void initialVideoEncoder();
    void initialAudioEncoder(int callingHostNum,int audioSampleRate,int audioChannelCount,const QAudioDevice& audioDev,int noiseGate=2);
    void initialVideoChatWindow(int peerHostNum,int asr=48000,int acc=1);
    void initialVideoChatRoute(int callingHostNum,int videoWidth,int videoHeight,int videoFps,
                               int audioSampleRate,int audioChannelCount,const QAudioDevice& audioDev,int noiseGate=2);
    void onDecodedFrame(const QImage&,int);
    void onAppealVideoCallRequest();
    void onEndVideoChat(int peerHostNum);
    void onRemoteVideoHangup(int peerHostNum);
    void cleanupVideoChatSession(int peerHostNum);
    void shutAllVideoSession();
    void shutVideoSession(int peerHostNum);
    bool checkVeNecessity()
    {return !videoChatSessions.isEmpty();}

public slots://Audio
    void onAppealAudioCallRequest();
    void initialAudioChatWindow(int peerHostNum,int asr,int acc);
    void initialAudioChatRoute(int callingHostNum,int audioSampleRate,int audioChannelCount,const QAudioDevice& audioDev,int noiseGate=2);
    void onEndAudioChat(int peerHostNum);
    void onRemoteAudioHangup(int peerHostNum);
    void cleanupAudioChatSession(int peerHostNum);
    void onDecodedAudioFlood(const QByteArray& pcmData,int peerHostNum);
    void onAudioChatMuteToggled(int callingPeerHostNum,bool muted);
    void onTopMuteToggled(bool muted);
    void syncTopMuteStateToAllWindows(bool muted);
    void onRemoteAudioLevel(int level,int peerHostNum);
    void shutAudioSession(int peerHostNum);
    void shutAllAudioSession();
    bool checkAeNecessity()
    {return !audioChatSessions.isEmpty()&&!videoChatSessions.isEmpty();}
    //两套音频链路
    //(1)向外推流/audioCaptrure=>audioEncoder=>dcworker=>推流到对端
    //(2)向内拉流/dcworker
                    //audioChatSessions.audioChatWindow<=内嵌audioPlayer
                    //videoChatSessions.videoChatWindow<=内嵌audioPlayer
    //其中(2)不受QWinEventNotifier影响 QWinEventNotifier是audioCapture/QAudioSource的内层对象
    //仅不存在有效session(audioChatSessions+audioChatSessions)时可释放(1)
    //shutVideoSession和shutAllAudioSession均需要checkAeNeccessity
        //这里备注一下/当已经存在session时再次发起新session时音视频协商(参数变更)均无意义

public://Video helpers
    void cleanupVideoSessionPipeline();

public://Audio helpers
    void cleanupAudioSessionPipeline();
    void togglePeerAudioSend(int peerHostNum, bool muted);

public slots://Settings
    void onSettingsClicked();

signals:

public:
    //sending file track
    class FileTransferInfo
        {
        public:
            QString fileName;
            uint32_t fileSize;
            QDateTime createTime;
            bool chunkFinished=false;
            bool terminateException=false;
            FileTransferInfo(const QString& name,uint32_t size,const QDateTime& time,bool state):fileName(name),fileSize(size),createTime(time),chunkFinished(state){}
        };
    QHash<int, QVector<FileTransferInfo>> fileTransferHash;
    //receiving file track
    class FileReceiveInfo
        {
        public:
            QString fileName;
            qreal progress;
            FileReceiveInfo(const QString& name, qreal p): fileName(name), progress(p) {}
        };
    QHash<int, QVector<FileReceiveInfo>> fileReceiveHash;

public:
    QHash<uint64_t,request> mission;
    uint64_t getRunningTime(){
        return QDateTime::currentMSecsSinceEpoch()-startTime;}
public slots:
    //响应端收到dcworker转发的请求
    void onTransferRequest(uint8_t msgType,uint64_t requestTime,const QJsonObject& callParams,void* voidDCWorker);
    //发起端收到dcworker转回的回复
    void onReturnResult(uint64_t timePoint,bool accept);
public:
    QByteArray createRequest(uint8_t type,uint64_t requestTime,const QJsonObject& json){
        QByteArray request;
        //header固有标识字段(TYPE_NEGOTIATE)
        request.append((char)TYPE_NEGOTIATE);
        request.append((const char*)&type,1);
        request.append((char)(10));
        request.append((const char*)(&requestTime),8);
        request.append(QJsonDocument(json).toJson(QJsonDocument::Compact));
        return request;}
    QByteArray createResponse(uint8_t type,bool accept,uint64_t requestTime){
        QByteArray response;
        response.append((char)TYPE_NEGOTIATE);
        response.append((const char*)&type,1);
        response.append((char)(accept));
        response.append((const char*)(&requestTime),8);
        return response;}

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
