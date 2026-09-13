#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QFileInfo>
#include <QProgressBar>
#include <QJsonObject>
#include "settingsdialog.h"

//(0)dcManager到Mainwindow的数据同步与MainWindow刷新
void MainWindow::onPeerAdded(int peerHostNum, const QString& peerHostName)
{
    peerNames.insert(peerHostNum, peerHostName);
    QString virtualIP = ui->subnetPrefix->text() + "." + QString::number(peerHostNum);
    int row = ui->peerTable->rowCount();
    ui->peerTable->insertRow(row);
    ui->peerTable->setItem(row, 0, new QTableWidgetItem(QString::number(peerHostNum)));
    ui->peerTable->setItem(row, 1, new QTableWidgetItem(peerHostName));
    ui->peerTable->setItem(row, 2, new QTableWidgetItem(virtualIP));
    ui->peerTable->setItem(row, 3, new QTableWidgetItem("在线"));
    ui->peerTable->setItem(row, 4, new QTableWidgetItem("1"));
    if(!peerChatHistory.contains(peerHostNum))
        peerChatHistory.insert(peerHostNum, QStringList());
    ui->btnBroadcast->setEnabled(true);
}
void MainWindow::onPeerRemoved(int peerHostNum)
{
    peerNames.remove(peerHostNum);
    peerChatHistory.remove(peerHostNum);
    for(int i = 0; i < ui->peerTable->rowCount(); i++)
    {
        QTableWidgetItem* item = ui->peerTable->item(i, 0);
        if(item && item->text().toInt() == peerHostNum) {
            ui->peerTable->removeRow(i);
            break;
        }
    }
    if(currentPeerHostNum == peerHostNum)
    {
        currentPeerHostNum = 0;
        ui->chatTitle->setText("未选择聊天对象");
        ui->chatListWidget->clear();
        ui->btnAttach->setEnabled(false);
        ui->btnSend->setEnabled(false);
        ui->btnVoiceChat->setEnabled(false);
        ui->btnVideoChat->setEnabled(false);
    }
    if(peerNames.size()==0)
        ui->btnBroadcast->setEnabled(false);
}
void MainWindow::updateCallButtonState()
{
    if(currentPeerHostNum == 0)
    {
        ui->btnVoiceChat->setEnabled(false);
        ui->btnVideoChat->setEnabled(false);
        return;
    }
    bool noExistCallingSession=audioChatSessions.isEmpty()&&videoChatSessions.isEmpty();
    ui->btnVoiceChat->setEnabled(noExistCallingSession&&isWorkerReady(currentPeerHostNum, TYPE_AUDIO, ipRoute));
    ui->btnVideoChat->setEnabled(noExistCallingSession&&isWorkerReady(currentPeerHostNum, TYPE_VIDEO, ipRoute));
    ui->btnVideoChat->setText(!noExistCallingSession ? "视频通话中..." : "开始视频通话");
}
void MainWindow::onPeerConnectionAmountChanged(int peerHostNum,int currentConnectAmount)
{
    for(int i = 0; i < ui->peerTable->rowCount(); i++)
    {
        QTableWidgetItem* item = ui->peerTable->item(i, 0);
        if(item && item->text().toInt() == peerHostNum)
        {
            QTableWidgetItem* connItem = ui->peerTable->item(i, 4);
            if(connItem)
                connItem->setText(QString::number(currentConnectAmount));
            break;
        }
    }
    if(currentPeerHostNum == peerHostNum)
        updateCallButtonState();
}
void MainWindow::onWorkerPulse(int ibs, int obs, int pr, const QList<fileDownLoadState> &fileState)
{
    if(ibs / 1024 <= 1)
        ui->internalSpeed->setText(QString("入站: %1 B/s").arg(ibs));
    else if((ibs /= 1024) / 1024 <= 1)
        ui->internalSpeed->setText(QString("入站: %1 KB/s").arg(ibs));
    else
        ui->internalSpeed->setText(QString("入站: %1 MB/s").arg(ibs /= 1024));

    if(obs / 1024 <= 1)
        ui->externalSpeed->setText(QString("出站: %1 B/s").arg(obs));
    else if((obs /= 1024) / 1024 <= 1)
        ui->externalSpeed->setText(QString("出站: %1 KB/s").arg(obs));
    else
        ui->externalSpeed->setText(QString("出站: %1 MB/s").arg(obs /= 1024));

    if(pr > 0)
        ui->pendingStackSize->setText(QString("待发积压: %1").arg(pr));
    else
        ui->pendingStackSize->setText("待发积压: --");

    if(!fileState.isEmpty())
        onFileDownLoadState(fileState);
}
void MainWindow::onPeerTableClicked(QTableWidgetItem* item)
{
    if(item)
    {
        int row = item->row();
        if(currentRow!=row)
            currentRow=row;
        else
            return;
        if(row < 0 || row >= ui->peerTable->rowCount())
            return;
        QTableWidgetItem* hostNumItem = ui->peerTable->item(row, 0);
        QTableWidgetItem* peerNameItem = ui->peerTable->item(row, 1);
        if(!hostNumItem || !peerNameItem)
            return;
        currentPeerHostNum = hostNumItem->text().toInt();
        QString peerName = peerNameItem->text();
        ui->chatTitle->setText(QString("与 %1 (%2) 的对话").arg(peerName).arg(currentPeerHostNum));
        ui->btnAttach->setEnabled(true);
        ui->btnSend->setEnabled(true);
        reloadChatHistory();
        updateFileTransferTable();
        updateCallButtonState();
    }
}
//(1.1)收信
void MainWindow::addChatBubble(QListWidget* listWidget, const QString& text, bool isSelf, bool isBroadcast)
{
    QWidget* bubbleContainer = new QWidget();
    QHBoxLayout* containerLayout = new QHBoxLayout(bubbleContainer);
    containerLayout->setContentsMargins(isSelf ? 60 : 8, 4, isSelf ? 8 : 60, 4);
    containerLayout->setSpacing(0);
    QLabel* bubbleLabel = new QLabel(isBroadcast ? QString("[广播] %1").arg(text) : text);
    bubbleLabel->setWordWrap(true);
    bubbleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubbleLabel->setStyleSheet(isSelf
                                   ? "background-color: #DCF8C6; border-radius: 10px; padding: 8px 12px; color: #333;"
                                   : "background-color: #FFFFFF; border-radius: 10px; padding: 8px 12px; color: #333; border: 1px solid #ddd;");
    if(isSelf)
        containerLayout->addStretch();
    containerLayout->addWidget(bubbleLabel);
    if(!isSelf)
        containerLayout->addStretch();
    QListWidgetItem* item = new QListWidgetItem(listWidget);
    item->setSizeHint(QSize(bubbleContainer->sizeHint().width(), bubbleContainer->sizeHint().height() + 8));
    listWidget->setItemWidget(item, bubbleContainer);
    listWidget->scrollToBottom();
}
void MainWindow::reloadChatHistory()
{
    ui->chatListWidget->clear();
    QStringList history = peerChatHistory.value(currentPeerHostNum, QStringList());
    for(const QString& item : history)
    {
        QStringList parts = item.split("|");
        if(parts.size() >= 2)
        {
            bool isSelf = parts[0] == "self";
            bool isBroadcast = parts.size() >= 3 && parts[1] == "broadcast";
            QString msg = isBroadcast ? parts[2] : parts[1];
            addChatBubble(ui->chatListWidget, msg, isSelf, isBroadcast);
        }
    }
}
void MainWindow::onPeerMsgReceived(int peerHostNum,const QString& msg)
{
    if(!peerNames.contains(peerHostNum))
        return;
    peerChatHistory[peerHostNum].append(QString("peer|%1").arg(msg));
    if(currentPeerHostNum == peerHostNum)
        addChatBubble(ui->chatListWidget, msg, false, false);
}
//(1.2)收文件
void MainWindow::updateFileReceiveTable()
{
    ui->fileReceiveTable->setRowCount(0);
    for(auto it = fileReceiveHash.begin(); it != fileReceiveHash.end(); ++it)
    {
        for(const FileReceiveInfo& info : it.value())
        {
            int row = ui->fileReceiveTable->rowCount();
            ui->fileReceiveTable->insertRow(row);
            ui->fileReceiveTable->setItem(row, 0, new QTableWidgetItem(info.fileName));
            ui->fileReceiveTable->setItem(row, 1, new QTableWidgetItem(peerNames.value(it.key(), QString::number(it.key()))));
            QProgressBar* progressBar = new QProgressBar();
            progressBar->setRange(0, 100);
            progressBar->setValue(static_cast<int>(info.progress));
            progressBar->setFormat(QString("%1%").arg(static_cast<int>(info.progress)));
            ui->fileReceiveTable->setCellWidget(row, 2, progressBar);
        }
    }
}
void MainWindow::onFileDownLoadState(const QList<fileDownLoadState> & stateList)
{
    for (const fileDownLoadState& state : stateList)
    {
        int peerNum = state.peerHostNum;
        if (!fileReceiveHash.contains(peerNum))
            fileReceiveHash.insert(peerNum, QVector<FileReceiveInfo>());
        QVector<FileReceiveInfo>& vec = fileReceiveHash[peerNum];
        bool found = false;
        for (auto& info : vec)
        {
            if (info.fileName == state.filename)
            {
                info.progress = state.progress;
                found = true;
                break;
            }
        }
        if (!found)
            vec.append(FileReceiveInfo(state.filename, state.progress));
    }
    updateFileReceiveTable();
}

