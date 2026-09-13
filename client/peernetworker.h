#ifndef PEERNETWORKER_H
#define PEERNETWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

class peernetworker : public QObject
{
    Q_OBJECT
public:
    QByteArray msg;
    QTcpSocket* clientSocket{NULL};

public slots:
    void startTcpClient(const QString& ip,int port)
    {
        if(clientSocket&&clientSocket->isValid())
            clientSocket->disconnectFromHost();
        if(!clientSocket)
        {
            clientSocket=new QTcpSocket;
            connect(clientSocket,&QTcpSocket::connected,[ip,port,this](){
                connect(clientSocket,&QTcpSocket::readyRead,this,&peernetworker::onReadyRead);
                connect(clientSocket,&QTcpSocket::disconnected,[this](){
                    clientSocket->deleteLater();
                    clientSocket=0;
                });
                emit readySendHostName();
            });
        }
        clientSocket->connectToHost(ip,port);
    }
    void onReadyRead()
    {
        msg += clientSocket->readAll();
        while (msg.contains('\n'))
        {
            int index = msg.indexOf('\n');
            QByteArray oneMsg = msg.left(index);
            msg.remove(0, index + 1);
            QJsonDocument doc;
            if ((doc= QJsonDocument::fromJson(oneMsg)).isObject())
            {
                emit  onJsonMsg(doc.object());
            }
        }
    }
    void pauseTcpClient()
    {
        if(clientSocket&&clientSocket->isValid())
            clientSocket->disconnectFromHost();
    }
    void sendMsg(const QByteArray& msg)
    {
        if(clientSocket&&clientSocket->isValid())
            clientSocket->write(msg);
    }

signals:
    void onJsonMsg(const QJsonObject&);
    void readySendHostName();
};

#endif // PEERNETWORKER_H


