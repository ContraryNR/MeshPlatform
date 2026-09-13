#ifndef AUDIODECODER_H
#define AUDIODECODER_H
#include <QObject>
#include <QByteArray>
#include <opus/opus.h>
#include <cmath>
#include "logger.h"

//dcworker =Opus编码帧=> audioDecoder => opusDecoder =PCM帧=> dcworker>dcmanager>MainWindow => AudioChatWindow.playFlood(QAudioSink)

class audiodecoder : public QObject
{
    Q_OBJECT
public:
    int sampleRate;
    int channelCount;
    OpusDecoder* decoderHandle{nullptr};
    int frameSizeSamples;
    // int jitterBufferDepth{3};//预留字段/目前不实现真正的 jitter buffer

    audiodecoder(int sr=48000,int ch=1)
        : sampleRate(sr), channelCount(ch)
    {
        frameSizeSamples = sampleRate / 1000 * 20;

        int error = OPUS_OK;
        decoderHandle = opus_decoder_create(sampleRate, channelCount, &error);
        if(error != OPUS_OK || !decoderHandle)
        {
            Logger::instance().log(QString("[audiodecoder] opus_decoder_create failed err=%1").arg(error));
            return;
        }
        Logger::instance().log(QString("[audiodecoder] init ok sr=%1 ch=%2 frameSamples=%3")
                               .arg(sampleRate).arg(channelCount).arg(frameSizeSamples));
    }

public slots:
    void decodedFlood(void* data, int bytes)
    {
        if(!data || bytes <= 0)
        {
            if(data) free(data);
            return;
        }
        if(!decoderHandle)
        {
            free(data);
            return;
        }

        QByteArray pcmBuffer;
        pcmBuffer.resize(frameSizeSamples * channelCount * static_cast<int>(sizeof(int16_t)));
        opus_int16* pcmOut = reinterpret_cast<opus_int16*>(pcmBuffer.data());

        int decodedSamples = opus_decode(decoderHandle,
                                         reinterpret_cast<const unsigned char*>(data),
                                         bytes,
                                         pcmOut,
                                         frameSizeSamples,
                                         0);

        free(data);

        if(decodedSamples < 0)
        {
            Logger::instance().log(QString("[audiodecoder] opus_decode failed: %1")
                                   .arg(QString::fromUtf8(opus_strerror(decodedSamples))));
            return;
        }
        if(decodedSamples != frameSizeSamples)
            pcmBuffer.resize(decodedSamples * channelCount * static_cast<int>(sizeof(int16_t)));

        {
            const int16_t* samples = reinterpret_cast<const int16_t*>(pcmBuffer.constData());
            int sampleCount = pcmBuffer.size() / static_cast<int>(sizeof(int16_t));
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
                emit sendDecodedAudioLevel(level);
            }
        }

        emit sendDecodedAudio(pcmBuffer);
    }

    void shutdown()
    {
        if(decoderHandle)
        {
            opus_decoder_destroy(decoderHandle);
            decoderHandle = nullptr;
        }
    }

signals:
    void sendDecodedAudio(const QByteArray& pcmData);
    void sendDecodedAudioLevel(int level);
};

#endif // AUDIODECODER_H
