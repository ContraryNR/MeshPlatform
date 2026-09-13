#ifndef STARTUPDIALOG_H
#define STARTUPDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QRandomGenerator>

class StartupDialog : public QDialog
{
    Q_OBJECT
public:
    QLineEdit* hostNameEdit;
    QRadioButton* radioCoordinator;
    QRadioButton* radioPeer;
    QRadioButton* radioOnline;
    QRadioButton* radioOffline;
    QPushButton* btnConfirm;
    QPushButton* btnCancel;

    StartupDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("启动配置");
        setMinimumSize(350, 280);

        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        //1.主机名输入
        QGroupBox* nameGroup = new QGroupBox("主机标识名", this);
        QFormLayout* nameLayout = new QFormLayout(nameGroup);
        hostNameEdit = new QLineEdit(this);
        hostNameEdit->setPlaceholderText("留空将自动生成");
        nameLayout->addRow("名称:", hostNameEdit);
        mainLayout->addWidget(nameGroup);

        //2.角色选择
        QGroupBox* roleGroup = new QGroupBox("运行模式", this);
        QHBoxLayout* roleLayout = new QHBoxLayout(roleGroup);
        radioCoordinator = new QRadioButton("Coordinator", this);
        radioPeer = new QRadioButton("Peer", this);
        radioCoordinator->setChecked(true);
        roleLayout->addWidget(radioCoordinator);
        roleLayout->addWidget(radioPeer);
        mainLayout->addWidget(roleGroup);

        //3.在线/离线模式
        QGroupBox* modeGroup = new QGroupBox("网络模式", this);
        QHBoxLayout* modeLayout = new QHBoxLayout(modeGroup);
        radioOnline = new QRadioButton("在线 (TCP Signaling)", this);
        radioOffline = new QRadioButton("离线 (JSON文件交换)", this);
        radioOnline->setChecked(true);
        modeLayout->addWidget(radioOnline);
        modeLayout->addWidget(radioOffline);
        mainLayout->addWidget(modeGroup);

        //4.确认/取消按钮
        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        btnConfirm = new QPushButton("确认", this);
        btnConfirm->setMinimumSize(80, 30);
        connect(btnConfirm, &QPushButton::clicked, this, &QDialog::accept);
        btnLayout->addWidget(btnConfirm);
        btnCancel = new QPushButton("取消", this);
        btnCancel->setMinimumSize(80, 30);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        btnLayout->addWidget(btnCancel);
        mainLayout->addLayout(btnLayout);
    }

    QString getHostName() const
    {
        QString name = hostNameEdit->text().trimmed();
        if(name.isEmpty())
            name = QString("Host_%1").arg(QRandomGenerator::global()->bounded(1000, 9999));
        return name;
    }

    bool getIsCoordinator() const { return radioCoordinator->isChecked(); }
    bool getOnlineMode() const { return radioOnline->isChecked(); }
};

#endif // STARTUPDIALOG_H
