#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H
#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QIODevice>
#include <QAudioSource>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudio>
#include <QMediaDevices>
#include <QThread>
#include <cmath>
#define noiseGateHold 8//噪声门打开后保持发送的帧数(20ms/帧,8帧≈160ms),防止尾音截断
//audioSource(源/生产) => audioencoder(中继管线/消费) =待编码的PCM帧(若干采样点)=> audioEncoder

class audiocapture : public QObject
{
    Q_OBJECT
public:
    //PCMConfig
    int sampleRate{0},channelCount{0},frameDurationMs{0},frameSizeBytes{0};
    //RunningConfig
    bool muted{false};//本端静音停止读取 PCM(本端不再发声)
    int noiseThreshold{2};//噪声门阈值(0~100):低于此值不发送,0=关闭噪声门
    int noiseGateHoldCounter{0};//当前剩余保持帧数(向下跌落到噪声门后继续发声时长)
    //Pump(Flood)Timer
    QTimer* floodTimer{nullptr};
    //构造时协商device和format
    QAudioDevice audioDevice;QAudioFormat audioFormat;
    //启动时初始化音频生产(audioSource)消费(captureDevice)链条
    QAudioSource* audioSource{nullptr};QIODevice* audioPipeline{nullptr};
    audiocapture(int sr=48000,int channelCount=1,int frameMs=20,const QAudioDevice& device=QAudioDevice(),int noiseGate=2)
        : sampleRate(sr), channelCount(channelCount), frameDurationMs(frameMs), noiseThreshold(noiseGate)
    {
        audioDevice = device;
        audioFormat.setSampleRate(sampleRate);
        audioFormat.setChannelCount(channelCount);
        audioFormat.setSampleFormat(QAudioFormat::Int16);
        if(!negotiateAudio(audioDevice, audioFormat))
            return;
        sampleRate = audioFormat.sampleRate();
        channelCount = audioFormat.channelCount();
        frameSizeBytes = (sampleRate / 1000) * frameDurationMs
                         * channelCount * static_cast<int>(sizeof(int16_t));
    }
public:
    static bool negotiateAudio(QAudioDevice& device, QAudioFormat& format)
    {
        if(device.isNull())
            device = QMediaDevices::defaultAudioInput();
        if(device.isNull())
            return false;
        if(!device.isFormatSupported(format))
            format = device.preferredFormat();
        return true;
    }
    bool sourceWorking()
    {
        if(!audioSource)return false;
        return (audioSource->state() == QAudio::ActiveState )||(audioSource->state() == QAudio::IdleState);
    }
public slots:
    void startFloodTimer()
    {
        if(!audioSource)
        {
            if(audioDevice.isNull())return;
            audioSource = new QAudioSource(audioDevice, audioFormat, this);
            audioSource->setBufferSize(frameSizeBytes * 4);
        }
        if(!sourceWorking())
        {
            audioPipeline = audioSource->start();
            if(!audioPipeline)
                return;
            if(!sourceWorking())
                return;
        }
        if(!floodTimer)
        {
            floodTimer = new QTimer(this);
            connect(floodTimer, &QTimer::timeout, this, [this](){
                if(audioPipeline&&(!(audioPipeline->bytesAvailable()<frameSizeBytes)))
                {
                    QByteArray pcmData = audioPipeline->read(frameSizeBytes);
                    if(pcmData.size() == frameSizeBytes)
                    {
                        const int16_t* samples = reinterpret_cast<const int16_t*>(pcmData.constData());
                        int sampleCount = frameSizeBytes / sizeof(int16_t);
                        if(sampleCount > 0)
                        {
                            double sumSq = 0.0;
                            for(int i = 0; i < sampleCount; ++i)
                            {
                                double v = static_cast<double>(samples[i]);
                                sumSq += v * v;
                            }
                            double rms = std::sqrt(sumSq / sampleCount) / 32768.0;
                            int level = static_cast<int>(rms * 100.0);
                            if(level > 100) level = 100;
                            emit sendPcmLevel(level);
                            if(noiseThreshold > 0 && level < noiseThreshold && noiseGateHoldCounter <= 0)
                                return;
                            if(level >= noiseThreshold)
                                noiseGateHoldCounter = noiseGateHold;
                            if(noiseGateHoldCounter > 0) noiseGateHoldCounter--;
                        }
                        emit sendPcmFrame(pcmData);
                    }
                }
            });
        }
        if(floodTimer && !(floodTimer->isActive()))
            floodTimer->start(frameDurationMs);
    }
    void setMuted(bool mute)
    {
        if(muted == mute)
            return;
        muted = mute;
        if(!floodTimer)return;
        if(muted&&floodTimer->isActive())
            floodTimer->stop();
        else if(!muted&&!floodTimer->isActive())
            floodTimer->start(frameDurationMs);
    }
    void shutdown()
    {
        if(floodTimer)
        {
            floodTimer->stop();
            floodTimer->deleteLater();
            floodTimer = nullptr;
        }
        if(audioSource)
        {
            audioSource->stop();
            audioSource->deleteLater();
            audioSource = nullptr;
        }
        if(audioPipeline)
        {
            audioPipeline->deleteLater();
            audioPipeline = nullptr;
        }
    }

signals:
    void sendPcmFrame(const QByteArray& pcmData);
    void sendPcmLevel(int level);
};

#endif // AUDIOCAPTURE_H
