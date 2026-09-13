#ifndef TOPBASEDIALOG_H
#define TOPBASEDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFont>
#include <QPushButton>

class TopBaseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TopBaseDialog(const QString& title, const QString& question,
                           const QString& paramsText, const QString& btn1Text,
                           const QString& btn0Text, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setMinimumWidth(360);
        mainLayout = new QVBoxLayout(this);

        QLabel* titleLabel = new QLabel(question, this);
        QFont f = titleLabel->font();
        f.setBold(true);
        titleLabel->setFont(f);
        titleLabel->setWordWrap(true);
        mainLayout->addWidget(titleLabel);

        if(!paramsText.isEmpty())
        {
            QLabel* paramsLabel = new QLabel("对方参数:\n" + paramsText, this);
            QFont mono("Consolas", 10);
            mono.setStyleHint(QFont::Monospace);
            paramsLabel->setFont(mono);
            paramsLabel->setStyleSheet("background:#F5F5F5; padding:6px;");
            paramsLabel->setAlignment(Qt::AlignLeft);
            mainLayout->addWidget(paramsLabel);
        }

        btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        btnBox->button(QDialogButtonBox::Ok)->setText(btn1Text);
        btnBox->button(QDialogButtonBox::Cancel)->setText(btn0Text);
        mainLayout->addWidget(btnBox);
        connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    bool accepted() const { return result() == QDialog::Accepted; }

protected:
    QVBoxLayout* mainLayout;
    QDialogButtonBox* btnBox;
};

#endif
