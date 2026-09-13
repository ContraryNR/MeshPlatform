#ifndef REQUEST_H
#define REQUEST_H
#include <QObject>
#include <functional>

class request
{
public:
    QObject *receiver;
    std::function<void()> mission;
    request():receiver(nullptr),mission(nullptr){}
    request(QObject *Receiver,std::function<void()> Mission) : receiver(Receiver),mission(Mission) {}
    void execute()
    {QMetaObject::invokeMethod(receiver,mission,Qt::QueuedConnection);}
};

#endif // REQUEST_H
