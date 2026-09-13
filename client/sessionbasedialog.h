#ifndef SESSIONBASEDIALOG_H
#define SESSIONBASEDIALOG_H

#include "topbasedialog.h"
#include <QSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QAudioDevice>
#include <QMessageBox>
#include <QMediaDevices>

class SessionBaseDialog : public TopBaseDialog
{
    Q_OBJECT
public:
    SessionBaseDialog(const QString& title, const QString& question,
                      const QString& paramsText, const QString& btn1Text,
                      const QString& btn0Text, bool showVideo,
                      int vw, int vh, int vf,
                      int asr, int acc, int noiseGate,
                      QWidget* parent = nullptr)
        : TopBaseDialog(title, question, paramsText, btn1Text, btn0Text, parent)
    {
        if(showVideo)
        {
            QGroupBox* videoGrp = new QGroupBox("本端视频参数(可修改)", this);
            QFormLayout* videoForm = new QFormLayout(videoGrp);
            sbWidth = new QSpinBox(this);
            sbWidth->setRange(160, 1920); sbWidth->setSingleStep(160);
            sbWidth->setValue(vw>0?vw:640);
            sbHeight = new QSpinBox(this);
            sbHeight->setRange(120, 1080); sbHeight->setSingleStep(120);
            sbHeight->setValue(vh>0?vh:480);
            sbFps = new QSpinBox(this);
            sbFps->setRange(5, 60); sbFps->setValue(vf>0?vf:15);
            videoForm->addRow("宽度:", sbWidth);
            videoForm->addRow("高度:", sbHeight);
            videoForm->addRow("帧率:", sbFps);
            mainLayout->addWidget(videoGrp);
        }

        cbDev = new QComboBox(this);
        inputDevices = QMediaDevices::audioInputs();
        for(int i = 0; i < inputDevices.size(); ++i)
            cbDev->addItem(inputDevices[i].description(), i);
        if(cbDev->count() == 0)
        {
            QMessageBox::warning(this, "提示", "未检测到可用的音频输入设备");
            reject();
            return;
        }
        cbDev->setCurrentIndex(0);

        cbSr = new QComboBox(this);
        cbSr->addItem("8000",  8000);
        cbSr->addItem("16000", 16000);
        cbSr->addItem("24000", 24000);
        cbSr->addItem("48000", 48000);
        cbSr->setCurrentIndex(cbSr->findData(asr>0?asr:48000));

        cbCh = new QComboBox(this);
        cbCh->addItem("单声道 (1)", 1);
        cbCh->addItem("立体声 (2)", 2);
        cbCh->setCurrentIndex(cbCh->findData(acc>0?acc:1));

        QGroupBox* audioGrp = new QGroupBox("本端音频参数(可修改)", this);
        QFormLayout* audioForm = new QFormLayout(audioGrp);
        audioForm->addRow("输入设备:", cbDev);
        audioForm->addRow("采样率:", cbSr);
        audioForm->addRow("通道数:", cbCh);

        cbNoise = new QComboBox(this);
        cbNoise->addItem("关闭 (0)", 0);
        cbNoise->addItem("低 (1)", 1);
        cbNoise->addItem("默认 (2)", 2);
        cbNoise->addItem("高 (5)", 5);
        cbNoise->addItem("很高 (10)", 10);
        cbNoise->setCurrentIndex(cbNoise->findData(noiseGate));
        audioForm->addRow("噪声门:", cbNoise);
        mainLayout->addWidget(audioGrp);

        mainLayout->insertWidget(mainLayout->count()-1, btnBox);
    }

    int videoWidth() const { return sbWidth ? sbWidth->value() : 0; }
    int videoHeight() const { return sbHeight ? sbHeight->value() : 0; }
    int videoFps() const { return sbFps ? sbFps->value() : 0; }
    int audioSampleRate() const { return cbSr->currentData().toInt(); }
    int audioChannelCount() const { return cbCh->currentData().toInt(); }
    int noiseGate() const { return cbNoise->currentData().toInt(); }
    QAudioDevice audioDevice() const
    {
        int idx = cbDev->currentData().toInt();
        if(idx >= 0 && idx < inputDevices.size())
            return inputDevices[idx];
        return QAudioDevice();
    }

protected:
    QSpinBox* sbWidth{nullptr};
    QSpinBox* sbHeight{nullptr};
    QSpinBox* sbFps{nullptr};
    QComboBox* cbDev;
    QComboBox* cbSr;
    QComboBox* cbCh;
    QComboBox* cbNoise;
    QList<QAudioDevice> inputDevices;
};

#endif
