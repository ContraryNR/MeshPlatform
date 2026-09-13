#ifndef TUNINWORKER_H
#define TUNINWORKER_H

#include <QObject>
#include <QTimer>
#include <QMutex>
#include "rtc/rtc.hpp"
#include "tunloader.h"

class tuninworker : public QObject
{
    Q_OBJECT
public:
    tunloader* functionLoader{NULL};
    std::vector<rtc::binary>& inboundBuffer;
    QMutex* inboundBufferMutex{NULL};
    QTimer* readTimer{NULL};
    tuninworker(WINTUN_SESSION_HANDLE session,tunloader* loader,std::vector<rtc::binary>& inBuffer,QMutex* mtx):functionLoader(loader),inboundBuffer(inBuffer),inboundBufferMutex(mtx)
    {
        readTimer=new QTimer(this);
        readTimer->setInterval(100);
        connect(readTimer,&QTimer::timeout,this,[this,session](){
            QMutexLocker locker(inboundBufferMutex);
            if(!inboundBuffer.empty())
                for(std::vector<std::byte> data:inboundBuffer)
                {
                    BYTE* packet = functionLoader->AllocateSendPacket(session, static_cast<DWORD>(data.size()));
                    if (packet)
                    {
                        std::memcpy(packet, data.data(), data.size());
                        functionLoader->SendPacket(session, packet);
                    }
                    else
                        qDebug() << "Wintun Write Buffer Full!";
                }
            inboundBuffer.clear();
        });
    }
public slots:
    void startInternalSessionFlood()
    {
        if(readTimer)
            readTimer->start();
    }
    void pasueInternalSessionFlood()
    {
        readTimer->stop();
    }
    void cleanQOBJ()
    {
        readTimer->stop();
        delete(readTimer);
    }
};

#endif // TUNINWORKER_H
