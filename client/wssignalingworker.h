#ifndef WSSIGNALINGWORKER_H
#define WSSIGNALINGWORKER_H

#include <QObject>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

//信令传输层 —— 接替原 peernetworker(QTcpSocket)的位置,消息格式与下游 jsonworker 链路完全不变
//对照关系:
//  startTcpClient(ip,port) => startWsClient(ip,port)   内部拼接 ws://ip:port/ws
//  readyRead+手动'\n'切帧  => textMessageReceived       WebSocket帧自带边界,一帧即一条完整JSON
//  write(json+'\n')        => sendTextMessage(json)    帧边界由WebSocket协议承载,上游不再附加'\n'
class wssignalingworker : public QObject
{
    Q_OBJECT
public:
    QWebSocket* wsSocket{NULL};//在worker线程内创建(见startWsClient),避免跨线程parent

public slots:
    void startWsClient(const QString& ip,int port)
    {
        if(wsSocket)
        {
            if(wsSocket->state()==QAbstractSocket::ConnectedState)
                wsSocket->close();
            wsSocket->deleteLater();
            wsSocket=NULL;
        }
        wsSocket=new QWebSocket;
        connect(wsSocket,&QWebSocket::connected,[this](){
            emit readySendHostName();
        });
        connect(wsSocket,&QWebSocket::textMessageReceived,[this](const QString& frame){
            //无需粘包缓冲:一帧一条完整JSON(Java端SignalingWebSocketHandler按target原样转发)
            QJsonDocument doc=QJsonDocument::fromJson(frame.toUtf8());
            if(doc.isObject())
                emit onJsonMsg(doc.object());
            else
                emit wsError(QString::fromUtf8("收到无法解析的信令帧"));
        });
        connect(wsSocket,&QWebSocket::disconnected,[this](){
            wsSocket->deleteLater();
            wsSocket=NULL;
            emit wsError(QString::fromUtf8("与信令服务器的连接已断开"));
        });
        connect(wsSocket,&QWebSocket::errorOccurred,[this](QAbstractSocket::SocketError){
            if(wsSocket)
                emit wsError(QString::fromUtf8("WebSocket错误: ")+wsSocket->errorString());
        });
        wsSocket->open(QUrl(QString("ws://%1:%2/ws").arg(ip).arg(port)));
    }
    void pauseWsClient()
    {
        if(wsSocket)
            wsSocket->close();//close后由disconnected回调完成deleteLater
    }
    void sendMsg(const QByteArray& msg)
    {
        if(wsSocket&&wsSocket->isValid())
            wsSocket->sendTextMessage(QString::fromUtf8(msg));
    }

signals:
    void onJsonMsg(const QJsonObject&);
    void readySendHostName();
    void wsError(const QString& errorMsg);
};

#endif // WSSIGNALINGWORKER_H