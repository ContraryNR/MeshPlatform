#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QAudioSink>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaDevices>
#include <QByteArray>

// 独立的音频播放 mixin —— 不继承 QObject，避免菱形继承
// AudioChatWindow / VideoChatWindow 同时继承 QDialog + audioplayer
class audioplayer
{
public:
    virtual ~audioplayer() = default;

    void initAudio(int sr, int ch)
    {
        sampleRate = sr;
        channelCount = ch;

        QAudioFormat fmt;
        fmt.setSampleRate(sampleRate);
        fmt.setChannelCount(channelCount);
        fmt.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (dev.isNull())
            return;
        if (!dev.isFormatSupported(fmt))
        {
            fmt = dev.preferredFormat();
            sampleRate = fmt.sampleRate();
            channelCount = fmt.channelCount();
        }

        m_device = dev;
        m_format = fmt;

        audioSink = new QAudioSink(m_device, m_format);
        playbackDevice = audioSink->start();
    }

    void shutdownAudio()
    {
        if (audioSink)
        {
            if (audioSink->state() != QAudio::StoppedState)
                audioSink->stop();
            audioSink->deleteLater();
            audioSink = nullptr;
        }
        playbackDevice = nullptr;
    }

    void playFlood(const QByteArray& pcmData)
    {
        if (!playbackDevice || pcmData.isEmpty())
            return;
        if (audioSink && audioSink->state() == QAudio::StoppedState)
            return;
        playbackDevice->write(pcmData);
    }

protected:
    int sampleRate{48000};
    int channelCount{1};

private:
    QAudioSink*  audioSink{nullptr};
    QIODevice*   playbackDevice{nullptr};
    QAudioDevice m_device;
    QAudioFormat m_format;
};

#endif // AUDIOPLAYER_H
