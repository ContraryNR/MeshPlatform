#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "startupdialog.h"

netConfig MainWindow::getCoordinateNetConfigFromUI()
{
    return netConfig(ui->listenIP->currentText(), ui->listenPort->value());
}

netConfig MainWindow::getPeerNetConfigFromUI()
{
    return netConfig(ui->coordIP->text(), ui->coordPort->value());
}

void MainWindow::initialUI()
{
    StartupDialog dlg(this);
    if(dlg.exec() != QDialog::Accepted)
    {
        localHostName = QString("Host_%1").arg(QRandomGenerator::global()->bounded(1000, 9999));
        isCoordinator = true;
        onlineMode = true;
    }
    else
    {
        localHostName = dlg.getHostName();
        isCoordinator = dlg.getIsCoordinator();
        onlineMode = dlg.getOnlineMode();

    }
    ui->hostNameDisplay->setText(localHostName);
    ui->modeDisplay->setText(isCoordinator ? "Coordinator" : "Peer");
    ui->coordinatorGroup->setVisible(isCoordinator);
    ui->peerGroup->setVisible(!isCoordinator);
    if(isCoordinator)
    {
        for(const QHostAddress& addr : QNetworkInterface::allAddresses())
            if(addr.protocol() == QAbstractSocket::IPv4Protocol)
                ui->listenIP->addItem(addr.toString());
        int index = ui->listenIP->findText("127.0.0.1");
        ui->listenIP->setCurrentIndex(index >= 0 ? index : 0);
    }
    else
        ui->coordIP->setText("127.0.0.1");//限目前测试用
    for(QPushButton* button : findChildren<QPushButton*>())
        button->setEnabled(false);
    ui->btnStart->setEnabled(true);
    ui->cleanState->setEnabled(true);
    ui->cleanMessage->setEnabled(true);
    connect(ui->cleanMessage, &QPushButton::clicked, ui->chatListWidget, &QListWidget::clear);
    connect(ui->cleanState, &QPushButton::clicked, ui->stateMsg, &QPlainTextEdit::clear);
    connect(ui->btnSend, &QPushButton::clicked, this, &MainWindow::goSendUnicastMsg);
    connect(ui->btnBroadcast, &QPushButton::clicked, this, &MainWindow::goSendBroadcastMsg);
    connect(ui->peerTable, &QTableWidget::itemClicked, this, &MainWindow::onPeerTableClicked);
    connect(ui->btnAttach, &QPushButton::clicked, this, &MainWindow::onAttachFile);
    connect(ui->btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->btnVideoChat, &QPushButton::clicked, this, &MainWindow::onAppealVideoCallRequest);
    connect(ui->btnVoiceChat, &QPushButton::clicked, this, &MainWindow::onAppealAudioCallRequest);
    connect(ui->sendingMsg, &QLineEdit::returnPressed, this, [this](){
        if(ui->btnSend->isEnabled())
            goSendUnicastMsg();
    });
    connect(ui->btnLoadJson, &QPushButton::clicked, this, [this](){
        QString filePath = QFileDialog::getOpenFileName(this, "选择JSON文件", QDir::currentPath(), "JSON文件 (*.json)");
        if(!filePath.isEmpty() && jsonLoader)
            jsonLoader->loadJsonFile(filePath);
    });
}
bool MainWindow::requestDialog(const QString& title, const QString& text, const QString& btnText1, const QString& btnText0)
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(QMessageBox::Question);
    QPushButton* button1 = msgBox.addButton(btnText1, QMessageBox::ActionRole);
    QPushButton* button0 = msgBox.addButton(btnText0, QMessageBox::ActionRole);
    msgBox.setDefaultButton(button1);
    msgBox.exec();
    return (QPushButton*)(msgBox.clickedButton()) == button1 ? 1 : 0;
}
