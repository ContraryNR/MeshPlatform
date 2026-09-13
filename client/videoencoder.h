#ifndef VIDEOENCODER_H
#define VIDEOENCODER_H

#include <QObject>
#include <QDebug>
#include <QDateTime>
#include <QDataStream>
#include <QIODevice>
#include <QTimer>
#include <QImage>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include "logger.h"
#include "dcworker.h"

class videoencoder : public QObject
{
    Q_OBJECT
public:
    int fps;
    uint64_t startTime;
    QTimer* floodTimer{nullptr};
    cv::VideoCapture* cam;//id传0打开系统默认摄像头
    videoencoder(int cameraId=0,int frameWidth=640,int frameHeight=480,int FPS=15):cam(new cv::VideoCapture(cameraId)),fps(FPS)
    {
        if(cam->isOpened())
        {
            cam->set(cv::CAP_PROP_FRAME_WIDTH, frameWidth);
            cam->set(cv::CAP_PROP_FRAME_HEIGHT, frameHeight);
            cam->set(cv::CAP_PROP_FPS, fps);
        }
    }
    void encodedFlood()
    {
        cv::Mat frame;
        cam->read(frame);
        if(frame.empty())
            return;
        std::vector<uchar> buffer;
        //预期文件扩展名  未编码图像对象 输出缓冲区 质量(关键key+参数value)
        cv::imencode(".jpg", frame, buffer, {cv::IMWRITE_JPEG_QUALITY, 50});//编码并存储到buffer中
        QByteArray encodedResult;
        QDataStream packetStream(&encodedResult, QIODevice::WriteOnly);
        packetStream<<(uint8_t)(TYPE_VIDEO);
        encodedResult.append((const char*)(buffer.data()),buffer.size());
        QImage localImg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
        emit sendEncodedFrame(localImg.copy(),encodedResult);
    }
public slots:
    void startFloodTimer()
    {
        floodTimer=new QTimer(this);
        floodTimer->setInterval((int)(1000/fps));
        connect(floodTimer,&QTimer::timeout,this,&videoencoder::encodedFlood);
        floodTimer->start();
    }
    void shutdown()
    {
        if(floodTimer)
        {
            if(floodTimer->isActive())
                floodTimer->stop();
            floodTimer->deleteLater();
            floodTimer = nullptr;
        }
        if(cam)
        {
            if(cam->isOpened())
                cam->release();
            delete cam;
            cam = nullptr;
        }
    }
signals:
    void sendEncodedFrame(const QImage&,const QByteArray&);
};

#endif // VIDEOENCODER_H
