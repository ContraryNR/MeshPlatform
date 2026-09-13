#ifndef FILESENDER_H
#define FILESENDER_H
#include <QObject>
#include <QFile>
#include <QFileInfo>
#include "dcworker.h"
#include "request.h"
#include <QPointer>

#define CHUNK_SIZE 23592

class filesender : public QObject
{
    Q_OBJECT
public:
    QString filepath;
    std::atomic<bool> running{false};
    filesender(const QString& file):filepath(file){}
public:
    QByteArray chunkPacker(uint64_t chunkIndex,uint64_t chunkAmount,uint8_t fileNameLength,const QByteArray& fileName,const QByteArray& data)
    {
        
        QByteArray msg;
        msg.append(static_cast<char>(TYPE_FILE));
        msg.append(reinterpret_cast<const char*>(&chunkIndex), sizeof(chunkIndex));
        msg.append(reinterpret_cast<const char*>(&chunkAmount), sizeof(chunkAmount));
        msg.append(static_cast<char>(fileNameLength));
        msg.append(fileName.left(fileNameLength));
        msg.append(data);
        return msg;
    }
public slots:
    void sendFile(void* voidDcworker)
    {
        QPointer<dcworker> worker((dcworker*)voidDcworker);
        QFile file(filepath);
        if(!file.open(QFile::ReadOnly))
            return;
        uint64_t chunkIndex = 0;
        uint64_t fileSize=file.size();
        uint64_t mainChunkAmount=fileSize/CHUNK_SIZE;
        uint64_t chunkAmount=mainChunkAmount+((fileSize-mainChunkAmount*CHUNK_SIZE)>0?1:0);
        QString fileName = QFileInfo(filepath).fileName();
        QByteArray fileNameByteArr = fileName.toUtf8();
        uint8_t fileNameBytes=fileNameByteArr.size();

        qDebug() << "开始发送文件:" << fileName << "文件大小:" << fileSize 
                 << "块大小:" << CHUNK_SIZE << "总块数:" << chunkAmount;
        
        while(!file.atEnd()&&running)
        {
            if(worker)
            {
                QByteArray chunk=file.read(CHUNK_SIZE);
                QMetaObject::invokeMethod(worker,"sendFileMsg",Qt::QueuedConnection,
                                          Q_ARG(const QByteArray&,chunkPacker(chunkIndex,chunkAmount,fileNameBytes,fileNameByteArr,chunk)),
                                          Q_ARG(bool,chunkIndex!=chunkAmount-1));
                                        //chunkIndex==chunkAmount-1 => 当前块为最后的块 => 发送完本chunk后暂无下个块待发 => false
                                        //相反 != 则是只要不是最后一个chunk则预测'有'(true)下个event
            }
            else
            {
                emit fileSendStop(false);
                return;//针对文件发送一半dcWorker销毁的情况
            }
            chunkIndex++;
        }
        emit fileSendStop(true);
    }
signals:
    void fileSendStop(bool isFinish);
signals:
};

#endif // FILESENDER_H
