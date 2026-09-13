#ifndef FILEDOWNLOADER_H
#define FILEDOWNLOADER_H

#include <QObject>
#include <QFile>
#include <QDir>
#include <QHash>
#include <QSet>
#define CHUNK_SIZE 23592

class filedownloader : public QObject
{
    Q_OBJECT
public:
    QFile* file;
    QString fileName;
    uint64_t writedChunkAmount=0,totalChunkAmount=0;
    QSet<uint64_t> writtenChunks;
    std::atomic<bool> running=false;
    filedownloader(const QString& filename,uint64_t chunkAmount):file(new QFile(QDir::currentPath() + "/" + filename)),fileName(filename)
    {
        totalChunkAmount=chunkAmount;
        bool ok=file->open(QIODevice::WriteOnly);
    }
public slots:
    void writeToChunkIndex(uint64_t chunkIndex,const QByteArray& chunk)
    {
        if(running)
        {
            if(file)
            {
                if(!(file->isOpen()))
                    if(!(file->open(QIODevice::WriteOnly)))
                        return;
            }
            else
                return;
        }
        else
        {
            if(file)
                if(file->isOpen())
                {
                    file->close();
                    delete(file);
                    file=nullptr;
                }
            return;
        }
        file->seek(CHUNK_SIZE*chunkIndex);
        qint64 written=file->write(chunk);
        if(writtenChunks.contains(chunkIndex))//使用set避免重复
            return;
        writtenChunks.insert(chunkIndex);
        writedChunkAmount++;
        if(chunkIndex%50==0||writedChunkAmount==totalChunkAmount)
        emit fileWriteProgress(fileName,qreal(writedChunkAmount)*100.0/(qreal(totalChunkAmount)));
        if(writedChunkAmount==totalChunkAmount)
        {
            file->close();
            emit fileWriteFinished(fileName);
            running=false;
        }
    }
signals:
    void fileWriteFinished(const QString& filename);
    void fileWriteProgress(const QString&,qreal);
};

#endif // FILEDOWNLOADER_H
