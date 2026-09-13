#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QJsonObject>
#include "passivesessionrequestdialog.h"
#include "filerequestdialog.h"

//1.解析Request
void MainWindow::onTransferRequest(uint8_t msgType,uint64_t requestTime,const QJsonObject& callParams,void* voidDCWorker)
{
    dcworker* worker = static_cast<dcworker*>(voidDCWorker);
    if(!worker)
        return;

    QString title;
    switch(msgType)
    {
    case TYPE_FILE:  title = "文件传输请求"; break;
    case TYPE_AUDIO: title = "语音通话请求"; break;
    case TYPE_VIDEO: title = "视频通话请求"; break;
    default: title = "请求"; break;
    }
    QString explain=callParams.value("explain").toString();
    int vw=0,vh=0,vf=0,asr=0,acc=0;
    vw=callParams.value("videoWidth").toInt();
    vh=callParams.value("videoHeight").toInt();
    vf=callParams.value("videoFps").toInt();
    asr=callParams.value("audioSampleRate").toInt();
    acc=callParams.value("audioChannelCount").toInt();
    QString paramsText;
    if(msgType == TYPE_VIDEO)
        paramsText = QString("视频:%1 x %2 @ %3 fps\n音频:%4 Hz / %5 ch")
                         .arg(vw>0?vw:0).arg(vh>0?vh:0).arg(vf>0?vf:0)
                         .arg(asr>0?asr:0).arg(acc>0?acc:0);
    else if(msgType == TYPE_AUDIO)
        paramsText = QString("音频:%1 Hz / %2 ch")
                         .arg(asr>0?asr:0).arg(acc>0?acc:0);
    else if(msgType == TYPE_FILE)
    {
        QString fileName=callParams.value("fileName").toString();
        qint64 fileSize=callParams.value("fileSize").toVariant().toLongLong();
        paramsText = QString("文件:%1\n大小:%2 字节")
                         .arg(fileName.isEmpty()?"(未知)":fileName)
                         .arg(fileSize);
    }
    bool accepted = false;
    QAudioDevice selectedDevice;
    int selectedNoiseGate = 2;
    if(msgType == TYPE_AUDIO)
    {
        PassiveSessionRequestDialog dlg(title, explain.isEmpty() ? title : explain, paramsText,
                                     false, 0, 0, 0, asr, acc, selectedNoiseGate, this);
        accepted = dlg.exec() == QDialog::Accepted;
        if(accepted)
        {
            asr=dlg.audioSampleRate(); acc=dlg.audioChannelCount();
            selectedNoiseGate=dlg.noiseGate(); selectedDevice=dlg.audioDevice();
        }
    }
    else if(msgType == TYPE_VIDEO)
    {
        PassiveSessionRequestDialog dlg(title, explain.isEmpty() ? title : explain, paramsText,
                                     true, vw, vh, vf, asr, acc, selectedNoiseGate, this);
        accepted = dlg.exec() == QDialog::Accepted;
        if(accepted)
        {
            vw=dlg.videoWidth(); vh=dlg.videoHeight(); vf=dlg.videoFps();
            asr=dlg.audioSampleRate(); acc=dlg.audioChannelCount();
            selectedNoiseGate=dlg.noiseGate(); selectedDevice=dlg.audioDevice();
        }
    }
    else
        accepted = requestDialogRich(title, explain.isEmpty() ? title : explain, paramsText, "同意", "拒绝");
    QByteArray response = createResponse(msgType, accepted, requestTime);

    switch(msgType)
    {
    case TYPE_FILE:
        QMetaObject::invokeMethod(worker, "sendFileMsg", Qt::QueuedConnection,
                                  Q_ARG(const QByteArray&, response), Q_ARG(bool, false));
        break;
    case TYPE_AUDIO:
        QMetaObject::invokeMethod(worker, "sendAudioMsg", Qt::QueuedConnection,
                                  Q_ARG(const QByteArray&, response));
        break;
    case TYPE_VIDEO:
        QMetaObject::invokeMethod(worker, "sendVideoMsg", Qt::QueuedConnection,
                                  Q_ARG(const QByteArray&, response));
        break;
    default:
        return;
    }

    QString peerName = peerNames.value(worker->peerHostNum, "未知");
    ui->stateMsg->appendPlainText(QString("已%1来自 %2 的请求").arg(accepted ? "同意" : "拒绝").arg(peerName));

    if(accepted)
    {
        int callingHostNum = worker->peerHostNum;
        if(msgType == TYPE_AUDIO)
            initialAudioChatRoute(callingHostNum, asr, acc, selectedDevice, selectedNoiseGate);
        else if(msgType == TYPE_VIDEO)
            initialVideoChatRoute(callingHostNum, vw, vh, vf, asr, acc, selectedDevice, selectedNoiseGate);
    }
}
//2.Request弹窗
bool MainWindow::requestDialogRich(const QString& title,const QString& question,const QString& paramsText,
                                   const QString& btnText1,const QString& btnText0)
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(title);
    QString richText = QString("<p style=\"font-weight:bold;font-size:11pt;\">%1</p>").arg(question.toHtmlEscaped());
    if(!paramsText.isEmpty())
        richText += QString("<pre style=\"font-family:Consolas,monospace;font-size:10pt;"
                            "background:#F5F5F5;padding:6px;\">%1</pre>").arg(paramsText.toHtmlEscaped());
    msgBox.setText(richText);
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setIcon(QMessageBox::Question);
    QPushButton* button1 = msgBox.addButton(btnText1, QMessageBox::ActionRole);
    QPushButton* button0 = msgBox.addButton(btnText0, QMessageBox::ActionRole);
    msgBox.setDefaultButton(button1);
    msgBox.exec();
    return (QPushButton*)(msgBox.clickedButton()) == button1 ? 1 : 0;
}
