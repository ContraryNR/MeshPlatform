#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QHash>
#include <QVector>

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    //参数设置控件
    QSpinBox* busySizeSpinBox;
    QSpinBox* freeSizeSpinBox;

    //连接数表格
    QTableWidget* connTable;
    QSpinBox* connectionCountSpinBox;
    QPushButton* btnApplyConnection;
    QCheckBox* connSelectAllCheckBox;

    //确认/取消按钮
    QPushButton* btnConfirm;
    QPushButton* btnCancel;

    SettingsDialog(int currentBusySize, int currentFreeSize,
                   const QHash<int, QString>& nameRoute,
                   const QHash<int, QVector<class dcworker*>>& ipRoute,
                   const QString& subnetPrefix,
                   QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("参数设置");
        setMinimumSize(520, 500);

        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        //1.参数设置组
        QGroupBox* paramGroup = new QGroupBox("数据通道参数", this);
        QFormLayout* paramLayout = new QFormLayout(paramGroup);

        busySizeSpinBox = new QSpinBox(this);
        busySizeSpinBox->setRange(1024, 1048576);
        busySizeSpinBox->setValue(currentBusySize);
        busySizeSpinBox->setSuffix(" 字节");
        paramLayout->addRow("busySize:", busySizeSpinBox);

        freeSizeSpinBox = new QSpinBox(this);
        freeSizeSpinBox->setRange(1024, 1048576);
        freeSizeSpinBox->setValue(currentFreeSize);
        freeSizeSpinBox->setSuffix(" 字节");
        paramLayout->addRow("freeSize:", freeSizeSpinBox);

        mainLayout->addWidget(paramGroup);

        //2.连接数表格组
        QGroupBox* connGroup = new QGroupBox("Peer连接数管理", this);
        QVBoxLayout* connLayout = new QVBoxLayout(connGroup);

        connTable = new QTableWidget(this);
        connTable->setColumnCount(4);
        connTable->setHorizontalHeaderLabels({"选择", "Peer地址", "主机名", "当前连接数"});
        connTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        connTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        connTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

        int row = 0;
        for(auto it = nameRoute.begin(); it != nameRoute.end(); ++it)
        {
            int peerHostNum = it.key();
            QString peerHostName = it.value();
            QString virtualIP = subnetPrefix + "." + QString::number(peerHostNum);
            int connectionCount = ipRoute.contains(peerHostNum) ? ipRoute[peerHostNum].size() : 0;

            connTable->insertRow(row);
            QTableWidgetItem* checkItem = new QTableWidgetItem();
            checkItem->setCheckState(Qt::Unchecked);
            connTable->setItem(row, 0, checkItem);
            QTableWidgetItem* ipItem = new QTableWidgetItem(virtualIP);
            ipItem->setData(Qt::UserRole, peerHostNum);
            connTable->setItem(row, 1, ipItem);
            connTable->setItem(row, 2, new QTableWidgetItem(peerHostName));
            connTable->setItem(row, 3, new QTableWidgetItem(QString::number(connectionCount)));
            row++;
        }
        connLayout->addWidget(connTable);

        QHBoxLayout* connSelectLayout = new QHBoxLayout();
        connSelectAllCheckBox = new QCheckBox("全选", this);
        connect(connSelectAllCheckBox, &QCheckBox::toggled, this, [this](bool checked){
            for(int i = 0; i < connTable->rowCount(); i++)
            {
                QTableWidgetItem* item = connTable->item(i, 0);
                if(item)
                    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            }
        });
        connSelectLayout->addWidget(connSelectAllCheckBox);
        connSelectLayout->addStretch();
        connLayout->addLayout(connSelectLayout);

        QHBoxLayout* connApplyLayout = new QHBoxLayout();
        connApplyLayout->addWidget(new QLabel("目标连接数:", this));
        connectionCountSpinBox = new QSpinBox(this);
        connectionCountSpinBox->setRange(1, 4);
        connectionCountSpinBox->setValue(1);
        connApplyLayout->addWidget(connectionCountSpinBox);
        btnApplyConnection = new QPushButton("应用连接数", this);
        connect(btnApplyConnection, &QPushButton::clicked, this, &SettingsDialog::onApplyConnection);
        connApplyLayout->addWidget(btnApplyConnection);
        connApplyLayout->addStretch();
        connLayout->addLayout(connApplyLayout);

        mainLayout->addWidget(connGroup);

        //3.确认/取消按钮
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

    int getBusySize() const { return busySizeSpinBox->value(); }
    int getFreeSize() const { return freeSizeSpinBox->value(); }

    QList<int> getSelectedPeerHostNums() const
    {
        QList<int> selected;
        for(int i = 0; i < connTable->rowCount(); i++)
        {
            QTableWidgetItem* checkItem = connTable->item(i, 0);
            if(checkItem && checkItem->checkState() == Qt::Checked)
            {
                QTableWidgetItem* ipItem = connTable->item(i, 1);
                if(ipItem)
                    selected.append(ipItem->data(Qt::UserRole).toInt());
            }
        }
        return selected;
    }

signals:
    void applyConnectionCount(int targetAmount, const QList<int>& peerHostNums);

private slots:
    void onApplyConnection()
    {
        QList<int> selectedPeers = getSelectedPeerHostNums();
        if(selectedPeers.isEmpty())
            return;
        emit applyConnectionCount(connectionCountSpinBox->value(), selectedPeers);
    }
};

#endif // SETTINGSDIALOG_H
