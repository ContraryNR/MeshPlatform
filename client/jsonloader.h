#ifndef JSONLOADER_H
#define JSONLOADER_H

#include <QObject>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QDebug>

class jsonloader : public QObject
{
    Q_OBJECT
public:
    QFileSystemWatcher* watcher;
    QString watchDir;

    jsonloader(QObject* parent=nullptr):QObject(parent)
    {
        watchDir=QDir::currentPath()+"/loadingJson";
        QDir().mkpath(watchDir);
        watcher=new QFileSystemWatcher(this);
        watcher->addPath(watchDir);
        connect(watcher,&QFileSystemWatcher::directoryChanged,this,&jsonloader::onDirChanged);
    }

public slots:
    void onDirChanged(const QString& path)
    {
        QDir dir(path);
        QStringList jsonFiles=dir.entryList(QStringList("*.json"),QDir::Files,QDir::Name);
        for(const QString& fileName : jsonFiles)
        {
            QString filePath=dir.filePath(fileName);
            loadJsonFile(filePath);
            QFile::remove(filePath);
        }
    }

    void loadJsonFile(const QString& filePath)
    {
        QFile file(filePath);
        if(!file.open(QIODevice::ReadOnly))
        {
            emit loadFailed(filePath, "无法打开文件");
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if(error.error != QJsonParseError::NoError)
        {
            emit loadFailed(filePath, error.errorString());
            return;
        }

        if(doc.isObject())
        {
            emit jsonObjLoaded(doc.object());
            emit loadSuccess(filePath);
        }
        else
            emit loadFailed(filePath, "json不是对象格式");
    }

signals:
    //updateUi
    void loadSuccess(const QString& filePath);
    void loadFailed(const QString& filePath, const QString& errorMsg);
    void jsonObjLoaded(const QJsonObject& jsonObj);

};

#endif // JSONLOADER_H