void MainWindow::onFileDownLoadFinish(const QString & fileName, int peerHostNum)
{
    ui->stateMsg->appendPlainText(QString("来自'%1'的文件'%2'接收完毕").arg(peerNames[peerHostNum]).arg(fileName));
    if (fileReceiveHash.contains(peerHostNum))
    {
        QVector<FileReceiveInfo>& vec = fileReceiveHash[peerHostNum];
        for (auto& info : vec)
        {
            if (info.fileName == fileName)
            {
                info.progress = 100;
                break;
            }
        }
    }
    updateFileReceiveTable();
}
//(2.1)发信
void MainWindow::goSendUnicastMsg()
{
    QString msg = ui->sendingMsg->text();
    if(msg.isEmpty()) return;
    msg.push_front(QTime::currentTime().toString("HH:mm")+QString("\n"));
    if(currentPeerHostNum == 0)
    {
        ui->sendingMsg->clear();
        return;
    }
    QMetaObject::invokeMethod(getDcWorker((void*)ipRoute,currentPeerHostNum,TYPE_TUN), "sendStringMsg", Qt::QueuedConnection,Q_ARG(const QString&, msg));
    peerChatHistory[currentPeerHostNum].append(QString("self|%1").arg(msg));
    addChatBubble(ui->chatListWidget, msg, true, false);
    ui->sendingMsg->clear();
}
void MainWindow::goSendBroadcastMsg()
{
    QString msg = ui->sendingMsg->text();
    if(msg.isEmpty()) return;
    msg.push_front(QTime::currentTime().toString("HH:mm")+QString("\n"));
    if(peerNames.isEmpty())
    {
        ui->sendingMsg->clear();
        return;
    }
    for(dcworker* worker:getBroundCastWorkers((void*)ipRoute))
    {
        worker->newEventNow=true;
        QMetaObject::invokeMethod(worker, "sendStringMsg", Qt::QueuedConnection,Q_ARG(const QString&, msg));
    }
    QString historyItem = QString("self|broadcast|%1").arg(msg);
    for(int hostNum : peerNames.keys()) {
        peerChatHistory[hostNum].append(historyItem);
    }
    if(currentPeerHostNum != 0)
        addChatBubble(ui->chatListWidget, msg, true, true);
    ui->sendingMsg->clear();
}
//(2.2)发文件
QString MainWindow::formatFileSize(qint64 bytes)
{
    const qint64 KB = 1024;
    const qint64 MB = 1024 * KB;
    const qint64 GB = 1024 * MB;
    if (bytes >= GB)
        return QString("%1 GB").arg(QString::number(bytes / (double)GB, 'f', 2));
    else if (bytes >= MB)
        return QString("%1 MB").arg(QString::number(bytes / (double)MB, 'f', 2));
    else if (bytes >= KB)
        return QString("%1 KB").arg(QString::number(bytes / (double)KB, 'f', 2));
    else
        return QString("%1 B").arg(bytes);
}
void MainWindow::updateFileTransferTable()
{
    ui->fileTransferTable->setRowCount(0);
    for (const FileTransferInfo& info : fileTransferHash.value(currentPeerHostNum))
    {
        int row = ui->fileTransferTable->rowCount();
        ui->fileTransferTable->insertRow(row);
        ui->fileTransferTable->setItem(row, 0, new QTableWidgetItem(info.fileName));
        ui->fileTransferTable->setItem(row, 1, new QTableWidgetItem(formatFileSize(info.fileSize)));
        ui->fileTransferTable->setItem(row, 2, new QTableWidgetItem(info.chunkFinished ? "投递完毕" :(info.terminateException?"异常中断":"切块中")));
        ui->fileTransferTable->setItem(row, 3, new QTableWidgetItem(info.createTime.toString("yyyy-MM-dd HH:mm:ss")));
    }
}
void MainWindow::onAttachFile()
{
    if(!currentPeerHostNum)return;
    QString filePath = QFileDialog::getOpenFileName(this, "选择文件", "", "所有文件 (*.*)");
    if(!filePath.isEmpty())
    {
        ui->stateMsg->appendPlainText(QString("选中文件: %1 => 待对方确认接收").arg(filePath));
        QFileInfo fileInfo(filePath);
        QString fileName=fileInfo.fileName();
        dcworker* worker=getDcWorker((void*)ipRoute,currentPeerHostNum,TYPE_FILE);
        uint64_t requestTime=getRunningTime();
        QJsonObject requestJson;
        requestJson["explain"]  = QString("对方%1请求发送文件'%2'给你").arg(localHostName).arg(fileName);
        requestJson["fileName"] = fileName;
        requestJson["fileSize"] = (qint64)fileInfo.size();
        QMetaObject::invokeMethod(worker,"sendFileMsg",Qt::QueuedConnection,
        Q_ARG(const QByteArray&,createRequest(TYPE_FILE,requestTime,requestJson)),Q_ARG(bool,false));
        mission.emplace(requestTime,this,
                        [this,filePath,fileInfo,fileName,worker]()
            {
                QPointer<dcworker> safeDcWorker(worker);
                if(!safeDcWorker)
                    return;
                FileTransferInfo transferInfo(fileName,fileInfo.size(),QDateTime::currentDateTime(),false);
                fileTransferHash[currentPeerHostNum].append(transferInfo);
                filesender* fileSender;QThread* trd;
                fileSenderContanier.insert(fileSender=new filesender(filePath),trd=new QThread);
                fileSender->moveToThread(trd);trd->start();fileSender->running=true;
                QMetaObject::invokeMethod(fileSender,"sendFile",Qt::QueuedConnection,Q_ARG(void*,(void*)worker));
                connect(fileSender,&filesender::fileSendStop,this,[this, trd, fileName](bool isFinish){
                    trd->quit();
                    for (FileTransferInfo& info : fileTransferHash[currentPeerHostNum])
                        if (info.fileName == fileName)
                        {
                            if(isFinish)
                                info.chunkFinished = isFinish;
                            else
                                info.terminateException=true;
                            break;
                        }
                    updateFileTransferTable();
                });
                connect(trd,&QThread::finished,this,[this,fileSender,trd](){
                    delete(fileSender);
                    fileSenderContanier.remove(fileSender);
                    trd->deleteLater();
                });
            });
    }
}
//(3)设置dcWorker
void MainWindow::onSettingsClicked()
{
    if(!dcManager)
        return;
    SettingsDialog dialog(dcManager->busySize, dcManager->freeSize,
                          dcManager->nameRoute, dcManager->ipRoute,
                          ui->subnetPrefix->text(), this);
    connect(&dialog, &SettingsDialog::applyConnectionCount, this, [this](int targetAmount, const QList<int>& peerHostNums){
        for(int peerHostNum : peerHostNums)
        {
            if(!ipRoute || !ipRoute->contains(peerHostNum))
                continue;
            int currentAmount = ipRoute->value(peerHostNum).size();
            if(currentAmount == targetAmount)
                continue;
            if(targetAmount > currentAmount)
                QMetaObject::invokeMethod(dcManager, "getExtraConnection", Qt::QueuedConnection,
                                          Q_ARG(int, targetAmount), Q_ARG(int, peerHostNum));
            else
                QMetaObject::invokeMethod(dcManager, "releaseExtraConnection", Qt::QueuedConnection,
                                          Q_ARG(int, targetAmount), Q_ARG(int, peerHostNum));
        }
    });
    if(dialog.exec() == QDialog::Accepted)
    {
        int busySize = dialog.getBusySize();
        int freeSize = dialog.getFreeSize();
        QMetaObject::invokeMethod(dcManager, "updateAllDcWorkerSettings", Qt::QueuedConnection,
                                  Q_ARG(int, busySize), Q_ARG(int, freeSize));
        ui->stateMsg->appendPlainText(QString("参数已更新 - busySize: %1, freeSize: %2")
                                          .arg(busySize).arg(freeSize));
    }
}
