#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QObject>
#include <QByteArray>
#include <QImage>
#include <QDebug>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

class videodecoder : public QObject
{
    Q_OBJECT
public slots:
    void decodedFlood(void* data,int bytes)
    {
        cv::Mat frame = cv::imdecode(cv::Mat(1,bytes,CV_8UC1,data),cv::IMREAD_COLOR);
        free(data);
        if(frame.empty())
            return;
        emit sendDecodedFrame(QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888).copy());
    }
signals:
    void sendDecodedFrame(const QImage&);
};

#endif // VIDEODECODER_H

