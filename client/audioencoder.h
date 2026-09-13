#ifndef AUDIOENCODER_H
#define AUDIOENCODER_H

#include <QObject>
#include <QByteArray>
#include <QThread>
#include "opus/opus.h"
#include "logger.h"

#ifndef TYPE_AUDIO
#define TYPE_AUDIO 3
#endif

class audioencoder : public QObject
{
    Q_OBJECT
public:
    int sampleRate;
    int channelCount;
    int bitrate;
    int frameSizeSamples;
    OpusEncoder* encoderHandle{nullptr};

    audioencoder(int sr=48000,int ch=1,int br=32000)
        : sampleRate(sr), channelCount(ch), bitrate(br)
    {
        frameSizeSamples = sampleRate / 1000 * 20;//20ms 一帧

        int error = OPUS_OK;
        encoderHandle = opus_encoder_create(sampleRate,
                                            channelCount,
                                            OPUS_APPLICATION_VOIP,
                                            &error);
        if(error != OPUS_OK || !encoderHandle)
        {
            Logger::instance().log(QString("[audioencoder] opus_encoder_create failed err=%1").arg(error));
            return;
        }
        //32kbps 是 VoIP 场景的甜点码率(语音清晰 + 带宽友好)
        opus_encoder_ctl(encoderHandle, OPUS_SET_BITRATE(bitrate));
        //复杂度 5 平衡 CPU 与质量(0~10),常规 PC 完全够用
        opus_encoder_ctl(encoderHandle, OPUS_SET_COMPLEXITY(5));
        Logger::instance().log(QString("[audioencoder] init ok sr=%1 ch=%2 br=%3 frameSamples=%4")
                               .arg(sampleRate).arg(channelCount).arg(bitrate).arg(frameSizeSamples));
    }

public slots:
    void encodeFlood(const QByteArray& pcmData)
    {
        if(!encoderHandle)
            return;

        const int expectedBytes = frameSizeSamples * channelCount * static_cast<int>(sizeof(int16_t));
        if(pcmData.size() < expectedBytes)
            return;

        unsigned char encodedBuf[4096];
        int encodedBytes = opus_encode(encoderHandle,
                                       reinterpret_cast<const opus_int16*>(pcmData.constData()),
                                       frameSizeSamples,
                                       encodedBuf,
                                       sizeof(encodedBuf));
        if(encodedBytes < 0)
        {
            Logger::instance().log(QString("[audioencoder] opus_encode failed: %1")
                                   .arg(QString::fromUtf8(opus_strerror(encodedBytes))));
            return;
        }

        QByteArray packet;
        packet.reserve(1 + 3 + encodedBytes);
        packet.append(static_cast<char>(TYPE_AUDIO));
        uint16_t srCode = static_cast<uint16_t>(sampleRate / 1000);
        packet.append(reinterpret_cast<const char*>(&srCode), 2);
        packet.append(static_cast<char>(channelCount));
        packet.append(reinterpret_cast<const char*>(encodedBuf), encodedBytes);
        emit sendEncodedAudio(packet);
    }

    void shutdown()
    {
        if(encoderHandle)
        {
            opus_encoder_destroy(encoderHandle);
            encoderHandle = nullptr;
        }
    }

signals:
    void sendEncodedAudio(const QByteArray& encodedPacket);
};

#endif // AUDIOENCODER_H
