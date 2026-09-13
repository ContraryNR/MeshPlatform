#ifndef LOGGER_H
#define LOGGER_H

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QString>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

class Logger
{
public:
    static Logger& instance()
    {
        static Logger instance;
        return instance;
    }

    void init(const QString& fileName)
    {
        QMutexLocker locker(&mutex);
        logFile.setFileName(fileName);
        if(logFile.exists())
            logFile.remove();//open自动重新创建
        if (logFile.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text))
            stream.setDevice(&logFile);
    }

    void log(const QString& msg)
    {
        QMutexLocker locker(&mutex);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString formattedMsg = QString("[%1] %2").arg(timestamp, msg);
        
        qDebug().noquote() << formattedMsg;
        
        if (logFile.isOpen())
        {
            stream << formattedMsg << "\n";
            stream.flush();
            logFile.flush();
        }
    }

    void log(const char* msg)
    {
        log(QString::fromUtf8(msg));
    }

    bool isInitialized() const { return logFile.isOpen(); }

private:
    Logger() = default;
    ~Logger()
    {
        if (logFile.isOpen())
            logFile.close();
    }
    
    QFile logFile;
    QTextStream stream;
    QMutex mutex;
};

class FileDebug
{
public:
    FileDebug() : stream(&buffer, QIODevice::WriteOnly) {}
    
    template<typename T>
    FileDebug& operator<<(const T& value)
    {
        stream << value;
        return *this;
    }
    
    FileDebug& operator<<(const QJsonObject& obj)
    {
        stream << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        return *this;
    }
    
    FileDebug& operator<<(const QJsonArray& arr)
    {
        stream << QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        return *this;
    }
    
    ~FileDebug()
    {
        stream.flush();
        // qDebug()<<buffer;
        Logger::instance().log(buffer);
    }
    
private:
    QString buffer;
    QTextStream stream;
};

#define qLog() FileDebug()

#endif // LOGGER_H

//qLog()临时构造栈区FileDebug对象=>利用"<<"运算符重载写入stream=>
//fileDebug临时对象析构时将stream内容写入buffer=>传给Logger全局实例(mainwindow构造时init()指定目标文件)
//=>Logger将参数QString写入文件
